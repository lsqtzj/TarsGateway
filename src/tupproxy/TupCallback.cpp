#include "GatewayServer.h"
#include "ProxyImp.h"
#include "TupProxyManager.h"
#include "proxybase/ProxyUtils.h"
#include "proxybase/ReportHelper.h"
#include "servant/Application.h"
#include "tup/tup.h"
#include "util/tc_base64.h"
#include "util/tc_config.h"
#include "util/tc_gzip.h"
#include "util/tc_hash_fun.h"
#include "util/tc_http.h"
#include "util/tc_mysql.h"
#include "util/tc_parsepara.h"
#include "util/tc_tea.h"
#include <zlib.h>

using namespace std;
using namespace tup;

//////////////////////////////////////////////////////

TupCallback::~TupCallback()
{
    handleResponse();
}

int TupCallback::onDispatch(ReqMessagePtr msg)
{
    if (msg->response->iRet == 0)
    {
        TLOG_DEBUG("================ getType:" << getType() << ", reqId:" << _iNewRequestId << ", iRet:" << msg->response->iRet << endl);
        if (getType() == "tup")
        {
            vector<char> &buff = msg->response->sBuffer;
            if (!buff.empty())
            {
                ReportHelper::reportProperty("response_iVersion_tup");
                doResponse_tup(buff);
            }
        }
        else if (getType() == "json")
        {
            ReportHelper::reportProperty("response_iVersion_json");
            doResponse_json(msg->response);
        }
        else if (getType() == "tars")
        {
            ReportHelper::reportProperty("response_iVersion_tars");
            doResponse_tars(msg->response);
        }
    }
    else
    {
        TLOG_ERROR("================ getType:" << getType() << ", reqId:" << _iNewRequestId << ", iRet:" << msg->response->iRet 
             << ", servant:" << _stParam->sServantName << ", func:" << _stParam->sFuncName << endl);
        if (getType() == "tup" || getType() == "json" || getType() == "tars")
        {
            vector<char> &buff = msg->response->sBuffer;

            TLOG_ERROR("buff.size:" << buff.size() << ", iVersion:" << msg->response->iVersion << endl);

            doResponseException(msg->response->iRet, buff);
        }
    }

    if (!getTraceKey().empty())
    {
        string _trace_param_;
        int _trace_param_flag_ = ServantProxyThreadData::needTraceParam(ServantProxyThreadData::TraceContext::EST_TE, getTraceKey(), msg->response->sBuffer.size());
        if (ServantProxyThreadData::TraceContext::ENP_NORMAL == _trace_param_flag_)
        {
            if (getType() == "json")
            {
                _trace_param_.assign(msg->response->sBuffer.begin(), msg->response->sBuffer.end());
            }
            else
            {
                _trace_param_ = "tup-bin";
            }
        }
        else if(ServantProxyThreadData::TraceContext::ENP_OVERMAXLEN == _trace_param_flag_)
        {
            _trace_param_ = "{\"trace_param_over_max_len\":true, \"data_len\":" + TC_Common::tostr(msg->response->sBuffer.size()) + "}";
        }
        
        TARS_TRACE(getTraceKey(), TRACE_ANNOTATION_TE, ServerConfig::Application + "." + ServerConfig::ServerName, _stParam->sServantName, _stParam->sFuncName, msg->response->iRet, _trace_param_, ""); 
    }

    return 0;
}

void TupCallback::doResponse_tup(const vector<char> &buffer)
{
    try
    {
        tars::TarsInputStream<BufferReader> is;

        //去掉四个字节的长度
        if (buffer.size() > 4)
        {
            is.setBuffer(&buffer[0] + 4, buffer.size() - 4);
        }
        else
        {
            TLOG_ERROR("buffer.size = " << buffer.size() << endl);
            return;
        }

        RequestPacket req;
        req.readFrom(is);
        TLOG_DEBUG("read tup RequestPacket succ." << endl);

        req.iRequestId = _stParam->iRequestId;
        req.sServantName = _stParam->sServantName;
        req.sFuncName = _stParam->sFuncName;

        tars::TarsOutputStream<BufferWriter> os;
        req.writeTo(os);

        if (os.getLength() > g_app.getRspSizeLimit())
        {
            TLOG_ERROR("packet is too big tup|" << os.getLength()
                                               << "|" << _stParam->sServantName
                                               << "|" << _stParam->sFuncName
                                               << "|" << _stParam->sReqGuid
                                               << endl);
            ReportHelper::reportProperty("RspSizeLimit", 1, 1);
        }

        //doRSP的时候也要加上头部4个字节
        unsigned int bufferlength = os.getLength() + 4;
        bufferlength = htonl(bufferlength);

        _rspBuffer.resize(os.getLength() + 4);
        memcpy(&_rspBuffer[0], (char *)&bufferlength, 4);
        memcpy(&_rspBuffer[4], os.getBuffer(), os.getLength());

        TLOG_DEBUG(_stParam->sServantName << "::"
                                        << _stParam->sFuncName << ", requestid:"
                                        << req.iRequestId << ", length:" << os.getLength() << endl);

        handleResponse();
    }
    catch (exception &ex)
    {
        TLOG_ERROR("exception: " << ex.what() << endl);
    }
    catch (...)
    {
        TLOG_ERROR("exception: unknown error." << endl);
    }
}

void TupCallback::doResponse_tars(shared_ptr<ResponsePacket> tup)
{
    try
    {
        if (tup->iRequestId != _iNewRequestId)
        {
            TLOG_ERROR("find tup origin request error:"
                      << _stParam->sServantName << "::"
                      << _stParam->sFuncName << " requestid:"
                      << tup->iRequestId << ", no match origin request:" << _iNewRequestId
                      << endl);
            return;
        }

        ////回复包设置为请求包的信息
        tup->iRequestId = _stParam->iRequestId;
        //tup->sServantName = _stParam->sServantName;
        //tup->sFuncName = _stParam->sFuncName;

        tars::TarsOutputStream<BufferWriter> os;
        tup->writeTo(os);

        if (os.getLength() > g_app.getRspSizeLimit())
        {
            TLOG_ERROR("packet is too big tup|" << os.getLength()
                                               << "|" << _stParam->sServantName
                                               << "|" << _stParam->sFuncName
                                               << "|" << _stParam->sReqGuid
                                               << endl);
            ReportHelper::reportProperty("RspSizeLimit", 1, 1);
        }

        //doRSP的时候也要加上头部4个字节
        unsigned int bufferlength = os.getLength() + 4;
        bufferlength = htonl(bufferlength);

        _rspBuffer.resize(os.getLength() + 4);
        memcpy(&_rspBuffer[0], (char *)&bufferlength, 4);
        memcpy(&_rspBuffer[4], os.getBuffer(), os.getLength());

        TLOG_DEBUG(_stParam->sServantName << "::"
                                        << _stParam->sFuncName << ", requestid:"
                                        << tup->iRequestId << ", length:" << os.getLength() << endl);

        handleResponse();
    }
    catch (exception &ex)
    {
        TLOG_ERROR("error:" << ex.what() << endl);
    }
    catch (...)
    {
        TLOG_ERROR("unknown error." << endl);
    }
}

void TupCallback::doResponse_json(shared_ptr<ResponsePacket> tup)
{
    try
    {
        if (tup->iRequestId != _iNewRequestId)
        {
            TLOG_DEBUG("request error: requestid:" << tup->iRequestId << ", origin request:" << _iNewRequestId << endl);
            return;
        }

        if (_stParam->isRestful)
        {
            _rspBuffer.assign(tup->sBuffer.begin(), tup->sBuffer.end());
        }
        else
        {
            tars::JsonValueObjPtr p = new tars::JsonValueObj();
            p->value["reqid"] = tars::JsonOutput::writeJson(_stParam->iRequestId);
            p->value["data"] = tars::JsonOutput::writeJson(string(tup->sBuffer.begin(), tup->sBuffer.end()));
            string buff = tars::TC_Json::writeValue(p);
            _rspBuffer.resize(buff.length());
            _rspBuffer.assign(buff.begin(), buff.end());
        }

        handleResponse();
    }
    catch (exception &ex)
    {
        TLOG_ERROR("exception:" << ex.what() << endl);
    }
    catch (...)
    {
        TLOG_ERROR("exception: unknown error." << endl);
    }
}

void TupCallback::doResponseException(int ret, const vector<char> &buffer)
{
    try
    {
        TLOG_ERROR("ret:" << ret << ", buffer len:" << buffer.size() << endl);

        //记录调用开始时间和结束时间的两者之差
        int64_t nowTime = TC_Common::now2ms();
        TLOG_ERROR(nowTime - _stParam->iTime << "|"
                                           << _stParam->sReqIP << "|"
                                           << _stParam->sReqGuid << "|"
                                           << _stParam->sServantName << "|"
                                           << _stParam->sFuncName << "|"
                                           << _stParam->iEptType << "|"
                                           << _stParam->iZipType << "|"
                                           << endl);

        FDLOG("tupcall_exception") << _stParam->sReqIP << "|"
                                   << _stParam->sServantName << "|"
                                   << _stParam->sFuncName << "|"
                                   << _stParam->sReqGuid << "|"
                                   << _stParam->sReqXua << "|"
                                   << _stParam->iEptType << "|"
                                   << _stParam->iZipType << "|"
                                   << nowTime - _stParam->iTime << "|"
                                   << ret
                                   << endl;
        
        if (ret == -7)
        {
            ProxyUtils::doErrorRsp(504, _current, EPT_TUP_PROXY, _stParam->httpKeepAlive);
        }
        else
        {
            ProxyUtils::doErrorRsp(502, _current, EPT_TUP_PROXY, _stParam->httpKeepAlive);
        }

        ReportHelper::reportStat(g_app.getLocalServerName(), "RequestMonitor", "doResponseException", 0);
    }
    catch (exception &ex)
    {
        TLOG_ERROR("error:" << ex.what() << endl);
    }
    catch (...)
    {
        TLOG_ERROR("unknown error." << endl);
    }
}

void TupCallback::handleResponse()
{
	TLOG_DEBUG("rsp size:" << _rspBuffer.size() <<", " << _stParam->sServantName << "::" << _stParam->sFuncName << endl);

    bool bGzipOk = !_stParam->pairAcceptZip.first.empty();
    bool bEncrypt = !_stParam->pairAcceptEpt.first.empty();

    size_t iOrgRspLen = _rspBuffer.size();

    //回复http协议
    TC_HttpResponse httpResponse;
    httpResponse.setHeader("Date", TC_Common::now2GMTstr());
    httpResponse.setHeader("Server", "TarsGateway-Server");
    httpResponse.setHeader("Cache-Control", "no-cache"); //不缓存内容

    if (_stParam->httpKeepAlive)
    {
        httpResponse.setConnection("keep-alive");
    }
    else
    {
        httpResponse.setConnection("close");
    }

    if (getType() == "json")
    {
        httpResponse.setHeader("Content-Type", "application/json");
        g_app.setRspHeaders((int)EPT_JSON_PROXY, _stParam->sServantName, httpResponse);
    }
    else
    {
        httpResponse.setHeader("Content-Type", "application/octet-stream");
        g_app.setRspHeaders((int)EPT_TUP_PROXY, _stParam->sServantName, httpResponse);
    }

    vector<char> tmpBuffer = _rspBuffer;

    if (bGzipOk)
    {
        //压缩
        bGzipOk = TC_GZip::compress(&_rspBuffer[0], _rspBuffer.size(), tmpBuffer);

        if (bGzipOk && tmpBuffer.size() < _rspBuffer.size())
        {
            TLOG_DEBUG(" gzip: " << _rspBuffer.size() << "->" << tmpBuffer.size() << endl);

            _rspBuffer.swap(tmpBuffer);

            httpResponse.setHeader(_stParam->pairAcceptZip.first, _stParam->pairAcceptZip.second);
        }
    }

    if (bEncrypt)
    {
        //加密
        TC_Tea::encrypt(_stParam->sEncryptKey.c_str(), &_rspBuffer[0], _rspBuffer.size(), tmpBuffer);

        TLOG_DEBUG(" encrypt: " << _rspBuffer.size() << "->" << tmpBuffer.size() << endl);

        _rspBuffer.swap(tmpBuffer);

        httpResponse.setHeader(_stParam->pairAcceptEpt.first, _stParam->pairAcceptEpt.second);
    }

    TLOG_DEBUG("rsp buffer length:" << _rspBuffer.size() << endl);

    httpResponse.setResponse(200, "OK", &_rspBuffer[0], _rspBuffer.size());

    string response = httpResponse.encode();

    TLOG_DEBUG("\r\n"
              << response.substr(0, response.find("\r\n\r\n")) << endl);

    _current->sendResponse(response.c_str(), response.length());
    if (!_stParam->httpKeepAlive)
    {
        _current->close();
    }

    // 日志及统计上报等
    ReportHelper::reportStat(g_app.getLocalServerName(), "RequestMonitor", "handleOK", 0);
    ReportHelper::reportProperty("TupTotalRspNum");

    //记录调用开始时间和结束时间的两者之差
    int64_t nowTime = TC_Common::now2ms();
    FDLOG("response") << _stParam->sReqIP << "|"
                      << _stParam->sReqGuid << "|"
                      << _stParam->sReqXua << "|"
                      << _stParam->sServantName << "|"
                      << _stParam->sFuncName << "|"
                      << _stParam->iEptType << "|"
                      << _stParam->iZipType << "|"
                      << bEncrypt << "|"
                      << bGzipOk << "|"
                      << nowTime - _stParam->iTime << "|"
                      << _rspBuffer.size()
                      << endl;

    //超级包打一个错误日志
    if (iOrgRspLen > g_app.getRspSizeLimit())
    {
        TLOG_ERROR("packet is too big all|" << iOrgRspLen
                                           << "|" << _stParam->sReqGuid << ", " << _stParam->sServantName << "|" << _stParam->sFuncName
                                           << endl);
        ReportHelper::reportProperty("RspTotalSizeLimit", 1, 1);
    }

    _rspBuffer.clear();
}

/*
void VerifyCallback::callback_verify(tars::Int32 ret,  const Base::VerifyRsp& rsp)
{
    if (0 != ret || rsp.ret != EVC_SUCC)
    {
        TLOG_ERROR("verify ret error:" << ret << "|verify code:" << rsp.ret << "|" << _request->sServantName << ":" << _request->sFuncName << endl);
        ProxyUtils::doErrorRsp(401,  _param->current, _param->proxyType, _param->httpKeepAlive, "Unauthorized: veirfy fail, ret=" + TC_Common::tostr(rsp.ret));
        return;
    }
    else
    {
        TLOG_DEBUG(_request->sServantName << ":" << _request->sFuncName << "|async_verify succ:" << rsp.ret << ", " << rsp.uid << ", " << rsp.context << endl);
    }

    _request->context["X-Verify-UID"] = rsp.uid;
    _request->context["X-Verify-Data"] = rsp.context;
    _request->context["X-Verify-Token"] = _token;

    TupBase::callServer(_proxy, _param, _request, _hashInfo);
}

void VerifyCallback::callback_verify_exception(tars::Int32 ret)
{
    TLOG_ERROR("async_verify ret error:" << ret << "|" << _request->sServantName << ":" << _request->sFuncName << endl);
    ProxyUtils::doErrorRsp(401, _param->current, _param->proxyType, _param->httpKeepAlive, "Unauthorized: verify exception, ret:" + TC_Common::tostr(ret));
}
*/

int VerifyCallbackEx::onDispatch(ReqMessagePtr msg)
{
    TLOG_DEBUG("ret:" << msg->response->iRet<< endl);
    if (msg->response->iRet == 0)
    {
        tars::TarsInputStream<tars::BufferReader> is;
        is.setBuffer(msg->response->sBuffer);
        Base::VerifyRsp rsp;
        is.read(rsp, 2, true);
        if (rsp.ret != EVC_SUCC)
        {
            TLOG_ERROR("verify ret error:" << msg->response->iRet << "|verify code:" << rsp.ret << "|" << _request->sServantName << ":" << _request->sFuncName << endl);
            ProxyUtils::doErrorRsp(401, _param->current, _param->proxyType, _param->httpKeepAlive, "Unauthorized: veirfy fail, ret=" + TC_Common::tostr(rsp.ret));
            return -1;
        }
        else
        {
            TLOG_DEBUG(_request->sServantName << ":" << _request->sFuncName << "|async_verify succ:" << rsp.ret << ", " << rsp.uid << ", " << rsp.context << endl);
        }

        _request->context["X-Verify-UID"] = rsp.uid;
        _request->context["X-Verify-Data"] = rsp.context;
        _request->context["X-Verify-Token"] = _token;

        TupBase::callServer(_proxy, _param, _request, _hashInfo);
    }
    else
    {
        TLOG_ERROR("async_verify ret error:" << msg->response->iRet << "|" << _request->sServantName << ":" << _request->sFuncName << endl);
        ProxyUtils::doErrorRsp(401, _param->current, _param->proxyType, _param->httpKeepAlive, "Unauthorized: veirfy exception, ret=" + TC_Common::tostr(msg->response->iRet));
    }

    return 0;
}

//////////////////////////////////////////////////////

#include "TupBase.h"
#include "servant/Application.h"
#include "tup/tup.h"
#include "util/tc_config.h"
#include "util/tc_hash_fun.h"
#include "util/tc_http.h"
#include "util/tc_mysql.h"
#include "util/tc_parsepara.h"
//#include "TupProxyManager.h"
#include "proxybase/FlowControlManager.h"
#include "GatewayServer.h"
#include "httpproxy/StationManager.h"
#include "proxybase/ProxyUtils.h"
#include "proxybase/ReportHelper.h"
#include "util/tc_config.h"
#include "util/tc_gzip.h"
#include "util/tc_hash_fun.h"
#include "util/tc_parsepara.h"
#include "util/tc_tea.h"
#include <zlib.h>
#include "util/tc_md5.h"
#include "util/tc_uuid_generator.h"
#include "TraceControl.h"

//////////////////////////////////////////////////////

using namespace std;
using namespace tup;
using namespace tars;

std::atomic<int> g_requestId;
const string CTX_TARSHASH_KEY = "CTX_TARSHASH_KEY";

//////////////////////////////////////////////////////

int TupBase::_setConnectionTag = 0;

string TupBase::_sEncryptKey;
string TupBase::_sEncryptKeyV2;
//int TupBase::_iMinCompressLen = 512;

//////////////////////////////////////////////////////
TupBase::TupBase() //: _pEncryptBuff(NULL), _iEncryptLength(0)
{
}

TupBase::~TupBase()
{
}

void TupBase::initializeBase()
{
    loadFilterHeader();

    TLOG_DEBUG("initializeBase OK!" << endl);
}

void TupBase::destroyBase()
{

    TLOG_DEBUG("destroyBase OK!" << endl);
}

// 初始化静态配置
int TupBase::initStaticParam(const TC_Config &conf)
{
    _sEncryptKey = "sDf434ol*123+-KD";
    _sEncryptKeyV2 = "lS-0O&62Jl(j%!zM";

    //是否设置回包的http包头Connection值为keep_alive,默认不设置
    _setConnectionTag = TC_Common::strto<int>(conf.get("/config/setConnection/<tag>", "0"));

    return 0;
}

//////////////////////////////////////////////////////

bool TupBase::reloadFilterHeader(const string &command, const string &params, string &result)
{
    TLOG_DEBUG("command:" << command << endl);
    result = loadFilterHeader();
    TLOG_DEBUG("result:" << result << endl);
    return true;
}

string TupBase::loadFilterHeader()
{
    try
    {
        TC_Config conf;
        conf.parseFile(g_app.getConfFile());
        _headers = TC_Common::sepstr<string>(conf.get("/main<filterheaders>", ""), ", ;|");

        TLOG_DEBUG(TC_Common::tostr(_headers) << endl);

        return "reload ok";
    }
    catch (exception &ex)
    {
        TLOG_ERROR("error:" << ex.what() << endl);
        return string("loadFilterHeader config error:") + ex.what();
    }

    return "loadFilterHeader error.";
}

int TupBase::getHttpFilter(const TC_Http::http_header_type &filterHeader, map<string, string> &filters)
{
    vector<string>::iterator it = _headers.begin();
    while (it != _headers.end())
    {
        TC_Http::http_header_type::const_iterator i = filterHeader.find(*it);
        if (i != filterHeader.end())
        {
            filters[i->first] = i->second;
        }
        ++it;
    }

    return 0;
}

/** 
 * \
 * 
 * @param httpRequest
 * @param iZipType
 * @param iEptType
 * 
 * @return int
 */
int TupBase::getReqEncodingFromHeader(const TC_HttpRequest &httpRequest, int &iZipType, int &iEptType)
{
    // HTTP头规则
    //客户端设置：
    //X-S-ZIP：        自定义字段，标识post内容是否采用了gzip压缩。同时告诉服务端可接受压缩的response
    //X-R-ZIP：        自定义字段，告诉服务端可接受压缩的response。请求内容为压缩时可以不设置此头。
    //X-S-Encrypt：    自定义字段，标识post内容是否采用了加密，同时告诉服务端可接受加密的response

    //服务端：
    //X-S-ZIP：        自定义字段，标识post内容是否采用了gzip压缩
    //X-S-Encrypt：    自定义字段，标识response内容是否做了加密。

    // 交互流程
    //客户端发送请求时：
    //1、对准备发送的整个TUP包（对应HTTP包体）进行压缩。对应的需要置HTTP头：X-S-ZIP，标明本内容为压缩,同时可以接收压缩内容。
    //   X-S-ZIP:gzip
    //2、对压缩后的内容进行加密。对应的需要置HTTP头：X-S-Encrypt，标明本内容为加密，同时接收加密内容。
    //   X-S-Encrypt:qencrypt
    //   服务端为TEA算法，java代码可以使用jutil库
    //3、如果发送的内容不需要压缩，则要置HTTP头：X-R-ZIP，单独标明下可以接收压缩内容。
    //   X-R-ZIP:gzip
    //4、最终压缩及加密处理后的内容做为HTTP包体发送到服务端。
    //
    //  内容即要加密又要压缩时，要先压缩再加密。
    //
    //客户端收到应答时：
    //1、检测HTTP应答是否含有X-S-Encrypt头，如果有则对HTTP包体进行解密。
    //   X-S-Encrypt:encrypt1
    //2、检测HTTP应答是否含有X-S-ZIP头，如果有则对解密后的内容进行解压得到最终TUP包内容。如果没有，则1之后的内容就是最终TUP包内容。
    //   X-S-ZIP:gzip
    //
    // 内容即被加密又被压缩时，要先解密再解压。

    iZipType = 0;
    string sHeadList;
    //body内容是否是gzip格式
    std::string sTccEncoding = httpRequest.getHeader("X-S-ZIP");
    if (!sTccEncoding.empty())
    {
        iZipType = 1;

        ReportHelper::reportProperty("HTTPHeader_X-S-ZIP");
        sHeadList += "X-S-ZIP;";
    }

    iEptType = 0;
    //body内容是否是加密格式
    std::string sTccEncrypt = httpRequest.getHeader("X-S-Encrypt");
    if (sTccEncrypt == "encrypt2")
    {
        iEptType = 2;

        ReportHelper::reportProperty("HTTPHeader_QQ-S-Encrypt");
        sHeadList += "X-S-Encrypt;";
    }
    else if (!sTccEncrypt.empty())
    {
        //        if (sTccEncrypt.find("encrypt1") != string::npos)
        iEptType = 1;

        ReportHelper::reportProperty("HTTPHeader_QQ-S-Encrypt");
        sHeadList += "X-S-Encrypt;";
    }

    return 0;
}

int TupBase::setRspEncodingToHeader(const TC_HttpRequest &httpRequest, pair<string, string> &pairAcceptZip, pair<string, string> &pairAcceptEpt)
{
    string sHeadList;
    std::string sTccEncoding1 = httpRequest.getHeader("X-R-ZIP");
    std::string sTccEncoding2 = httpRequest.getHeader("X-S-ZIP");

    if (!sTccEncoding1.empty())
    {
        pairAcceptZip.first = "X-S-ZIP";
        pairAcceptZip.second = "gzip";
        ReportHelper::reportProperty("HTTPHeader_X-R-ZIP");
        sHeadList += "X-R-ZIP;";
    }

    if (!sTccEncoding2.empty())
    {
        pairAcceptZip.first = "X-S-ZIP";
        pairAcceptZip.second = "gzip";
        ReportHelper::reportProperty("HTTPHeader_X-S-ZIP");
        sHeadList += "X-S-ZIP;";
    }

    std::string sTccEncrypt2 = httpRequest.getHeader("X-S-Encrypt");
    if (!sTccEncrypt2.empty())
    {
        pairAcceptEpt.first = "X-S-Encrypt";
        //pairAcceptEpt.second = "encrypt1";
        pairAcceptEpt.second = sTccEncrypt2;
        sHeadList += "X-S-Encrypt;";
    }

    TLOG_DEBUG("Zip.first:" << pairAcceptZip.first
                           << ", Zip.second:" << pairAcceptZip.second
                           << ", Ept.first:" << pairAcceptEpt.first
                           << ", Ept.second:" << pairAcceptEpt.second
                           << ", HeaderList:" << sHeadList << endl);

    return 0;
}

int TupBase::getDataFromHTTPRequest(const TC_HttpRequest &httpRequest, vector<char> &buffer) //, size_t& iOrgReqLen)
{
    //根据Content-Length的长度来确定解包长度
    int contentLength = TC_Common::strto<int>(httpRequest.getHeader("Content-Length"), "0");
    if (contentLength > 0)
    {
        string tmp = httpRequest.getContent().substr(0, contentLength);

        buffer.assign(tmp.c_str(), tmp.c_str() + contentLength);
    }

    if (buffer.size() == 0)
    {
        TLOG_DEBUG("buffer result is empty" << endl);
        ReportHelper::reportStat(g_app.getLocalServerName(), "RequestMonitor", "TupDataEmpyErr", -1);
        return 1;
    }
    else
    {
        return 0;
    }
}

int TupBase::getRealDataByDecode(const shared_ptr<HandleParam> &stParam)
{
    if (!stParam->iEptType && !stParam->iZipType)
    {
        return 0;
    }

    size_t iOrgLen = stParam->buffer.size();

    vector<char> sTempData = stParam->buffer;

    // 解密数据
    if (stParam->iEptType)
    {
    	bool bEpt = TC_Tea::decrypt(stParam->sEncryptKey.c_str(), stParam->buffer.data(), stParam->buffer.size(), sTempData);
        if (!bEpt)
        {
            TLOG_ERROR("http protocol decrypt error, origin len:" << iOrgLen << endl);
            ReportHelper::reportStat(g_app.getLocalServerName(), "RequestMonitor", "DecryptErr", -1);
            return -1;
        }

        TLOG_DEBUG("TeaDecrypt OK, origin len: " << iOrgLen << "-> " << stParam->buffer.size() << endl);

        sTempData.swap(stParam->buffer);
    }

    // 解压缩
    if (stParam->iZipType)
    {
    	bool bGzip = TC_GZip::uncompress(stParam->buffer.data(), stParam->buffer.size(), sTempData);
        if (!bGzip)
        {
        	TLOG_ERROR("http protocol gzip uncompress error, origin len:" << iOrgLen << endl);
            ReportHelper::reportStat(g_app.getLocalServerName(), "RequestMonitor", "UncompressErr", -1);
            return -2;
        }

        stParam->buffer.swap(sTempData);
        TLOG_DEBUG("gzipUncompress OK, origin len: " << iOrgLen << "->" << stParam->buffer.size() << endl);
    }

    return 0;
}

void TupBase::getFilter(const shared_ptr<HandleParam> &stParam)
{
    getHttpFilter(stParam->httpRequest.getHeaders(), stParam->filterHeader);
    stParam->filterHeader["REMOTE_IP"] = stParam->sIP;
    stParam->filterHeader["X-Forwarded-For-Host"] = stParam->sIP + ":" + TC_Common::tostr(stParam->current->getPort());
    if (stParam->filterHeader.find("X-GUID") == stParam->filterHeader.end())
    {
        stParam->filterHeader["X-GUID"] = stParam->sGUID;
    }
}

int TupBase::handleTarsRequest(const shared_ptr<HandleParam> &stParam)
{
    string sErrMsg;

    try
    {
        stParam->current->setResponse(false);
        //解析出所有的tup请求
        shared_ptr<RequestPacket> tupRequest = make_shared<RequestPacket>();

        int ret = -1;
        if (EPT_JSON_PROXY == stParam->proxyType)
        {
            ret = parseJsonRequest(stParam, tupRequest);
        }
        else if (EPT_TUP_PROXY == stParam->proxyType)
        {
            ret = parseTupRequest(stParam, tupRequest);
        }

        if (ret != 0)
        {
            TLOG_ERROR("parseTupRequest error"
                      << ", type:" << stParam->proxyType
                      << ",ret:" << ret
                      << ",length:" << stParam->buffer.size()
                      << ",sGUID:" << stParam->sGUID
                      << endl);

            ReportHelper::reportStat(g_app.getLocalServerName(), "RequestMonitor", "ParseTupBodyErr", -1);
            ProxyUtils::doErrorRsp(400,  stParam->current, stParam->proxyType, stParam->httpKeepAlive);
            return 0;
        }

        //解析代理
        ProxyExInfo pei;
        ServantPrx proxy = TupProxyManager::getInstance()->getProxy(tupRequest->sServantName, tupRequest->sFuncName, stParam->httpRequest, pei);

        if (!proxy)
        {
            TLOG_ERROR("tup request no match proxy error, "
                      << ", sServantName:" << tupRequest->sServantName
                      << ", sFuncName:" << tupRequest->sFuncName
                      << ", sGUID:" << stParam->sGUID
                      << endl);

            TLOG_ERROR("parseTupProxy error, sGUID:" << stParam->sGUID << endl);

            ReportHelper::reportStat(g_app.getLocalServerName(), "RequestMonitor", "GetProxyErr", -1);
            ProxyUtils::doErrorRsp(404,  stParam->current, stParam->proxyType, stParam->httpKeepAlive);
            return 0;
        }
        else
        {
            ReportHelper::reportStat(g_app.getLocalServerName(), "FindProxyMonitor", "TupProxyOK", 0);
        }

        if (!STATIONMNG->checkWhiteList(proxy->tars_name(), stParam->sIP))
        {
            TLOG_ERROR(stParam->sIP << " is not in " << proxy->tars_name() << " 's white list, obj:" << tupRequest->sServantName << ":" << tupRequest->sFuncName << endl);
            ProxyUtils::doErrorRsp(403,  stParam->current, stParam->proxyType, stParam->httpKeepAlive);
            return 0;
        }
        else if (STATIONMNG->isInBlackList(proxy->tars_name(), stParam->sIP))
        {
            TLOG_ERROR(stParam->sIP << " is in " << proxy->tars_name() << " 's black list, obj:" << tupRequest->sServantName << ":" << tupRequest->sFuncName << endl);
            ProxyUtils::doErrorRsp(403,  stParam->current, stParam->proxyType, stParam->httpKeepAlive);
            return 0;
        }
        else if (!FlowControlManager::getInstance()->check(proxy->tars_name()))
        {
            TLOG_ERROR("tars request:" << proxy->tars_name() << " flowcontrol false!!!" << endl);
            ProxyUtils::doErrorRsp(429, stParam->current, stParam->proxyType, stParam->httpKeepAlive);
            return 0;
        }

        getFilter(stParam);

        if (pei.verifyInfo.prx)
        {
            // 处理verify身份鉴权问题 ============
            doVerifyAsync(proxy, stParam, tupRequest, pei);
        }
        else
        {
            callServer(proxy, stParam, tupRequest, pei.hashInfo);
        }

        return 0;
    }
    catch (exception &ex)
    {
        sErrMsg = ex.what();
    }
    catch (...)
    {
        sErrMsg = "unknown error";
    }

    ProxyUtils::doErrorRsp(400, stParam->current, stParam->proxyType, stParam->httpKeepAlive);

    TLOG_ERROR("exception: " << sErrMsg << ",sGUID:" << stParam->sGUID << endl);

    ReportHelper::reportStat(g_app.getLocalServerName(), "RequestMonitor", "ReqException2Num", -1);

    return -99;
}

int TupBase::doVerifyAsync(ServantPrx proxy, shared_ptr<HandleParam> stParam, shared_ptr<RequestPacket> tRequest, const ProxyExInfo &pei)
{
    static atomic<int>  verifyReqID;
    VerifyReq vReq;
    vReq.token = stParam->httpRequest.getHeader(pei.verifyInfo.tokenHeader);
    if (vReq.token.empty())
    {
        TLOG_ERROR("get tokenHeader empty:" << tRequest->sServantName << ":" << tRequest->sFuncName << "|http header:" << pei.verifyInfo.tokenHeader << endl);
        ProxyUtils::doErrorRsp(401, stParam->current, stParam->proxyType, stParam->httpKeepAlive, "Unauthorized: " + pei.verifyInfo.tokenHeader + " is empty.");
        return 0;
    }
    for (auto it = pei.verifyInfo.verifyHeaders.begin(); it != pei.verifyInfo.verifyHeaders.end(); it++)
    {
        vReq.verifyHeaders[*it] = stParam->httpRequest.getHeader(*it);
    }
    if (pei.verifyInfo.verifyBody)
    {
        vReq.body = stParam->buffer;
    }
    
    RequestPacket tReq;
    tReq.iVersion = tars::TARSVERSION;
    tReq.cPacketType = tars::TARSNORMAL;
    tReq.iRequestId = ++verifyReqID;
    tReq.sServantName = string(pei.verifyInfo.prx->tars_name().substr(0, pei.verifyInfo.prx->tars_name().find("@")));
    tReq.sFuncName = "verify";

    tars::TarsOutputStream<tars::BufferWriterVector> os;
    os.write(vReq, 1);
    tReq.sBuffer.assign(os.getBuffer(), os.getBuffer() + os.getLength());

    tars::TarsOutputStream<BufferWriter> buffer;
    tReq.writeTo(buffer);

    unsigned int bufferlength = buffer.getLength() + 4;
    bufferlength = htonl(bufferlength);

    string s;
    s.append((char *)&bufferlength, 4);
    s.append(buffer.getBuffer(), buffer.getLength());

    try
    {
        VerifyCallbackExPtr vcb = new VerifyCallbackEx(proxy, tRequest, stParam, pei.hashInfo, vReq.token);    
        pei.verifyInfo.prx->rpc_call_async(tReq.iRequestId, tReq.sFuncName,  s.c_str(), s.length(), vcb);    
        TLOG_DEBUG("do verify tars_rpc_call_async succ, buff len:" << s.length() << endl);   
    }
    catch(exception& ex)
    {
        TLOG_ERROR("verify exception:" << ex.what() << "|" << tRequest->sServantName << ":" << tRequest->sFuncName << endl);
        ProxyUtils::doErrorRsp(401, stParam->current, stParam->proxyType, stParam->httpKeepAlive, "Unauthorized: veirfy exception.");  
    }    
    return 0;
}

int TupBase::parseJsonRequest(const shared_ptr<HandleParam> &stParam, const shared_ptr<RequestPacket> &tupRequest)
{
    try
    {
//        string buff(stParam->buffer, stParam->buffer + stParam->length);

        tupRequest->iVersion = JSONVERSION;
        tupRequest->cPacketType = tars::TARSNORMAL;

        if (stParam->httpRequest.getRequestUrl().length() > 6)
        {
            vector<string> vs = TC_Common::sepstr<string>(stParam->httpRequest.getRequestUrl(), "/");
            if (vs.size() < 3)
            {
                TLOG_ERROR("parse json url fail:" << stParam->httpRequest.getRequestUrl() << endl);
                return -1;
            }

            tupRequest->iRequestId = 99;
            tupRequest->sServantName = TC_Common::trim(vs[1]);
            tupRequest->sFuncName = TC_Common::trim(vs[2]);
            tupRequest->sBuffer = stParam->buffer;
//            tupRequest->sBuffer.resize(buff.length());
//            tupRequest->sBuffer.assign(buff.begin(), buff.end());
            stParam->isRestful = true;
        }
        else
        {

//            TLOG_DEBUG("buff:" << buff << endl);

            tars::JsonValuePtr p = tars::TC_Json::getValue(stParam->buffer);
            tars::JsonValueObjPtr pObj = tars::JsonValueObjPtr::dynamicCast(p);
            tars::JsonInput::readJson(tupRequest->iMessageType, pObj->value["msgtype"], false);
            tars::JsonInput::readJson(tupRequest->iRequestId, pObj->value["reqid"], false);
            tars::JsonInput::readJson(tupRequest->iTimeout, pObj->value["timeout"], false);
            tars::JsonInput::readJson(tupRequest->context, pObj->value["context"], false);
            tars::JsonInput::readJson(tupRequest->status, pObj->value["status"], false);

            tars::JsonInput::readJson(tupRequest->sServantName, pObj->value["obj"], false);
            tars::JsonInput::readJson(tupRequest->sFuncName, pObj->value["func"], false);

            string data;
            tars::JsonInput::readJson(data, pObj->value["data"], true);
            if (data.empty())
            {
                TLOG_ERROR("parseJsonRequest error, data is empty!!!" << endl);
                return -1;
            }
            tupRequest->sBuffer.resize(data.length());
            tupRequest->sBuffer.assign(data.begin(), data.end());
            stParam->isRestful = false;
        }

        return 0;
    }
    catch (exception &ex)
    {
        TLOG_ERROR("exception:" << ex.what() << endl);
    }

    return -1;
}

int TupBase::parseTupRequest(const shared_ptr<HandleParam> &stParam, const shared_ptr<RequestPacket> &tupRequest)
{
//    const char *buffer = stParam->buffer;
//    size_t length = stParam->length;
    //长度保护
    if (stParam->buffer.size() < sizeof(uint32_t))
    {
    	TLOG_ERROR("tup error: tup packet length < 4, length:" << stParam->buffer.size() << endl);
        return -1;
    }

    uint32_t l = 0;
    memcpy(&l, stParam->buffer.data(), sizeof(uint32_t));
    l = ntohl(l);

    //长度保护
    if (l > stParam->buffer.size())
    {
    	TLOG_ERROR("tup error: " << l << " > " << stParam->buffer.size() << endl);
        return -2;
    }

    if (l <= 4)
    {
    	TLOG_ERROR("tup error: l:" << l << " <= 4, length:" << stParam->buffer.size() << endl);
        return -3;
    }

    try
    {
        tars::TarsInputStream<BufferReader> is;

        is.setBuffer(stParam->buffer.data() + 4, l - 4);

        tupRequest->readFrom(is);

        TLOGDEBUG(tupRequest->sServantName << "::" << tupRequest->sFuncName << ", requestid:" << tupRequest->iRequestId << endl);

        return 0;
    }
    catch(const std::exception& e)
    {
    	TLOGERROR("parseTupRequest:" << e.what() << ", req buffer len:" << stParam->buffer.size() << ", tarslen:" << l << endl);
    }
    return -1;
}

void TupBase::callServer(ServantPrx proxy, const shared_ptr<HandleParam> &stParam, const shared_ptr<RequestPacket> &tupRequest, const THashInfo& hi)
{
    //构造异步回调对象
    shared_ptr<TupCallbackParam> stCbParam = make_shared<TupCallbackParam>();
    stCbParam->iTime = TC_Common::now2ms();
    stCbParam->pairAcceptZip = stParam->pairAcceptZip;
    stCbParam->pairAcceptEpt = stParam->pairAcceptEpt;
    stCbParam->iEptType = stParam->iEptType;
    stCbParam->iZipType = stParam->iZipType;
    stCbParam->sReqGuid = stParam->sGUID;
    stCbParam->sReqXua = stParam->sXUA;
    stCbParam->sReqIP = stParam->sIP;
    stCbParam->sEncryptKey = stParam->sEncryptKey;
    stCbParam->iReqBufferSize = stParam->buffer.size();

    stCbParam->iRequestId = tupRequest->iRequestId;
    stCbParam->sServantName = tupRequest->sServantName;
    stCbParam->sFuncName = tupRequest->sFuncName;
    stCbParam->httpKeepAlive = stParam->httpKeepAlive;
    stCbParam->isRestful = stParam->isRestful;

    TupCallbackPtr cb;
    if (JSONVERSION == tupRequest->iVersion)
    {
        cb = new TupCallback("json", stParam->current, stCbParam, stParam->httpKeepAlive);
    }
    else if (TARSVERSION == tupRequest->iVersion)
    {
        cb = new TupCallback("tars", stParam->current, stCbParam, stParam->httpKeepAlive);
    }
    else
    {
        cb = new TupCallback("tup", stParam->current, stCbParam, stParam->httpKeepAlive);
    }

    //记录日志
    FDLOG("tup") << 0 << "|"
                    << stParam->sIP << "|"
                    << stParam->sGUID << "|"
                    << tupRequest->sServantName << "|"
                    << tupRequest->sFuncName << "|"
                    << stParam->iEptType << "|"
                    << stParam->iZipType << "|"
                    << proxy->tars_name() << "|"
                    << stParam->buffer.size() << "|"
                    << endl;

    string sHttpHeaderValue;
    if (!hi.httpHeadKey.empty())
    {
        sHttpHeaderValue = stParam->httpRequest.getHeader(hi.httpHeadKey);
    }

    //设置需要透传的头部
    tupRequest->context.insert(stParam->filterHeader.begin(), stParam->filterHeader.end());
    // 正常用户
    tupAsyncCall(tupRequest, proxy, cb, hi.type, sHttpHeaderValue);
}

void TupBase::tupAsyncCall(const shared_ptr<RequestPacket> &tup, ServantPrx proxy, TupCallbackPtr cb, THashInfo::E_HASH_TYPE ht, const string &sHttpHeaderValue)
{
    //用自己的requestid, 回来的时候好匹配
    int requestId = (++g_requestId);

    try
    {
        //后续需要修改requestid的值, 因此赋值一个tup
        //设置新的tup requestid
        int oldReqID = tup->iRequestId;
        tup->iRequestId = requestId;

        //设置TARS服务的ServantName
        tup->sServantName = string(proxy->tars_name().substr(0, proxy->tars_name().find("@")));

        //调用链追踪;
        int traceFlag = TraceControl::getInstance()->check(cb->getServantName(), tup->sFuncName);
        if (traceFlag >= 0 && traceFlag <= 15)
        {
            string traceID = genTraceID(cb->getServantName(), tup->sFuncName, ServerConfig::LocalIp, requestId);
            stringstream ss;
            ss << std::hex << traceFlag << "." << TraceControl::getInstance()->getParamMaxLen() << "-" << traceID << "|";
            
            string spanID = TC_UUIDGenerator::getInstance()->genID();
            string traceKey = ss.str() + spanID + "|" + spanID;
            SET_MSG_TYPE(tup->iMessageType, tars::TARSMESSAGETYPETRACE);
            tup->status[ServantProxy::STATUS_TRACE_KEY] = traceKey;
            
            string _trace_param_;
            int _trace_param_flag_ = ServantProxyThreadData::needTraceParam(ServantProxyThreadData::TraceContext::EST_TS, traceKey, tup->sBuffer.size());
            if (ServantProxyThreadData::TraceContext::ENP_NORMAL == _trace_param_flag_)
            {
                if (tup->iVersion == tars::JSONVERSION)
                {
                    _trace_param_.assign(tup->sBuffer.begin(), tup->sBuffer.end());
                }
                else
                {
                    _trace_param_ = "tup-bin";
                }
            }
            else if(ServantProxyThreadData::TraceContext::ENP_OVERMAXLEN == _trace_param_flag_)
            {
                _trace_param_ = "{\"trace_param_over_max_len\":true, \"data_len\":" + TC_Common::tostr(tup->sBuffer.size()) + "}";
            }
            
            TARS_TRACE(traceKey, TRACE_ANNOTATION_TS, ServerConfig::Application + "." + ServerConfig::ServerName, tup->sServantName, tup->sFuncName, 0, _trace_param_, "");
            TLOG_DEBUG("trace===>" << traceKey << ", " << tup->sServantName << ":" << tup->sFuncName << endl);

            cb->setTraceKey(traceKey);
        }
        
        /////end 调用链追踪

        //tup->context.insert(filterHeader.begin(), filterHeader.end());
        //newtup->context = filterHeader;

        //重新编码
        tars::TarsOutputStream<BufferWriter> buffer;

        tup->writeTo(buffer);

        unsigned int bufferlength = buffer.getLength() + 4;
        bufferlength = htonl(bufferlength);

        string s;
        s.append((char *)&bufferlength, 4);
        s.append(buffer.getBuffer(), buffer.getLength());

        cb->setNewRequestId(requestId);

        //异步发送出去(用requestId hash来发送)
        TLOG_DEBUG("tars_name:" << proxy->tars_name() << endl);

        string sHashType;
        if (tup->context.find(CTX_TARSHASH_KEY) != tup->context.end() && tup->context[CTX_TARSHASH_KEY].length() > 0)
        {
            proxy->tars_hash(tars::hash<string>()(tup->context[CTX_TARSHASH_KEY]))->rpc_call_async(requestId, tup->sFuncName, s.c_str(), s.length(), cb);
            sHashType = "context hash, key=" + tup->context[CTX_TARSHASH_KEY];
        }
        else if (ht == THashInfo::EHT_ROBINROUND)
        {
            sHashType = "robin round";
            proxy->rpc_call_async(requestId, tup->sFuncName, s.c_str(), s.length(), cb);
        }
        else if (ht == THashInfo::EHT_REQUESTID)
        {
            sHashType = "requestid hash, reqid=" + TC_Common::tostr(oldReqID);
            proxy->tars_hash(oldReqID)->rpc_call_async(requestId, tup->sFuncName, s.c_str(), s.length(), cb);
        }
        else if (ht == THashInfo::EHT_HTTPHEAD && !sHttpHeaderValue.empty())
        {
            sHashType = "http head hash, head=" + sHttpHeaderValue;
            proxy->tars_hash(tars::hash<string>()(sHttpHeaderValue))->rpc_call_async(requestId, tup->sFuncName, s.c_str(), s.length(), cb);
        }
        else if (ht == THashInfo::EHT_CLIENTIP)
        {
            sHashType = "clientip hash, ip=" + cb->getClientIp();
            proxy->tars_hash(tars::hash<string>()(cb->getClientIp()))->rpc_call_async(requestId, tup->sFuncName, s.c_str(), s.length(), cb);
        }
        else
        {
            sHashType = "default hash";
            proxy->rpc_call_async(requestId, tup->sFuncName, s.c_str(), s.length(), cb);
        }

        TLOG_DEBUG(tup->sServantName << "::" << tup->sFuncName << ", version:" << (int)tup->iVersion << ", hash:" << sHashType << ", async call succ." << endl);

        FDLOG("call") << tup->sServantName + "::" + tup->sFuncName << "|" << sHttpHeaderValue
                      << "|iRequestId:" << tup->iRequestId
                      << "|oldReqID:" << oldReqID
                      << "|type:" << (int)(tup->iVersion)
                      << "|buffer.size:" << s.length()
                      << "|HashType:" << sHashType
                      << "|ServantName:" << proxy->tars_name()
                      << endl;
    }
    catch (exception &ex)
    {
        TLOG_ERROR("tupAsyncCall error:" << ex.what() << endl);
    }
}

string TupBase::genTraceID(const string& servantName, const string& funcName, const string& host, int reqID)
{
    stringstream ss;
    ss << servantName << "|" << funcName << "|" << host << "|" << reqID << "|" << TNOW;
    return TC_MD5::md5str(ss.str());
}

//////////////////////////////////////////////////////

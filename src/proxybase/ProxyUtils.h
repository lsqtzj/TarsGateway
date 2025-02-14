#ifndef _PROXYUTILS_H_
#define _PROXYUTILS_H_

#include "servant/Application.h"

using namespace tars;

class ProxyUtils
{
public:

    // 统一错误返回处理
    static string getHttpErrorRsp(int statusCode, E_PROXY_TYPE type, bool keepAlive, const string& rspInfo = "")
    {
        TC_HttpResponse httpRsp;
        string content;
        string info;
        if (400 == statusCode)
        {
            info = "Bad Request";
            content = "<html> <head><title>400 Bad Request</title></head> <body> <center><h1>400 Bad Request</h1></center> </body> </html>";
        }
        else if (401 == statusCode)
        {
            info = "Unauthorized";
            if (!rspInfo.empty())
            {
                content = "<html> <head><title>401 Unauthorized</title></head> <body> <center><h1>" + rspInfo + "</h1></center> </body> </html>"; 
            }
            else
            {
                content = "<html> <head><title>401 Unauthorized</title></head> <body> <center><h1>401 Unauthorized</h1></center> </body> </html>"; 
            }           }
        else if (403 == statusCode)
        {
            info = "Forbidden";
            content = "<html> <head><title>403 Forbidden</title></head> <body> <center><h1>403 Forbidden</h1></center> </body> </html>"; 
        }
        else if (404 == statusCode)
        {
            info = "Not Found";
            content = "<html> <head><title>404 Not Found</title></head> <body> <center><h1>404 Not Found</h1></center> </body> </html>";
        }
        else if (429 == statusCode)
        {
            info = "Too Many Request";
            content = "<html> <head><title>429 Too Many Request</title></head> <body> <center><h1>429 Too Many Request</h1></center> </body> </html>";
        }
        else if (500 == statusCode)
        {
            info = "Server Interval Error";
            content = content = "<html> <head><title>500 Server Interval Error</title></head> <body> <center><h1>500 Server Interval Error</h1></center> </body> </html>";
        }
        else if (501 == statusCode)
        {
            info = "Not Implemented";
            content = "<html> <head><title>501 Not Implemented</title></head> <body> <center><h1>501 Not Implemented</h1></center> </body> </html>";
        }
        else if (502 == statusCode)
        {
            info = "Bad Gateway";
            content = "<html> <head><title>502 Bad Gateway</title></head> <body> <center><h1>502 Bad Gateway</h1></center> </body> </html>";
        }
        else if(503 == statusCode)
        {
            info = "Service Unavailable";
            content = "<html> <head><title>503 Service Unavailable</title></head> <body> <center><h1>503 Service Unavailable</h1></center> </body> </html>";
        }
        else if(504 == statusCode)
        {
            info = "Gateway Timeout";
            content = "<html> <head><title>504 Gateway Timeout</title></head> <body> <center><h1>504 Gateway Timeout</h1></center> </body> </html>"; 
        }
        else
        {
            info = "Server Interval Error";
            content = "<html> <head><title>Server Interval Error</title></head> <body> <center><h1>Server Interval Error</h1></center> </body> </html>";
        }
        
        if (keepAlive)
        {
            httpRsp.setConnection("keep-alive");
        }
        else
        {
            httpRsp.setConnection("close");
        }

        httpRsp.setResponse(statusCode, info, content);
        g_app.setRspHeaders((int)type, "", httpRsp);

        return httpRsp.encode();
    }

    static void doErrorRsp(int statusCode, tars::TarsCurrentPtr current, E_PROXY_TYPE type, bool keepAlive = false, const string& rspInfo = "")
    {
        string data = getHttpErrorRsp(statusCode, type, keepAlive, rspInfo);
        current->sendResponse(data.c_str(), data.length());

        if (!keepAlive)
        {
            current->close();
        }

        TLOG_DEBUG(statusCode << "|" << data << endl);
    }

    static void doOptionsRsp(tars::TarsCurrentPtr current, bool keepAlive = false)
    {
        TC_HttpResponse httpRsp;
        httpRsp.setResponse(200, "OK", "<html> <head><title>200 OK</title></head> <body> <center><h1>200 OK</h1></center> </body> </html>");
        g_app.setRspHeaders((int)EPT_OPTIONS_REQ, "", httpRsp);
        string data =  httpRsp.encode();
        current->sendResponse(data.c_str(), data.length());
        if (!keepAlive)
        {
            current->close();
        }
    }

};


#endif
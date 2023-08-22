/*
 * @Author       : mark
 * @Date         : 2020-06-27
 * @copyleft Apache 2.0
 */
#include "httpresponse.h"

using namespace std;

const unordered_map<string, string> HttpResponse::SUFFIX_TYPE = {
    {".html", "text/html"},
    {".xml", "text/xml"},
    {".xhtml", "application/xhtml+xml"},
    {".txt", "text/plain"},
    {".rtf", "application/rtf"},
    {".pdf", "application/pdf"},
    {".word", "application/msword"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".au", "audio/basic"},
    {".mpeg", "video/mpeg"},
    {".mpg", "video/mpeg"},
    {".avi", "video/x-msvideo"},
    {".gz", "application/x-gzip"},
    {".tar", "application/x-tar"},
    {".css", "text/css "},
    {".js", "text/javascript "},
};

const unordered_map<int, string> HttpResponse::CODE_STATUS = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
};

const unordered_map<int, string> HttpResponse::CODE_PATH = {
    {400, "/400.html"},
    {403, "/403.html"},
    {404, "/404.html"},
};

HttpResponse::HttpResponse()
{
    code_ = -1;
    reqType_ = -1;
    reqRes_ = "";
    isKeepAlive_ = false;
    mmFile_ = nullptr;
    mmFileStat_ = {0};
};

HttpResponse::~HttpResponse()
{
    UnmapFile();
}

void HttpResponse::Init(int reqType, string &reqRes, bool isKeepAlive, int code)
{
    if (mmFile_)
    {
        UnmapFile();
    }
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    reqType_ = reqType;
    reqRes_ = reqRes;
    mmFile_ = nullptr;
    mmFileStat_ = {0};
}

void HttpResponse::MakeResponse(Buffer &buff)
{
    // 判断请求的资源类型
    if (reqType_ == HttpRequest::GET_HTML || reqType_ == HttpRequest::GET_FILE)
    {
        /* 判断请求的资源文件 */
        if (reqRes_.empty() || stat((reqRes_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode))
        {
            code_ = 404;
        }
        else if (!(mmFileStat_.st_mode & S_IROTH))
        {
            code_ = 403;
        }
        else if (code_ == -1)
        {
            code_ = 200;
        }
    }
    else if (reqType_ == HttpRequest::GET_INFO)
    {
        code_ = 200;
    }

    ErrorHtml_();
    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}

char *HttpResponse::File()
{
    return mmFile_;
}

size_t HttpResponse::FileLen() const
{
    return mmFileStat_.st_size;
}

void HttpResponse::ErrorHtml_()
{
    if (CODE_PATH.count(code_))
    {
        reqRes_ = CODE_PATH.find(code_)->second;
        stat((reqRes_).data(), &mmFileStat_);
    }
}

void HttpResponse::AddStateLine_(Buffer &buff)
{
    string status;
    if (CODE_STATUS.count(code_))
    {
        status = CODE_STATUS.find(code_)->second;
    }
    else
    {
        code_ = 400;
        status = CODE_STATUS.find(400)->second;
    }
    buff.Append("HTTP/1.1 " + to_string(code_) + " " + status + "\r\n");
}

void HttpResponse::AddHeader_(Buffer &buff)
{
    buff.Append("Connection: ");
    if (isKeepAlive_)
    {
        buff.Append("keep-alive\r\n");
        buff.Append("keep-alive: max=6, timeout=120\r\n");
    }
    else
    {
        buff.Append("close\r\n");
    }

    if (reqType_ == HttpRequest::GET_HTML)
    {
        buff.Append("Content-Type: " + GetFileType_() + "\r\n");
    }
    else if (reqType_ == HttpRequest::GET_FILE)
    {
        buff.Append("Content-Type: " + GetFileType_() + "\r\n");
        // Set the Content-Disposition header to specify the file name for download
        string fileName = reqRes_.substr(reqRes_.find_last_of('/') + 1);
        buff.Append("Content-Disposition: attachment; filename=\"" + fileName + "\"\r\n");
    }
    else if (reqType_ == HttpRequest::GET_INFO)
    {
        buff.Append("Content-Type: application/json\r\n");
    }
}

void HttpResponse::AddContent_(Buffer &buff)
{
    if (reqType_ == HttpRequest::GET_HTML || reqType_ == HttpRequest::GET_FILE)
    {
        int srcFd = open((reqRes_).data(), O_RDONLY);
        if (srcFd < 0)
        {
            ErrorContent(buff, "File Not Found!");
            return;
        }

        /* 将文件映射到内存提高文件的访问速度
            MAP_PRIVATE 建立一个写入时拷贝的私有映射*/
        LOG_DEBUG("file path %s", (reqRes_).data());
        int *mmRet = (int *)mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
        if (*mmRet == -1)
        {
            ErrorContent(buff, "File Not Found!");
            return;
        }
        mmFile_ = (char *)mmRet;
        close(srcFd);
        buff.Append("Content-Length: " + to_string(mmFileStat_.st_size) + "\r\n\r\n");
    }
    else if (reqType_ == HttpRequest::GET_INFO)
    {
        buff.Append("Content-Length: " + to_string(reqRes_.size()) + "\r\n\r\n");
        buff.Append(reqRes_);
    }
}

void HttpResponse::UnmapFile()
{
    if (mmFile_)
    {
        munmap(mmFile_, mmFileStat_.st_size);
        mmFile_ = nullptr;
    }
}

string HttpResponse::GetFileType_()
{
    /* 判断文件类型 */
    string::size_type idx = reqRes_.find_last_of('.');
    if (idx == string::npos)
    {
        return "application/octet-stream";
    }
    string suffix = reqRes_.substr(idx);
    transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower); // Convert to lowercase
    if (SUFFIX_TYPE.count(suffix))
    {
        return SUFFIX_TYPE.find(suffix)->second;
    }
    return "application/octet-stream";
}

void HttpResponse::ErrorContent(Buffer &buff, string message)
{
    string body;
    string status;
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    if (CODE_STATUS.count(code_))
    {
        status = CODE_STATUS.find(code_)->second;
    }
    else
    {
        status = "Bad Request";
    }
    body += to_string(code_) + " : " + status + "\n";
    body += "<p>" + message + "</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";

    buff.Append("Content-Length: " + to_string(body.size()) + "\r\n\r\n");
    buff.Append(body);
}

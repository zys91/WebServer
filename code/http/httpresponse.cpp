/*
 * @Author       : mark
 * @Date         : 2020-06-27
 * @copyleft Apache 2.0
 */
#include "httpresponse.h"

#include <fcntl.h>    // open
#include <unistd.h>   // close
#include <sys/mman.h> // mmap, munmap

#include "log/log.h"

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
    isKeepAlive_ = false;
    reqType_ = HttpRequest::GET_HTML;
    reqRes_ = "";
    authState_ = HttpRequest::AUTH_NONE;
    authInfo_ = "";
    transMethod_ = NONE;
    FileFd_ = -1;
    FilePtr_ = nullptr;
    FileStat_ = {0};
};

HttpResponse::~HttpResponse()
{
    UnmapFile();
    CloseFile();
}

void HttpResponse::Init(HttpRequest::REQ_TYPE reqType, string &reqRes, HttpRequest::AUTH_STATE authState, string &authInfo, string &resDir, bool isKeepAlive, int code)
{
    UnmapFile();
    CloseFile();
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    resDir_ = resDir;
    reqType_ = reqType;
    reqRes_ = reqRes;
    authState_ = authState;
    authInfo_ = authInfo;
    transMethod_ = NONE;
    FileFd_ = -1;
    FilePtr_ = nullptr;
    FileStat_ = {0};
}

void HttpResponse::MakeResponse(Buffer &buff)
{
    if (code_ == 200)
    {
        // 判断请求的资源类型
        if (reqType_ == HttpRequest::GET_HTML || reqType_ == HttpRequest::GET_FILE)
        {
            /* 判断请求的资源文件 */
            if (reqRes_.empty() || stat((reqRes_).data(), &FileStat_) < 0 || S_ISDIR(FileStat_.st_mode))
            {
                code_ = 404;
            }
            else if (!(FileStat_.st_mode & S_IROTH))
            {
                code_ = 403;
            }
        }
    }

    ErrorHtml_();
    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}

HttpResponse::TransMethod HttpResponse::FileTransMethod() const
{
    return transMethod_;
}

int HttpResponse::FileFd()
{
    return FileFd_;
}

char *HttpResponse::FilePtr()
{
    return FilePtr_;
}

size_t HttpResponse::FileLen() const
{
    return FileStat_.st_size;
}

void HttpResponse::ErrorHtml_()
{
    if (CODE_PATH.count(code_))
    {
        reqRes_ = resDir_ + CODE_PATH.find(code_)->second;
        stat((reqRes_).data(), &FileStat_);
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

    if (authState_ == HttpRequest::AUTH_SET)
    {
        buff.Append("Set-Cookie: " + authInfo_ + "\r\n");
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
    if (reqType_ == HttpRequest::GET_HTML)
    {
        // using mmap
        transMethod_ = MMAP;
        int srcFd = open((reqRes_).data(), O_RDONLY);
        if (srcFd < 0)
        {
            ErrorContent(buff, "File Not Found!");
            return;
        }

        /* 将文件映射到内存提高文件的访问速度
            MAP_PRIVATE 建立一个写入时拷贝的私有映射*/
        LOG_DEBUG("file path %s", (reqRes_).data());
        int *mmRet = (int *)mmap(0, FileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
        if (*mmRet == -1)
        {
            ErrorContent(buff, "File Not Found!");
            return;
        }
        FilePtr_ = (char *)mmRet;
        close(srcFd);
        buff.Append("Content-Length: " + to_string(FileStat_.st_size) + "\r\n\r\n");
    }
    else if (reqType_ == HttpRequest::GET_FILE)
    {
        // using sendfile
        transMethod_ = SENDFILE;
        int srcFd = open((reqRes_).data(), O_RDONLY);
        if (srcFd < 0)
        {
            ErrorContent(buff, "File Not Found!");
            return;
        }
        FileFd_ = srcFd;
        buff.Append("Content-Length: " + to_string(FileStat_.st_size) + "\r\n\r\n");
    }
    else if (reqType_ == HttpRequest::GET_INFO)
    {
        transMethod_ = NONE;
        buff.Append("Content-Length: " + to_string(reqRes_.size()) + "\r\n\r\n");
        buff.Append(reqRes_);
    }
}

void HttpResponse::UnmapFile()
{
    if (FilePtr_)
    {
        munmap(FilePtr_, FileStat_.st_size);
        FilePtr_ = nullptr;
    }
}

void HttpResponse::CloseFile()
{
    if (FileFd_ != -1)
    {
        close(FileFd_);
        FileFd_ = -1;
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

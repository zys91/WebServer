/*
 * @Author       : mark
 * @Date         : 2020-06-25
 * @copyleft Apache 2.0
 */
#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <unordered_map>
#include <string>
#include <sys/stat.h> // stat

#include "buffer/buffer.h"
#include "http/httprequest.h"

class HttpResponse
{
public:
    HttpResponse();
    ~HttpResponse();

    enum TransMethod
    {
        NONE = 0,
        MMAP,
        SENDFILE,
    };

    void Init(HttpRequest::REQ_TYPE reqType, std::string &reqRes, HttpRequest::AUTH_STATE authState, std::string &authInfo, std::string &resDir, bool isKeepAlive = false, int code = -1);
    void MakeResponse(Buffer &buff);
    void UnmapFile();
    void CloseFile();

    char *FilePtr();
    int FileFd();
    TransMethod FileTransMethod() const;
    size_t FileLen() const;
    void ErrorContent(Buffer &buff, std::string message);
    int Code() const { return code_; }

private:
    void AddStateLine_(Buffer &buff);
    void AddHeader_(Buffer &buff);
    void AddContent_(Buffer &buff);

    void ErrorHtml_();
    std::string GetFileType_();

    int code_;
    bool isKeepAlive_;
    std::string resDir_;

    HttpRequest::REQ_TYPE reqType_;
    std::string reqRes_;
    HttpRequest::AUTH_STATE authState_;
    std::string authInfo_;

    TransMethod transMethod_;
    char *FilePtr_; // mmap
    int FileFd_;    // sendfile
    struct stat FileStat_;

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;
    static const std::unordered_map<int, std::string> CODE_STATUS;
    static const std::unordered_map<int, std::string> CODE_PATH;
};

#endif // HTTP_RESPONSE_H
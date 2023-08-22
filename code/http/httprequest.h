/*
 * @Author       : mark
 * @Date         : 2020-06-25
 * @copyleft Apache 2.0
 */
#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>

#include "json/json.hpp"
#include "buffer/buffer.h"

class HttpRequest
{
public:
    enum PARSE_STATE
    {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,
    };

    enum HTTP_CODE
    {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURSE,
        FORBIDDENT_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };

    enum REQ_TYPE
    {
        GET_HTML = 0,
        GET_FILE,
        GET_INFO, // JSON
    };

    HttpRequest();
    ~HttpRequest() = default;

    void Init(const std::string &resDir, const std::string &dataDir);
    HTTP_CODE parse(Buffer &buff);
    PARSE_STATE State() const;

    std::string path() const;
    std::string method() const;
    std::string version() const;
    std::string GetBody(const std::string &key) const;
    std::string GetBody(const char *key) const;
    std::string reqRes() const;
    std::string &reqRes();
    int reqType() const;

    bool IsKeepAlive() const;

private:
    bool ParseRequestLine_(const std::string &line);
    void ParseHeader_(const std::string &line);
    void ParseBody_(const std::string &line);

    void ParsePath_();
    void ParseQuery_();
    void ParseGet_();
    void ParsePost_();
    void ParseUrlencodedData_(std::string &data, std::unordered_map<std::string, std::string> &paramMap);
    void ParseMultipartFormData_(const std::string &data, const std::string &boundary);
    std::string GetBoundaryFromContentType_(const std::string &contentType);
    void ParsePartHeaders_(const std::string &headers, std::map<std::string, std::string> &headerMap);
    void ParseHeaderValueParams_(const std::string &value, std::map<std::string, std::string> &paramMap);
    void ProcessFormField_(const std::string &paramName, const std::string &fieldValue);
    void SaveFileUpload_(const std::string &filename, const std::string &content);
    std::vector<std::string> SplitString_(const std::string &str, const std::string &delimiter);
    std::string TrimString_(const std::string &str);
    void ParseJsonData_(const std::string &jsonData, nlohmann::json &jsonObject);

    static void GetFileList(const std::string &path, nlohmann::json &jsonObject);
    static bool DeleteFile(const std::string &path);
    static bool UserVerify(const std::string &name, const std::string &pwd, bool isLogin);

    std::string resDir_;
    std::string dataDir_;
    PARSE_STATE state_;
    std::string method_, url_, path_, query_, version_, body_;
    std::unordered_map<std::string, std::string> header_;
    std::unordered_map<std::string, std::string> queryRes_;
    std::unordered_map<std::string, std::string> bodyRes_;

    int reqType_;
    std::string reqRes_;

    static const std::unordered_set<std::string> DEFAULT_HTML;
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;
    static const std::unordered_map<std::string, int> SPECIAL_PATH_TAG;
};

#endif // HTTP_REQUEST_H
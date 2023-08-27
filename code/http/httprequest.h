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

    enum LINE_STATE
    {
        LINE_OK,
        LINE_ERROR,
        LINE_OPEN,
    };

    enum HTTP_METHOD
    {
        METHOD_UNKNOWN,
        GET,
        POST,
        HEAD,
        PUT,
        DELETE,
        CONNECT,
        OPTIONS,
        TRACE,
        PATCH,
    };

    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,        // 200
        BAD_REQUEST,        // 400
        UNAUTH_REQUEST,     // 401
        FORBIDDENT_REQUEST, // 403
        INTERNAL_ERROR,     // 500
    };

    enum REQ_TYPE
    {
        GET_HTML,
        GET_FILE,
        GET_INFO, // JSON
    };

    enum AUTH_STATE
    {
        AUTH_ANON,
        AUTH_NEED,
        AUTH_SET,
        AUTH_PASS,
        AUTH_FAIL,
    };

    HttpRequest();
    ~HttpRequest() = default;

    void Init(const std::string &resDir, const std::string &dataDir);
    HTTP_CODE parse(Buffer &buff);
    PARSE_STATE State() const;

    std::string path() const;
    HTTP_METHOD method() const;
    std::string version() const;
    std::string GetBody(const std::string &key) const;
    std::string GetBody(const char *key) const;
    REQ_TYPE reqType() const;
    std::string reqRes() const;
    std::string &reqRes();
    AUTH_STATE authState() const;
    std::string authInfo() const;
    std::string &authInfo();

    bool IsKeepAlive() const;

private:
    LINE_STATE ParseLine_(Buffer &buff, std::string &line);
    bool ParseRequestLine_(const std::string &line);
    void ParseHeader_(const std::string &line);
    void ParsePath_();
    void ParseMethod_(const std::string &methodStr);
    void ParseQuery_();
    void ParseCookies_(const std::string &cookieStr);
    void CheckCookie_();
    void ParseRequest_(const std::string &line);
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
    static bool UserVerify(const std::string &uid, std::string &userInfo);
    static bool UserEnroll(const std::string &userInfo, std::string &cookie);
    static bool UserQuit(const std::string &uid);
    static std::string GenerateRandomID();

    std::string resDir_, dataDir_;
    PARSE_STATE state_;
    HTTP_METHOD method_;
    std::string url_, path_, query_, version_, body_;
    std::unordered_map<std::string, std::string> header_, cookies_, queryRes_, bodyRes_;

    REQ_TYPE reqType_;
    std::string reqRes_;
    AUTH_STATE authState_;
    std::string authInfo_;
    std::string userInfo_; // 此处简化用户信息为username

    static const std::unordered_map<std::string, HTTP_METHOD> HTTP_METHOD_MAP;
    static const std::unordered_set<std::string> DEFAULT_HTML;
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG, SPECIAL_PATH_TAG;
};

/*HTTP Status Code
1xx - Informational:
100 Continue
101 Switching Protocols
102 Processing

2xx - Success:
200 OK
201 Created
202 Accepted
204 No Content
206 Partial Content

3xx - Redirection:
300 Multiple Choices
301 Moved Permanently
302 Found (Moved Temporarily)
304 Not Modified
307 Temporary Redirect
308 Permanent Redirect

4xx - Client Errors:
400 Bad Request
401 Unauthorized
403 Forbidden
404 Not Found
405 Method Not Allowed
406 Not Acceptable
409 Conflict
410 Gone
429 Too Many Requests

5xx - Server Errors:
500 Internal Server Error
501 Not Implemented
502 Bad Gateway
503 Service Unavailable
504 Gateway Timeout
505 HTTP Version Not Supported
*/

#endif // HTTP_REQUEST_H
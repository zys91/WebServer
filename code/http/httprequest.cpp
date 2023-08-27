/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */
#include "httprequest.h"

#include <fstream>
#include <random>
#include <regex>
#include <dirent.h>
#include <sys/stat.h>
#include <mysql/mysql.h> //mysql

#include "log/log.h"
#include "pool/connpool.h"
#include "pool/connRAII.h"

using namespace std;

const unordered_map<string, HttpRequest::HTTP_METHOD> HttpRequest::HTTP_METHOD_MAP = {
    {"GET", GET},
    {"POST", POST},
    {"HEAD", HEAD},
    {"PUT", PUT},
    {"DELETE", DELETE},
    {"CONNECT", CONNECT},
    {"OPTIONS", OPTIONS},
    {"TRACE", TRACE},
    {"PATCH", PATCH}};

const unordered_set<string> HttpRequest::DEFAULT_HTML{
    "/index",
    "/picture",
    "/video",
    "/file",
    "/user",
};

const unordered_map<string, int> HttpRequest::DEFAULT_HTML_TAG{
    {"/file.html", 0},
    {"/user.html", 1},
};

// 需与下面处理业务逻辑的TAG对应
const unordered_map<string, int> HttpRequest::SPECIAL_PATH_TAG{
    {"/fileslist", 0}, // get
    {"/upload", 1},    // post
    {"/download", 2},  // get
    {"/delete", 3},    // post
    {"/register", 4},  // post
    {"/login", 5},     // post
    {"/userinfo", 6},  // get
    {"/logout", 7},    // get
};

HttpRequest::HttpRequest()
{
    url_ = path_ = query_ = version_ = body_ = "";
    method_ = METHOD_UNKNOWN;
    state_ = REQUEST_LINE;
    reqType_ = GET_HTML;
    reqRes_ = "";
    authState_ = AUTH_ANON;
    authInfo_ = "";
    userInfo_ = "";
}

void HttpRequest::Init(const string &resDir, const string &dataDir)
{
    url_ = path_ = query_ = version_ = body_ = "";
    method_ = METHOD_UNKNOWN;
    state_ = REQUEST_LINE;
    reqType_ = GET_HTML;
    reqRes_ = "";
    authState_ = AUTH_ANON;
    authInfo_ = "";
    userInfo_ = "";
    resDir_ = resDir;
    dataDir_ = dataDir;
    header_.clear();
    cookies_.clear();
    queryRes_.clear();
    bodyRes_.clear();
}

bool HttpRequest::IsKeepAlive() const
{
    if (header_.count("Connection") == 1)
    {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

HttpRequest::LINE_STATE HttpRequest::ParseLine_(Buffer &buff, string &line)
{
    const char CRLF[] = "\r\n";
    if (state_ == BODY) // BODY部分特殊处理
    {
        if (method_ == GET)
        {
            LOG_DEBUG("GET method, no body");
            line = "";
            buff.RetrieveAll();
            return LINE_OK;
        }
        else if (method_ == POST)
        {
            size_t contentLen;
            size_t maxAllowContentLength = 1024 * 1024 * 1024; // 1GB
            LOG_DEBUG("POST method, has body");
            if (header_.count("Content-Length") == 0)
            {
                LOG_ERROR("POST method, no Content-Length");
                return LINE_ERROR;
            }

            try
            {
                contentLen = std::stoul(header_["Content-Length"]); // 获取body长度
                if (contentLen > maxAllowContentLength)
                {
                    LOG_ERROR("POST method, Content-Length too long");
                    return LINE_ERROR;
                }
            }
            catch (const out_of_range &e)
            {
                // Handle the case where the string couldn't be converted to size_t
                LOG_ERROR("POST method, Content-Length too long");
                return LINE_ERROR;
            }

            // multipart/form-data处理方式：分段分批读取，节约内存占用
            if (header_["Content-Type"] == "multipart/form-data")
            {
                // TODO: 分段分批读取
            }

            // 其他Content-Type默认处理方式：按contentLen一次性读取
            if (buff.ReadableBytes() < contentLen)
            {
                return LINE_OPEN;
            }
            line = string(buff.Peek(), buff.Peek() + contentLen);
            buff.RetrieveAll();
            return LINE_OK;
        }
        // TODO: 支持其他Method
    }

    // REQUEST_LINE和HEADERS部分按行处理
    // lineEnd 为寻找到第一个/r/n所在的位置
    const char *lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
    // 未找到下一个CRLF
    if (lineEnd == buff.BeginWriteConst())
    {
        return LINE_OPEN;
    }
    line = string(buff.Peek(), lineEnd); // 提取一行，不含CRLF
    buff.RetrieveUntil(lineEnd + 2);     // 将已经提取的数据从缓冲区中取出
    return LINE_OK;
}

HttpRequest::HTTP_CODE HttpRequest::parse(Buffer &buff)
{
    string line;
    LINE_STATE lineState = LINE_OK;
    while (((lineState = ParseLine_(buff, line)) == LINE_OK && state_ != FINISH))
    {
        switch (state_)
        {
        case REQUEST_LINE:
            if (!ParseRequestLine_(line))
            {
                return BAD_REQUEST;
            }
            break;
        case HEADERS:
            ParseHeader_(line);
            break;
        case BODY:
            ParseRequest_(line);
            if (authState_ == AUTH_FAIL)
            {
                return FORBIDDENT_REQUEST;
            }
            else if (authState_ == AUTH_NEED)
            {
                return UNAUTH_REQUEST;
            }
            return GET_REQUEST;
            break;
        default:
            break;
        }
    }

    if (lineState == LINE_ERROR)
    {
        return BAD_REQUEST;
    }

    // 默认HttpRequest处理
    return NO_REQUEST;
}

void HttpRequest::ParsePath_()
{
    if (path_ == "/")
    {
        path_ = "/index.html";
    }
    else
    {
        for (auto &item : DEFAULT_HTML)
        {
            if (item == path_)
            {
                path_ += ".html";
                break;
            }
        }
    }
}

void HttpRequest::ParseQuery_()
{
    ParseUrlencodedData_(query_, queryRes_);
}

void HttpRequest::ParseMethod_(const string &methodStr)
{
    auto it = HTTP_METHOD_MAP.find(methodStr);
    if (it != HTTP_METHOD_MAP.end())
    {
        method_ = it->second;
    }
    else
    {
        method_ = METHOD_UNKNOWN;
    }
}

bool HttpRequest::ParseRequestLine_(const string &line)
{
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if (regex_match(line, subMatch, patten))
    {
        string methodStr = subMatch[1];
        ParseMethod_(methodStr);
        url_ = subMatch[2];
        version_ = subMatch[3];
        // Parse URL to extract path and query
        size_t queryStartPos = url_.find('?');
        if (queryStartPos != string::npos)
        {
            path_ = url_.substr(0, queryStartPos);
            query_ = url_.substr(queryStartPos + 1);
            ParsePath_();
            ParseQuery_();
        }
        else
        {
            path_ = url_;
            query_ = "";
            ParsePath_();
        }
        LOG_DEBUG("[%s], [%s], [%s], [%s]", methodStr.c_str(), path_.c_str(), query_.c_str(), version_.c_str());
        state_ = HEADERS;
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

void HttpRequest::ParseHeader_(const string &line)
{
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if (regex_match(line, subMatch, patten))
    {
        header_[subMatch[1]] = subMatch[2];
        if (subMatch[1] == "Cookie")
        {
            ParseCookies_(subMatch[2]);
        }
    }
    else
    {
        state_ = BODY;
    }
}

void HttpRequest::ParseCookies_(const string &cookieStr)
{
    regex cookiePattern("([^;=]+)=([^;]*)");
    smatch cookieMatch;

    auto cookieIter = sregex_iterator(cookieStr.begin(), cookieStr.end(), cookiePattern);
    auto cookieEnd = sregex_iterator();

    for (; cookieIter != cookieEnd; ++cookieIter)
    {
        string key = (*cookieIter)[1];
        string value = (*cookieIter)[2];
        // Trim leading and trailing spaces
        key.erase(0, key.find_first_not_of(" "));
        key.erase(key.find_last_not_of(" ") + 1);
        value.erase(0, value.find_first_not_of(" "));
        value.erase(value.find_last_not_of(" ") + 1);
        cookies_[key] = value;
    }
}

void HttpRequest::CheckCookie_()
{
    if (cookies_.count("session_id"))
    {
        if (UserVerify(cookies_["session_id"], userInfo_))
            authState_ = AUTH_PASS;
        else
            authState_ = AUTH_FAIL;
    }
    else
    {
        authState_ = AUTH_NEED;
    }
}

void HttpRequest::ParseRequest_(const string &line)
{
    body_ = line;
    // LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
    if (method_ == GET)
    {
        ParseGet_();
    }
    else if (method_ == POST)
    {
        ParsePost_();
    }
    // TODO: 支持其他Method
    state_ = FINISH;
}

void HttpRequest::ParseGet_()
{
    if (SPECIAL_PATH_TAG.count(path_))
    {
        nlohmann::json reqRes;
        int tag = SPECIAL_PATH_TAG.find(path_)->second;
        LOG_DEBUG("Tag:%d", tag);

        CheckCookie_();
        if (authState_ != AUTH_PASS)
        {
            return;
        }

        if (tag == 0) // fileslist
        {
            GetFileList(dataDir_ + "/" + userInfo_ + "/", reqRes);
            reqType_ = GET_INFO;
            reqRes_ = reqRes.dump();
            return;
        }
        else if (tag == 2 && queryRes_.count("file")) // download?file=${fileName}
        {
            reqType_ = GET_FILE;
            reqRes_ = dataDir_ + "/" + userInfo_ + "/" + queryRes_["file"];
            return;
        }
        else if (tag == 6) // userinfo
        {
            reqType_ = GET_INFO;
            reqRes["username"] = userInfo_;
            reqRes_ = reqRes.dump();
            return;
        }
        else if (tag == 7) // logout
        {
            if (UserQuit(cookies_["session_id"]))
            {
                path_ = "/user.html";
                authState_ = AUTH_SET;
                authInfo_ = "session_id=" + cookies_["session_id"] + "; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/; HttpOnly";
            }
            else
            {
                path_ = "/error.html";
            }
            reqType_ = GET_HTML;
            reqRes_ = resDir_ + path_;
            return;
        }
    }
    else if (DEFAULT_HTML_TAG.count(path_))
    {
        int tag = DEFAULT_HTML_TAG.find(path_)->second;
        LOG_DEBUG("Tag:%d", tag);

        CheckCookie_();
        if (authState_ != AUTH_PASS)
        {
            if (tag == 1) // user.html
            {
                authState_ = AUTH_ANON; // user页面不存在鉴权失败的情况
                reqType_ = GET_HTML;
                reqRes_ = resDir_ + path_;
            }
            return;
        }

        if (tag == 1) // user.html
        {
            path_ = "/welcome.html";
            reqType_ = GET_HTML;
            reqRes_ = resDir_ + path_;
            return;
        }
    }

    // 默认GET请求处理
    reqType_ = GET_HTML;
    reqRes_ = resDir_ + path_;
}

void HttpRequest::ParsePost_()
{
    // application/x-www-form-urlencoded
    if (header_["Content-Type"] == "application/x-www-form-urlencoded")
    {
        ParseUrlencodedData_(body_, bodyRes_);
        if (SPECIAL_PATH_TAG.count(path_))
        {
            int tag = SPECIAL_PATH_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);
            if (tag == 4 || tag == 5) // register or login
            {
                bool isLogin = (tag == 5);
                if (UserVerify(bodyRes_["username"], bodyRes_["password"], isLogin))
                {
                    string cookie;
                    if (UserEnroll(bodyRes_["username"], cookie))
                    {
                        authState_ = AUTH_SET;
                        authInfo_ = cookie;
                    }
                    path_ = "/welcome.html";
                }
                else
                {
                    path_ = "/error.html";
                }
            }
            reqType_ = GET_HTML;
            reqRes_ = resDir_ + path_;
            return;
        }
    } // multipart/form-data
    else if (header_["Content-Type"].find("multipart/form-data") != string::npos)
    {
        string boundary = GetBoundaryFromContentType_(header_["Content-Type"]);
        if (!boundary.empty())
        {
            ParseMultipartFormData_(body_, boundary);
            return;
        }
    } // application/json
    else if (header_["Content-Type"] == "application/json")
    {
        nlohmann::json jsonRes;
        ParseJsonData_(body_, jsonRes);
        if (SPECIAL_PATH_TAG.count(path_))
        {
            int tag = SPECIAL_PATH_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);

            CheckCookie_();
            if (authState_ != AUTH_PASS)
            {
                return;
            }

            if (tag == 3) // delete
            {
                int err = 0;
                nlohmann::json reqRes;
                string Filename = jsonRes["file"];
                if (!DeleteFile(dataDir_ + "/" + userInfo_ + "/" + Filename))
                {
                    err = 403;
                }
                reqRes["err"] = err;
                reqType_ = GET_INFO;
                reqRes_ = reqRes.dump();
                return;
            }
        }
    }

    // 默认POST请求处理
    reqType_ = GET_HTML;
    reqRes_ = resDir_ + path_;
}

void HttpRequest::ParseUrlencodedData_(string &data, unordered_map<string, string> &paramMap)
{
    string decodedData;
    string::const_iterator it = data.begin();
    while (it != data.end())
    {
        if (*it == '+')
        {
            decodedData += ' ';
        }
        else if (*it == '%')
        {
            ++it;
            char decodedChar = static_cast<char>(stoi(string(it, it + 2), nullptr, 16));
            decodedData += decodedChar;
            it += 1;
        }
        else
        {
            decodedData += *it;
        }
        ++it;
    }

    size_t pos = 0;
    while (pos < decodedData.size())
    {
        size_t eqPos = decodedData.find('=', pos);
        size_t ampPos = decodedData.find('&', pos);

        string key = decodedData.substr(pos, eqPos - pos);
        string value = decodedData.substr(eqPos + 1, ampPos - eqPos - 1);

        paramMap[key] = value;

        if (ampPos == string::npos)
        {
            break;
        }
        pos = ampPos + 1;
    }
}

// Helper function to extract boundary from Content-Type
string HttpRequest::GetBoundaryFromContentType_(const string &contentType)
{
    size_t pos = contentType.find("boundary=");
    if (pos != string::npos)
    {
        return contentType.substr(pos + 9); // 9 is the length of "boundary="
    }
    return "";
}

// Helper function to parse multipart/form-data
void HttpRequest::ParseMultipartFormData_(const string &data, const string &boundary)
{
    // Split data into parts using boundary
    vector<string> parts = SplitString_(data, "--" + boundary);
    for (const string &part : parts)
    {
        if (!part.empty())
        {
            size_t headerEnd = part.find("\r\n\r\n");
            if (headerEnd != string::npos)
            {
                string headers = part.substr(0, headerEnd);
                string content = part.substr(headerEnd + 4); // 4 is the length of "\r\n\r\n"

                // Extract filename, name, content type, etc. from headers
                // Process content (e.g., save to a file or handle form fields)
                // Example code:
                map<string, string> partHeaders;
                ParsePartHeaders_(headers, partHeaders);

                if (partHeaders.count("Content-Disposition"))
                {
                    string disposition = partHeaders["Content-Disposition"];
                    string contentType = partHeaders["Content-Type"];
                    map<string, string> dispositionParams;
                    ParseHeaderValueParams_(disposition, dispositionParams);

                    if (dispositionParams.count("name"))
                    {
                        string paramName = dispositionParams["name"];

                        if (dispositionParams.count("filename")) // 文件类型
                        {
                            if (SPECIAL_PATH_TAG.count(path_))
                            {
                                int tag = SPECIAL_PATH_TAG.find(path_)->second;
                                LOG_DEBUG("Tag:%d", tag);

                                CheckCookie_();
                                if (authState_ != AUTH_PASS)
                                {
                                    return;
                                }

                                if (tag == 1) // upload
                                {
                                    // This part contains a file upload, process it accordingly
                                    string filename = dispositionParams["filename"];
                                    SaveFileUpload_(filename, content);
                                    return;
                                }
                            }
                        }
                        else
                        {
                            // This part contains a form field, process it accordingly
                            ProcessFormField_(paramName, content);
                        }
                    }
                }
            }
        }
    }
}

// Helper function to parse headers of multipart/form-data part
void HttpRequest::ParsePartHeaders_(const string &headers, map<string, string> &headerMap)
{
    vector<string> lines = SplitString_(headers, "\r\n");
    for (const string &line : lines)
    {
        size_t colonPos = line.find(":");
        if (colonPos != string::npos)
        {
            string key = TrimString_(line.substr(0, colonPos));
            string value = TrimString_(line.substr(colonPos + 1));
            headerMap[key] = value;
        }
    }
}

// Helper function to parse header value parameters
void HttpRequest::ParseHeaderValueParams_(const string &value, map<string, string> &paramMap)
{
    vector<string> parts = SplitString_(value, ";");
    for (size_t i = 1; i < parts.size(); ++i)
    {
        size_t equalsPos = parts[i].find("=");
        if (equalsPos != string::npos)
        {
            string key = TrimString_(parts[i].substr(0, equalsPos));
            string value = TrimString_(parts[i].substr(equalsPos + 1));
            if (!value.empty() && value.front() == '"' && value.back() == '"')
            {
                value = value.substr(1, value.length() - 2);
            }
            paramMap[key] = value;
        }
    }
}

// Helper function to process form field
void HttpRequest::ProcessFormField_(const string &paramName, const string &fieldValue)
{
    // Process form field data as needed
    LOG_DEBUG("Form field: %s = %s", paramName.c_str(), fieldValue.c_str());
}

// Helper function to save file upload
void HttpRequest::SaveFileUpload_(const string &filename, const string &content)
{
    nlohmann::json reqRes;
    // Check if the filename is valid
    if (filename.empty())
    {
        LOG_ERROR("Invalid filename for uploaded file.");
        return;
    }

    // Define the directory where uploaded files will be stored
    string uploadDirectory = dataDir_ + "/" + userInfo_ + "/";

    // Create the full path for the uploaded file
    string fullPath = uploadDirectory + filename;
    size_t fileSize = content.size();
    if (fileSize > 0)
    {
        LOG_DEBUG("File upload: %s, size: %lu", fullPath.c_str(), fileSize);
    }
    else // 禁止空文件上传
    {
        LOG_ERROR("Empty file upload: %s", fullPath.c_str());
        reqRes["err"] = 400;
        reqType_ = GET_INFO;
        reqRes_ = reqRes.dump();
        return;
    }

    // Save the uploaded file content to the specified location
    ofstream file(fullPath, ios::out | ios::app | ios::binary);
    if (file.is_open())
    {
        file.write(content.c_str(), fileSize);
        file.close();
        LOG_DEBUG("Uploaded file saved: %s", fullPath.c_str());
        struct stat fileStat;
        if (stat(fullPath.c_str(), &fileStat) == 0)
        {
            reqRes["err"] = 0;
            reqRes["fileName"] = filename;
            reqRes["fileSize"] = fileSize;
            reqRes["uploadDate"] = static_cast<unsigned long long>(fileStat.st_mtime);
        }
        else
        {
            reqRes["err"] = 500;
        }
    }
    else
    {
        LOG_ERROR("Failed to save uploaded file: %s", fullPath.c_str());
        reqRes["err"] = 403;
    }
    reqType_ = GET_INFO;
    reqRes_ = reqRes.dump();
}

// Helper function to split string by a delimiter
vector<string> HttpRequest::SplitString_(const string &str, const string &delimiter)
{
    vector<string> parts;
    size_t start = 0;
    size_t end = 0;
    while ((end = str.find(delimiter, start)) != string::npos)
    {
        parts.push_back(str.substr(start, end - start));
        start = end + delimiter.length();
    }
    parts.push_back(str.substr(start));
    return parts;
}

// Helper function to trim leading and trailing whitespace from a string
string HttpRequest::TrimString_(const string &str)
{
    size_t firstNotSpace = str.find_first_not_of(" \t\r\n");
    if (firstNotSpace == string::npos)
    {
        return "";
    }
    size_t lastNotSpace = str.find_last_not_of(" \t\r\n");
    return str.substr(firstNotSpace, lastNotSpace - firstNotSpace + 1);
}

// Helper function to parse application/json data
void HttpRequest::ParseJsonData_(const string &jsonData, nlohmann::json &jsonObject)
{
    // Use a JSON parsing library (e.g., nlohmann::json) to parse jsonData
    try
    {
        jsonObject = nlohmann::json::parse(jsonData);
    }
    catch (const nlohmann::json::exception &e)
    {
        // Handle JSON parsing error
        LOG_ERROR("JSON parsing error: %s", e.what());
    }
}

bool HttpRequest::DeleteFile(const string &path)
{
    int result = remove(path.c_str());
    if (result == 0)
    {
        LOG_DEBUG("File deleted successfully: %s", path.c_str());
        return true;
    }
    else
    {
        LOG_ERROR("Error deleting file: %s", path.c_str());
        return false;
    }
}

void HttpRequest::GetFileList(const string &path, nlohmann::json &jsonObject)
{
    // Check if the directory exists, and create it if not
    DIR *dir = opendir(path.c_str());
    if (dir == nullptr)
    {
        LOG_DEBUG("Try to create directory: %s", path.c_str());
        if (mkdir(path.c_str(), 0777) != 0)
        {
            LOG_ERROR("Failed to create directory: %s", path.c_str());
            return;
        }
        dir = opendir(path.c_str());
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        string entryName = entry->d_name;
        if (entryName == "." || entryName == "..")
        {
            continue;
        }

        string entryPath = path + "/" + entryName;

        struct stat fileStat;
        if (stat(entryPath.c_str(), &fileStat) != 0)
        {
            LOG_ERROR("Failed to get file stat for: %s", entryPath);
            continue;
        }

        if (S_ISREG(fileStat.st_mode))
        {
            nlohmann::json fileInfo;
            fileInfo["fileName"] = entryName;
            fileInfo["fileSize"] = static_cast<unsigned long long>(fileStat.st_size);
            fileInfo["uploadDate"] = static_cast<unsigned long long>(fileStat.st_mtime);
            jsonObject.push_back(fileInfo);
        }
    }

    closedir(dir);
}

bool HttpRequest::UserVerify(const string &name, const string &pwd, bool isLogin)
{
    if (name == "" || pwd == "")
    {
        return false;
    }
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());
    MYSQL *sql;
    ConnRAII<MYSQL> sqlRAII(&sql, MySQLConnPool::Instance());
    assert(sql);

    bool flag = false;
    bool used = false;
    char order[256] = {0};
    MYSQL_RES *res = nullptr;

    // 查询用户及密码
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("%s", order);

    if (mysql_query(sql, order))
    {
        mysql_free_result(res);
        return false;
    }
    res = mysql_store_result(sql);

    while (MYSQL_ROW row = mysql_fetch_row(res))
    {
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        string password(row[1]);
        // 登录行为
        if (isLogin)
        {
            if (pwd == password)
            {
                // 登录成功
                flag = true;
            }
            else
            {
                flag = false;
                LOG_DEBUG("pwd error!");
            }
        }
        else
        {
            used = true;
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);

    // 注册行为 且 用户名未被使用
    if (!isLogin && !used)
    {
        LOG_DEBUG("regirster!");
        bzero(order, 256);
        snprintf(order, 256, "INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG("%s", order);
        if (mysql_query(sql, order))
        {
            LOG_DEBUG("Insert error!");
            flag = false;
        }
        // 注册成功
        flag = true;
    }

    LOG_DEBUG("UserVerify success!");
    return flag;
}

bool HttpRequest::UserVerify(const string &uid, string &userInfo)
{
    if (uid == "")
    {
        return false;
    }
    LOG_DEBUG("Verify uid:%s", uid.c_str());
    redisContext *redis;
    ConnRAII<redisContext> redisRAII(&redis, RedisConnPool::Instance());
    assert(redis);

    bool flag = false;
    char order[256] = {0};
    redisReply *reply = nullptr;

    // 检查cookie是否存在并获取username
    snprintf(order, 256, "HGET %s username", uid.c_str());
    LOG_DEBUG("%s", order);
    reply = (redisReply *)redisCommand(redis, order);
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR)
    {
        // Handle error
        if (reply != nullptr)
            LOG_ERROR("Redis command error: %s", reply->str);
        return false;
    }
    else if (reply->type == REDIS_REPLY_STRING)
    {
        // 获取到了用户信息
        string username = reply->str;
        userInfo = username; // 将用户名存储到 userInfo
        flag = true;         // 用户验证成功
    }
    else if (reply->type == REDIS_REPLY_NIL)
    {
        // 用户信息不存在
        LOG_DEBUG("User information not found in Redis");
        flag = false; // 用户验证失败
    }

    freeReplyObject(reply);
    return flag;
}

bool HttpRequest::UserEnroll(const string &userInfo, string &cookie)
{
    if (userInfo == "")
    {
        return false;
    }
    LOG_DEBUG("User enroll:%s", userInfo.c_str());
    redisContext *redis;
    ConnRAII<redisContext> redisRAII(&redis, RedisConnPool::Instance());
    assert(redis);

    bool used = false;
    string uid;
    int timeout = 60 * 60 * 24 * 1; // 1 day
    char order[256] = {0};
    redisReply *reply = nullptr;

    do
    {
        // 检查uid是否存在
        uid = GenerateRandomID();
        snprintf(order, 256, "EXISTS %s", uid.c_str());
        LOG_DEBUG("%s", order);
        reply = (redisReply *)redisCommand(redis, order);
        if (reply == nullptr || reply->type == REDIS_REPLY_ERROR)
        {
            // Handle error
            if (reply != nullptr)
                LOG_ERROR("Redis command error: %s", reply->str);
            return false;
        }
        else if (reply->type == REDIS_REPLY_INTEGER)
        {
            if (reply->integer == 1)
            {
                LOG_DEBUG("uid exists!");
                used = true;
            }
            else
            {
                used = false;
            }
        }
    } while (used == true);

    // 设置uid
    snprintf(order, 256, "HSET %s username %s", uid.c_str(), userInfo.c_str());
    LOG_DEBUG("%s", order);
    reply = (redisReply *)redisCommand(redis, order);
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR)
    {
        // Handle error
        if (reply != nullptr)
            LOG_ERROR("Redis command error: %s", reply->str);
        return false;
    }

    // 设置过期时间
    snprintf(order, 256, "EXPIRE %s %d", uid.c_str(), timeout);
    LOG_DEBUG("%s", order);
    reply = (redisReply *)redisCommand(redis, order);
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR)
    {
        // Handle error
        if (reply != nullptr)
            LOG_ERROR("Redis command error: %s", reply->str);
        return false;
    }

    freeReplyObject(reply);

    // Get the current time
    chrono::system_clock::time_point now = chrono::system_clock::now();

    // Calculate the expiration time
    chrono::duration<int> expiration_duration(timeout);
    chrono::system_clock::time_point expiration_time = now + expiration_duration;

    // Convert the expiration time to a time_t
    time_t expiration_t = chrono::system_clock::to_time_t(expiration_time);

    // Convert the expiration time to a tm struct
    tm *expiration_tm = gmtime(&expiration_t);

    // Format the expiration time as a string in the correct format
    char expires_str[100];
    strftime(expires_str, sizeof(expires_str), "%a, %d %b %Y %T GMT", expiration_tm);
    string expires_param = expires_str;

    cookie = "session_id=" + uid + "; expires=" + expires_param + "; path=/; HttpOnly";
    return true;
}

bool HttpRequest::UserQuit(const string &uid)
{
    if (uid == "")
    {
        return false;
    }
    LOG_DEBUG("Verify uid:%s", uid.c_str());
    redisContext *redis;
    ConnRAII<redisContext> redisRAII(&redis, RedisConnPool::Instance());
    assert(redis);

    char order[256] = {0};
    redisReply *reply = nullptr;

    // 删除uid
    snprintf(order, 256, "DEL %s", uid.c_str());
    LOG_DEBUG("%s", order);
    reply = (redisReply *)redisCommand(redis, order);
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR)
    {
        // Handle error
        if (reply != nullptr)
            LOG_ERROR("Redis command error: %s", reply->str);
        return false;
    }
    freeReplyObject(reply);
    return true;
}

string HttpRequest::GenerateRandomID()
{
    // Seed for the random number generator
    random_device rd;

    // Use the Mersenne Twister engine with a random seed
    mt19937 gen(rd());

    // Define the characters allowed in the uid
    const string characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    // uid length
    const int uid_length = 16;

    // Generate the uid
    uniform_int_distribution<> dis(0, characters.size() - 1);
    string uid;
    for (int i = 0; i < uid_length; ++i)
    {
        uid += characters[dis(gen)];
    }

    return uid;
}

HttpRequest::PARSE_STATE HttpRequest::State() const
{
    return state_;
}

string HttpRequest::path() const
{
    return path_;
}

HttpRequest::HTTP_METHOD HttpRequest::method() const
{
    return method_;
}

string HttpRequest::version() const
{
    return version_;
}

string HttpRequest::GetBody(const string &key) const
{
    assert(key != "");
    if (bodyRes_.count(key) == 1)
    {
        return bodyRes_.find(key)->second;
    }
    return "";
}

string HttpRequest::GetBody(const char *key) const
{
    assert(key != nullptr);
    if (bodyRes_.count(key) == 1)
    {
        return bodyRes_.find(key)->second;
    }
    return "";
}

HttpRequest::REQ_TYPE HttpRequest::reqType() const
{
    return reqType_;
}

string HttpRequest::reqRes() const
{
    return reqRes_;
}

string &HttpRequest::reqRes()
{
    return reqRes_;
}

HttpRequest::AUTH_STATE HttpRequest::authState() const
{
    return authState_;
}

string HttpRequest::authInfo() const
{
    return authInfo_;
}

string &HttpRequest::authInfo()
{
    return authInfo_;
}

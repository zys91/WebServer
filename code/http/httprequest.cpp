/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */
#include "httprequest.h"

#include <fstream>
#include <regex>
#include <dirent.h>
#include <sys/stat.h>
#include <mysql/mysql.h> //mysql

#include "log/log.h"
#include "pool/connpool.h"
#include "pool/connRAII.h"

using namespace std;

const unordered_set<string> HttpRequest::DEFAULT_HTML{
    "/index",
    "/register",
    "/login",
    "/welcome",
    "/video",
    "/picture",
};

const unordered_map<string, int> HttpRequest::DEFAULT_HTML_TAG{
    {"/register.html", 0},
    {"/login.html", 1},
};

const unordered_map<string, int> HttpRequest::SPECIAL_PATH_TAG{
    {"/upload", 0},    // post
    {"/fileslist", 1}, // get
    {"/delete", 2},    // post
    {"/download", 3},  // get
};

HttpRequest::HttpRequest()
{
    method_ = url_ = path_ = query_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    reqType_ = GET_HTML;
    reqRes_ = path_;
}

void HttpRequest::Init(const string &resDir, const string &dataDir)
{
    method_ = url_ = path_ = query_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    reqType_ = GET_HTML;
    reqRes_ = path_;
    resDir_ = resDir;
    dataDir_ = dataDir;
    header_.clear();
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

HttpRequest::HTTP_CODE HttpRequest::parse(Buffer &buff)
{
    const char CRLF[] = "\r\n";
    if (buff.ReadableBytes() <= 0)
    {
        return NO_REQUEST;
    }

    while (buff.ReadableBytes() && state_ != FINISH)
    {
        const char *lineEnd;
        if (state_ == BODY) // TODO: 支持流式分批处理BODY数据，尤其是大文件上传业务
        {
            lineEnd = buff.BeginWriteConst();
            /*判断post数据是否接受完整，未接收完则退出循环，表示继续请求*/
            if (buff.ReadableBytes() < static_cast<unsigned long>(atol(header_["Content-Length"].c_str())))
            {
                break;
            }
        }
        else
        {
            lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
            /* 若解析状态停留在HEADERS且没有空行作为结尾，直接退出循环，等待接收剩余数据 */
            if (lineEnd == buff.BeginWriteConst() && state_ == HEADERS)
            {
                break;
            }
        }

        string line(buff.Peek(), lineEnd); // 提取一行，不含CRLF

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
            // GET请求且解析到空行，则解析完成，即使BODY还有数据也不再解析
            if (state_ == BODY && method_ == "GET")
            {
                ParseGet_();
                state_ = FINISH;
                buff.RetrieveAll();
                return GET_REQUEST;
            } // POST请求且解析到空行，且Content-Length为0，则解析完成，即BODY无数据
            else if (state_ == BODY && method_ == "POST" && header_["Content-Length"] == "0")
            {
                body_ = "";
                ParsePost_();
                state_ = FINISH;
                buff.RetrieveAll();
                return GET_REQUEST;
            }
            break;
        case BODY:
            ParseBody_(line);
            buff.RetrieveAll();
            return GET_REQUEST;
            break;
        default:
            break;
        }
        buff.RetrieveUntil(lineEnd + 2); // 跳过末尾CRLF
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
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

bool HttpRequest::ParseRequestLine_(const string &line)
{
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if (regex_match(line, subMatch, patten))
    {
        method_ = subMatch[1];
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
    }
    else
    {
        state_ = BODY;
    }
}

void HttpRequest::ParseGet_()
{
    nlohmann::json reqRes;
    if (SPECIAL_PATH_TAG.count(path_))
    {
        int tag = SPECIAL_PATH_TAG.find(path_)->second;
        LOG_DEBUG("Tag:%d", tag);
        if (tag == 1) // fileslist
        {
            GetFileList(dataDir_, reqRes); // TODO: 支持多用户目录
            reqType_ = GET_INFO;
            reqRes_ = reqRes.dump();
            return;
        }
        else if (tag == 3 && queryRes_.count("file")) // download?file=${fileName}
        {
            reqType_ = GET_FILE;
            reqRes_ = dataDir_ + "/" + queryRes_["file"]; // TODO: 支持多用户目录
            return;
        }
    }
    reqType_ = GET_HTML;
    reqRes_ = resDir_ + path_;
}

void HttpRequest::ParseBody_(const string &line)
{
    body_ = line;
    ParsePost_();
    state_ = FINISH;
    // LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}

void HttpRequest::ParsePost_()
{
    // application/x-www-form-urlencoded
    if (method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded")
    {
        ParseUrlencodedData_(body_, bodyRes_);
        if (DEFAULT_HTML_TAG.count(path_))
        {
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);
            if (tag == 0 || tag == 1)
            {
                bool isLogin = (tag == 1);
                if (UserVerify(bodyRes_["username"], bodyRes_["password"], isLogin))
                {
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
    else if (method_ == "POST" && header_["Content-Type"].find("multipart/form-data") != string::npos)
    {
        string boundary = GetBoundaryFromContentType_(header_["Content-Type"]);
        if (!boundary.empty())
        {
            ParseMultipartFormData_(body_, boundary);
            return;
        }
    } // application/json
    else if (method_ == "POST" && header_["Content-Type"] == "application/json")
    {
        nlohmann::json jsonRes;
        ParseJsonData_(body_, jsonRes);
        if (SPECIAL_PATH_TAG.count(path_))
        {
            int tag = SPECIAL_PATH_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);
            if (tag == 2)
            {
                int err = 0;
                nlohmann::json reqRes;
                string Filename = jsonRes["file"];
                if (!DeleteFile(dataDir_ + "/" + Filename)) // TODO: 支持多用户目录
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

                        if (dispositionParams.count("filename"))
                        {
                            if (SPECIAL_PATH_TAG.count(path_))
                            {
                                int tag = SPECIAL_PATH_TAG.find(path_)->second;
                                LOG_DEBUG("Tag:%d", tag);
                                if (tag == 0)
                                {
                                    // This part contains a file upload, process it accordingly
                                    string filename = dispositionParams["filename"];
                                    SaveFileUpload_(filename, content);
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
    string uploadDirectory = dataDir_ + "/"; // TODO: 支持多用户目录

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
        LOG_ERROR("Failed to create directory: %s", path.c_str());
        return;
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
    char order[256] = {0};
    MYSQL_RES *res = nullptr;

    if (!isLogin)
    {
        flag = true;
    }
    /* 查询用户及密码 */
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
        /* 注册行为 且 用户名未被使用*/
        if (isLogin)
        {
            if (pwd == password)
            {
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
            flag = false;
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);

    /* 注册行为 且 用户名未被使用*/
    if (!isLogin && flag == true)
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
        flag = true;
    }

    LOG_DEBUG("UserVerify success!");
    return flag;
}

HttpRequest::PARSE_STATE HttpRequest::State() const
{
    return state_;
}

string HttpRequest::path() const
{
    return path_;
}

string HttpRequest::method() const
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

string HttpRequest::reqRes() const
{
    return reqRes_;
}

string &HttpRequest::reqRes()
{
    return reqRes_;
}

int HttpRequest::reqType() const
{
    return reqType_;
}
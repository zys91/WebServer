/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */

#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <string>
#include <atomic>
#include <sys/types.h>
#include <arpa/inet.h> // sockaddr

#include "buffer/buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

class HttpConn
{
public:
    HttpConn();

    ~HttpConn();

    void Init(int sockFd, const sockaddr_storage &addr);

    ssize_t read(int *saveErrno);

    ssize_t write(int *saveErrno);

    void Close();

    int GetFd() const;

    uint16_t GetPort() const;

    std::string GetIP() const;

    sockaddr_storage GetAddr() const;

    bool process();

    int ToWriteBytes()
    {
        return iov_[0].iov_len + iov_[1].iov_len;
    }

    bool IsKeepAlive() const
    {
        return request_.IsKeepAlive();
    }

    static bool isET;
    static std::string resDir;
    static std::string dataDir;
    static std::atomic<int> userCount;

private:
    int fd_;
    struct sockaddr_storage addr_;

    bool isClose_;

    int iovCnt_;
    struct iovec iov_[2];

    Buffer readBuff_;  // 读缓冲区
    Buffer writeBuff_; // 写缓冲区

    HttpRequest request_;
    HttpResponse response_;
};

#endif // HTTP_CONN_H
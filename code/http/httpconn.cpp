/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */
#include "httpconn.h"

#include <errno.h>
#include <fcntl.h>        // open
#include <unistd.h>       // close
#include <sys/uio.h>      // readv/writev
#include <sys/sendfile.h> // sendfile

#include "log/log.h"
#include "pool/sqlconnRAII.h"

using namespace std;

string HttpConn::resDir;
string HttpConn::dataDir;
atomic<int> HttpConn::userCount;
bool HttpConn::isET;

HttpConn::HttpConn()
{
    fd_ = -1;
    addr_ = {0};
    isClose_ = true;
};

HttpConn::~HttpConn()
{
    Close();
};

void HttpConn::init(int fd, const sockaddr_storage &addr)
{
    assert(fd > 0);
    userCount++;
    addr_ = addr;
    fd_ = fd;
    writeBuff_.RetrieveAll();
    readBuff_.RetrieveAll();
    request_.Init(resDir, dataDir);
    isClose_ = false;
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd_, GetIP().c_str(), GetPort(), (int)userCount);
}

void HttpConn::Close()
{
    response_.UnmapFile();
    response_.CloseFile();
    if (isClose_ == false)
    {
        isClose_ = true;
        userCount--;
        close(fd_);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP().c_str(), GetPort(), (int)userCount);
    }
}

int HttpConn::GetFd() const
{
    return fd_;
};

sockaddr_storage HttpConn::GetAddr() const
{
    return addr_;
}

string HttpConn::GetIP() const
{
    if (addr_.ss_family == AF_INET)
    {
        const sockaddr_in *ipv4Addr = reinterpret_cast<const sockaddr_in *>(&addr_);
        char ipv4Str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(ipv4Addr->sin_addr), ipv4Str, INET_ADDRSTRLEN);
        return string(ipv4Str);
    }
    else
    {
        const sockaddr_in6 *ipv6Addr = reinterpret_cast<const sockaddr_in6 *>(&addr_);
        char ipv6Str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &(ipv6Addr->sin6_addr), ipv6Str, INET6_ADDRSTRLEN);
        return string(ipv6Str);
    }
}

uint16_t HttpConn::GetPort() const
{
    if (addr_.ss_family == AF_INET)
    {
        const sockaddr_in *ipv4Addr = reinterpret_cast<const sockaddr_in *>(&addr_);
        return ntohs(ipv4Addr->sin_port);
    }
    else
    {
        const sockaddr_in6 *ipv6Addr = reinterpret_cast<const sockaddr_in6 *>(&addr_);
        return ntohs(ipv6Addr->sin6_port);
    }
}

ssize_t HttpConn::read(int *saveErrno)
{
    ssize_t len = -1;
    /*如果是LT模式，那么只读取一次，如果是ET模式，会一直读取，直到读不出数据*/
    do
    {
        len = readBuff_.ReadFd(fd_, saveErrno);
        if (len <= 0)
        {
            break;
        }
    } while (isET);
    return len;
}

ssize_t HttpConn::write(int *saveErrno)
{
    ssize_t len = -1;
    do
    {
        if (iovCnt_ == 1 && iov_[0].iov_len == 0 && iov_[1].iov_len > 0) // SENDFILE
        {
            off_t offset = response_.FileLen() - iov_[1].iov_len;
            len = sendfile(fd_, response_.FileFd(), &offset, response_.FileLen());
            iov_[1].iov_len -= len;
        }
        else
        {
            len = writev(fd_, iov_, iovCnt_);
        }

        if (len <= 0)
        {
            *saveErrno = errno;
            break;
        }

        if (ToWriteBytes() == 0)
        {
            break; /* 传输结束 */
        }
        else if (iovCnt_ > 1 && static_cast<size_t>(len) > iov_[0].iov_len) // MMAP
        {
            iov_[1].iov_base = (uint8_t *)iov_[1].iov_base + (len - iov_[0].iov_len);
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            if (iov_[0].iov_len)
            {
                writeBuff_.RetrieveAll();
                iov_[0].iov_len = 0;
            }
        }
        else if (iov_[0].iov_len != 0)
        {
            iov_[0].iov_base = (uint8_t *)iov_[0].iov_base + len;
            iov_[0].iov_len -= len;
            writeBuff_.Retrieve(len);
        }
    } while (ToWriteBytes() > 0);

    if (ToWriteBytes() == 0)
    {
        response_.CloseFile();
        response_.UnmapFile();
    }
    return len;
}

bool HttpConn::process()
{
    if (request_.State() == HttpRequest::FINISH)
    {
        request_.Init(resDir, dataDir);
    }

    if (readBuff_.ReadableBytes() <= 0)
    {
        return false;
    }

    LOG_DEBUG("Client[%d] process...", fd_);
    HttpRequest::HTTP_CODE processStatus = request_.parse(readBuff_);
    if (processStatus == HttpRequest::GET_REQUEST)
    {
        LOG_DEBUG("req:[%d]%s", request_.reqType(), request_.reqRes().c_str());
        response_.Init(request_.reqType(), request_.reqRes(), request_.IsKeepAlive(), 200);
    }
    else if (processStatus == HttpRequest::NO_REQUEST)
    {
        LOG_DEBUG("Client[%d] wait next...", fd_);
        return false;
    }
    else
    {
        response_.Init(request_.reqType(), request_.reqRes(), false, 400);
    }

    response_.MakeResponse(writeBuff_);
    /* 响应头 */
    iov_[0].iov_base = const_cast<char *>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;

    /* 文件 */
    if (response_.FileTransMethod() == HttpResponse::MMAP)
    {
        if (response_.FileLen() > 0 && response_.FilePtr())
        {
            iov_[1].iov_base = response_.FilePtr();
            iov_[1].iov_len = response_.FileLen();
            iovCnt_ = 2;
        }
    }
    else if (response_.FileTransMethod() == HttpResponse::SENDFILE)
    {
        if (response_.FileLen() > 0 && response_.FileFd() != -1)
        {
            iov_[1].iov_base = nullptr;
            iov_[1].iov_len = response_.FileLen();
            iovCnt_ = 1;
        }
    }
    else // NONE
    {
        iov_[1].iov_base = nullptr;
        iov_[1].iov_len = 0;
    }

    LOG_DEBUG("Client[%d] response filesize:%d, %d  to %d", fd_, response_.FileLen(), iovCnt_, ToWriteBytes());
    return true;
}

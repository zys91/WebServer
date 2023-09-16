/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */

#include "webserver.h"

#include <fcntl.h>  // fcntl
#include <unistd.h> // close
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>

#include "log/log.h"
#include "pool/connpool.h"
#include "pool/connRAII.h"

using namespace std;

bool WebServer::isClose_ = false;

WebServer::WebServer(
    int port, int trigMode, int timeoutMS, bool OptLinger, bool OptIPv6,
    const char *mysqlAddr, int mysqlPort, const char *mysqlUser, const char *mysqlPwd, const char *mysqlDBName,
    const char *redisAddr, int redisPort, const char *redisUser, const char *redisPwd, const char *redisDBName,
    int connPoolNum, int threadNum,
    bool enableLog, int logLevel, int logQueSize) : port_(port), enableLinger_(OptLinger), enableIPv6_(OptIPv6), timeoutMS_(timeoutMS),
                                                    timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
{
    HttpConn::resDir = "./resources";
    HttpConn::dataDir = "./data";
    HttpConn::userCount = 0;

    InitEventMode_(trigMode);

    if (enableLog)
    {
        Log::Instance()->Init(logLevel, "./log", ".log", logQueSize);
        if (isClose_)
        {
            LOG_ERROR("========== Server Init error!==========");
        }
        else
        {
            LOG_INFO("========== Server Init ==========");
            LOG_INFO("Port:%d, EnableLinger: %s EnableIpv6: %s", port_, OptLinger ? "true" : "false", OptIPv6 ? "true" : "false");
            LOG_INFO("Listen Mode: %s, Connect Mode: %s",
                     (listenEvent_ & EPOLLET ? "ET" : "LT"),
                     (connEvent_ & EPOLLET ? "ET" : "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("resDir: %s, dataDir: %s", HttpConn::resDir.c_str(), HttpConn::dataDir.c_str());
            LOG_INFO("ConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }

    if (!MySQLConnPool::Instance()->InitPool(mysqlAddr, mysqlPort, mysqlUser, mysqlPwd, mysqlDBName, connPoolNum))
    {
        isClose_ = true;
        LOG_ERROR("========== SQLPool Init error!==========");
    }

    if (!RedisConnPool::Instance()->InitPool(redisAddr, redisPort, redisUser, redisPwd, redisDBName, connPoolNum))
    {
        isClose_ = true;
        LOG_ERROR("========== RedisPool Init error!==========");
    }

    listenFdv4_=-1;
    listenFdv6_=-1;

    if (!InitSocket_())
    {
        isClose_ = true;
        LOG_ERROR("========== Socket Init error!==========");
    }
}

WebServer::~WebServer()
{
    LOG_INFO("========== Server quit ==========");
    close(listenFdv4_);
    if (enableIPv6_)
    {
        close(listenFdv6_);
    }
    close(pipefd[0]);
    close(pipefd[1]);
    isClose_ = true;
    MySQLConnPool::Instance()->ClosePool();
    RedisConnPool::Instance()->ClosePool();
}

/*
 * 初始化事件工作模式
 * EPOLLIN：表示对应的文件描述符可以读（包括对端SOCKET正常关闭）
 * EPOLLOUT：表示对应的文件描述符可以写
 * EPOLLPRI：表示对应的文件描述符有紧急的数据可读（这里应该表示有带外数据到来）
 * EPOLLERR：表示对应的文件描述符发生错误
 * EPOLLHUP：表示对应的文件描述符被挂断，读写关闭 本端异常断开连接
 * EPOLLRDHUP 表示读关闭 对方异常断开连接
 * EPOLLET：将EPOLL设为边缘触发(Edge Triggered)模式，这是相对于水平触发(Level Triggered)而言的，默认是水平触发
 * EPOLLONESHOT：只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里
 */
void WebServer::InitEventMode_(int trigMode)
{
    listenEvent_ = EPOLLRDHUP;
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connEvent_ |= EPOLLET;
        break;
    case 2:
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);
}

void WebServer::Start()
{
    int timeMS = -1; // epoll wait timeout == -1 无事件将阻塞
    if (!isClose_)
    {
        LOG_INFO("========== Server start ==========");
    }
    while (!isClose_)
    {
        if (timeoutMS_ > 0)
        {
            timeMS = timer_->GetNextTick(); // 清除当前超时节点并获取最近的下一次超时时间
        }
        int eventCnt = epoller_->Wait(timeMS);
        for (int i = 0; i < eventCnt; i++)
        {
            // 处理事件
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            if (fd == listenFdv4_ || fd == listenFdv6_)
            {
                DealListen_(fd);
            }
            else if (fd == pipefd[0] && (events & EPOLLIN))
            {
                int endfd = -1;
                lock_guard<mutex> lock(pipeMutex);
                ssize_t bytesRead = read(pipefd[0], &endfd, sizeof(endfd));
                if (bytesRead > 0 && endfd > 0)
                {
                    assert(users_.count(endfd) > 0);
                    EndConn_(&users_[endfd]);
                }
            } // EPOLLRDHUP: 对方异常断开连接 EPOLLHUP: 本方异常断开连接 EPOLLERR: 错误
            else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                assert(users_.count(fd) > 0);
                EndConn_(&users_[fd]);
            }
            else if (events & EPOLLIN)
            {
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            }
            else if (events & EPOLLOUT)
            {
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            }
            else
            {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

void WebServer::SendError_(int fd, const char *info)
{
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if (ret < 0)
    {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

// CloseConn_为timer超时的回调函数，无论主动关闭还是超时关闭，都需要调用CloseConn_函数
void WebServer::CloseConn_(HttpConn *client)
{
    assert(client);
    LOG_INFO("Timeout -> Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    users_.erase(client->GetFd()); // 自动调用析构函数
}

// 主动关闭连接，并从timer中删除
void WebServer::EndConn_(HttpConn *client)
{
    assert(client);
    LOG_INFO("Active close -> Client[%d] quit!", client->GetFd());
    timer_->doWork(client->GetFd());
}

void WebServer::AddClient_(int fd, sockaddr_storage addr)
{
    assert(fd > 0);
    users_[fd].Init(fd, addr);
    if (timeoutMS_ > 0)
    {
        timer_->add(fd, timeoutMS_, bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

void WebServer::DealListen_(int listenFd)
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    do
    {
        int fd = accept(listenFd, (struct sockaddr *)&addr, &len);
        if (fd <= 0)
        {
            return;
        }
        else if (HttpConn::userCount >= MAX_FD)
        {
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        AddClient_(fd, addr);
    } while (listenEvent_ & EPOLLET);
}

void WebServer::DealRead_(HttpConn *client)
{
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(bind(&WebServer::OnRead_, this, client));
}

void WebServer::DealWrite_(HttpConn *client)
{
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(bind(&WebServer::OnWrite_, this, client));
}

// 延长一个连接的超时时间
void WebServer::ExtentTime_(HttpConn *client)
{
    assert(client);
    if (timeoutMS_ > 0)
    {
        timer_->adjust(client->GetFd(), timeoutMS_);
    }
}

void WebServer::OnRead_(HttpConn *client)
{
    assert(client);
    int ret = -1;
    int readErrno = 0;
    int fd = client->GetFd();
    ret = client->read(&readErrno);
    if (ret <= 0 && readErrno != EAGAIN)
    {
        lock_guard<mutex> lock(pipeMutex);
        write(pipefd[1], &fd, sizeof(fd));
        return;
    }
    OnProcess(client);
}

void WebServer::OnProcess(HttpConn *client)
{
    if (client->process())
    {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    }
    else
    {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

void WebServer::OnWrite_(HttpConn *client)
{
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    int fd = client->GetFd();
    ret = client->write(&writeErrno);
    if (client->ToWriteBytes() == 0)
    {
        // 传输完成
        if (client->IsKeepAlive())
        {
            OnProcess(client);
            return;
        }
    }
    else if (ret < 0)
    {
        if (writeErrno == EAGAIN)
        {
            // 继续传输
            epoller_->ModFd(fd, connEvent_ | EPOLLOUT);
            return;
        }
    }
    lock_guard<mutex> lock(pipeMutex);
    write(pipefd[1], &fd, sizeof(fd));
}

void sig_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM)
        WebServer::isClose_ = true;
}

bool WebServer::InitSocket_()
{
    int ret;
    if (port_ > 65535 || port_ < 1024)
    {
        LOG_ERROR("Port:%d error!", port_);
        return false;
    }

    struct sockaddr_in addr_v4;
    addr_v4.sin_family = AF_INET;
    addr_v4.sin_addr.s_addr = htonl(INADDR_ANY);
    addr_v4.sin_port = htons(port_);

    struct sockaddr_in6 addr_v6;
    addr_v6.sin6_family = AF_INET6;
    addr_v6.sin6_addr = in6addr_any;
    addr_v6.sin6_port = htons(port_);

    struct linger optLinger = {0};
    if (enableLinger_)
    {
        // 优雅关闭: 直到所剩数据发送完毕或超时
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    // 创建监听socket
    listenFdv4_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFdv4_ < 0)
    {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    if (enableIPv6_)
    {
        listenFdv6_ = socket(AF_INET6, SOCK_STREAM, 0);
        if (listenFdv6_ < 0)
        {
            LOG_ERROR("Create socket error!", port_);
            return false;
        }
    }

    // 设置linger选项
    ret = setsockopt(listenFdv4_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if (ret < 0)
    {
        close(listenFdv4_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    if (enableIPv6_)
    {
        ret = setsockopt(listenFdv6_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
        if (ret < 0)
        {
            close(listenFdv6_);
            LOG_ERROR("Init linger error!", port_);
            return false;
        }
    }

    int optval = 1;

    // 设置端口复用
    ret = setsockopt(listenFdv4_, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));
    if (ret == -1)
    {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFdv4_);
        return false;
    }

    if (enableIPv6_)
    {
        ret = setsockopt(listenFdv6_, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));
        // 设置 IPv6 只监听v6地址，防止后面同时绑定发生冲突
        ret = setsockopt(listenFdv6_, IPPROTO_IPV6, IPV6_V6ONLY, (const void *)&optval, sizeof(int));
        if (ret == -1)
        {
            LOG_ERROR("set socket setsockopt error !");
            close(listenFdv6_);
            return false;
        }
    }

    // 绑定地址
    ret = bind(listenFdv4_, (struct sockaddr *)&addr_v4, sizeof(addr_v4));
    if (ret < 0)
    {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFdv4_);
        return false;
    }

    if (enableIPv6_)
    {
        ret = bind(listenFdv6_, (struct sockaddr *)&addr_v6, sizeof(addr_v6));
        if (ret < 0)
        {
            LOG_ERROR("Bind Port:%d error!", port_);
            close(listenFdv6_);
            return false;
        }
    }

    // 监听，设置适当的最大挂起连接数
    ret = listen(listenFdv4_, 128);
    if (ret < 0)
    {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFdv4_);
        return false;
    }

    if (enableIPv6_)
    {
        ret = listen(listenFdv6_, 128);
        if (ret < 0)
        {
            LOG_ERROR("Listen port:%d error!", port_);
            close(listenFdv6_);
            return false;
        }
    }

    // 添加到epoll中
    ret = epoller_->AddFd(listenFdv4_, listenEvent_ | EPOLLIN);
    if (ret == 0)
    {
        LOG_ERROR("Add listen error!");
        close(listenFdv4_);
        return false;
    }
    SetFdNonblock(listenFdv4_);

    if (enableIPv6_)
    {
        ret = epoller_->AddFd(listenFdv6_, listenEvent_ | EPOLLIN);
        if (ret == 0)
        {
            LOG_ERROR("Add listen error!");
            close(listenFdv6_);
            return false;
        }
        SetFdNonblock(listenFdv6_);
    }
    // 创建管道，用于信号处理，多线程写入管道pipefd[1]，主线程从管道pipefd[0]读取数据
    ret = pipe(pipefd);
    if (ret == -1)
    {
        LOG_ERROR("pipe error!");
        return false;
    }

    ret = epoller_->AddFd(pipefd[0], EPOLLIN);
    if (ret == 0)
    {
        LOG_ERROR("Add pipefd error!");
        close(pipefd[0]);
        return false;
    }
    SetFdNonblock(pipefd[0]); // 管道读
    SetFdNonblock(pipefd[1]); // 管道写

    // 屏蔽管道信号 SIGPIPE: Broken pipe 防止程序向已关闭的socket写数据时，系统向进程发送SIGPIPE信号，导致进程退出
    signal(SIGPIPE, SIG_IGN);
    // 注册信号处理函数 SIGINT: Ctrl+C SIGTERM: kill
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    LOG_INFO("Server port:%d", port_);
    return true;
}

int WebServer::SetFdNonblock(int fd)
{
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}

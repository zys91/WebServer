/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <arpa/inet.h> // sockaddr

#include "epoller.h"
#include "timer/heaptimer.h"
#include "pool/threadpool.h"
#include "http/httpconn.h"

class WebServer
{
public:
    WebServer(
        int port, int trigMode, int timeoutMS, bool OptLinger, bool OptIPv6,
        const char *mysqlAddr, int mysqlPort, const char *mysqlUser, const char *mysqlPwd, const char *mysqlDBName,
        const char *redisAddr, int redisPort, const char *redisUser, const char *redisPwd, const char *redisDBName,
        int connPoolNum, int threadNum,
        bool enableLog, int logLevel, int logQueSize);

    ~WebServer();
    void Start();
    static bool isClose_;

private:
    bool InitSocket_();
    void InitEventMode_(int trigMode);
    void AddClient_(int fd, sockaddr_storage addr);

    void DealListen_(int listenFd);
    void DealWrite_(HttpConn *client);
    void DealRead_(HttpConn *client);

    void SendError_(int fd, const char *info);
    void ExtentTime_(HttpConn *client);
    void CloseConn_(HttpConn *client);
    void EndConn_(HttpConn *client);

    void OnRead_(HttpConn *client);
    void OnWrite_(HttpConn *client);
    void OnProcess(HttpConn *client);

    static const int MAX_FD = 65536;

    static int SetFdNonblock(int fd);

    int port_;
    bool enableLinger_;
    bool enableIPv6_;
    int timeoutMS_; // 毫秒MS
    int listenFdv4_;
    int listenFdv6_;
    int pipefd[2]; // 文件描述符数组，0表示读取端，1表示写入端
    std::mutex pipeMutex;

    uint32_t listenEvent_;
    uint32_t connEvent_;

    std::unique_ptr<HeapTimer> timer_;
    std::unique_ptr<ThreadPool> threadpool_;
    std::unique_ptr<Epoller> epoller_;
    std::unordered_map<int, HttpConn> users_;
};

#endif // WEBSERVER_H
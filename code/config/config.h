#ifndef CONFIG_H
#define CONFIG_H

class Config
{
public:
    Config();
    ~Config(){};

    void parse_arg(int argc, char *argv[]);

    bool sr_daemon;     // 是否后台运行
    int sr_port;        // 服务端口
    int sr_trigMode;    // 事件触发模式
    int sr_timeoutMS;   // 超时时间
    bool sr_optLinger;  // Linger选项
    bool sr_optIPv6;    // 双栈支持选项
    int sr_connPoolNum; // 连接池数量
    int sr_threadNum;   // 线程池数量
    bool sr_enableLog;  // 日志开关
    int sr_logLevel;    // 日志等级
    int sr_logQueSize;  // 日志异步队列容量
};

#endif
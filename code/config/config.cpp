/*
 * @Author       : zys
 * @Date         : 2023-08-20
 * @copyleft Apache 2.0
 */
#include "config.h"

#include <iostream>
#include <getopt.h>

using namespace std;

Config::Config()
{
    sr_daemon = false;    // 前台运行 -d
    sr_port = 1316;       // 服务端口 默认1316 -p 1316
    sr_trigMode = 3;      // ET+ET -e 3
    sr_timeoutMS = 60000; // 超时60s -t 60000
    sr_optLinger = false;  // 优雅退出 -L
    sr_optIPv6 = false;    // 双栈支持 -I
    sr_connPoolNum = 12;  // 连接池数量 -C 12
    sr_threadNum = 8;     // 线程池数量 -T 8
    sr_enableLog = false;  // 日志开关 -l
    sr_logLevel = 1;      // 日志等级 -D 1
    sr_logQueSize = 1024; // 日志异步队列容量 -q 1024
}

void Config::parse_arg(int argc, char *argv[])
{
    int opt;
    const char *str = "dp:e:t:LIC:T:lD:q:h";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'd':
        {
            sr_daemon = true;
            break;
        }
        case 'p':
        {
            sr_port = atoi(optarg);
            break;
        }
        case 'e':
        {
            sr_trigMode = atoi(optarg);
            break;
        }
        case 't':
        {
            sr_timeoutMS = atoi(optarg);
            break;
        }
        case 'L':
        {
            sr_optLinger = true;
            break;
        }
        case 'I':
        {
            sr_optIPv6 = true;
            break;
        }
        case 'C':
        {
            sr_connPoolNum = atoi(optarg);
            break;
        }
        case 'T':
        {
            sr_threadNum = atoi(optarg);
            break;
        }
        case 'l':
        {
            sr_enableLog = true;
            break;
        }
        case 'D':
        {
            sr_logLevel = atoi(optarg);
            break;
        }
        case 'q':
        {
            sr_logQueSize = atoi(optarg);
            break;
        }
        case 'h':
        {
            cout << " -p <port>          port" << endl;
            cout << " -e <emm>           epoll mode : 0 LT + LT, 1 LT + ET, 2 ET + LT, 3 ET + ET" << endl;
            cout << " -t <ms>            timeout ms" << endl;
            cout << " -L                 enable linger" << endl;
            cout << " -I                 enable IPv6" << endl;
            cout << " -C <num>           mysql connection pool num" << endl;
            cout << " -T <threadnum>     threadnum" << endl;
            cout << " -l                 enable log" << endl;
            cout << " -D <level>         log level : 0 DEBUG, 1 INFO, 2 WARN, 3 ERROR" << endl;
            cout << " -q <capacity>      log que capacity" << endl;
            cout << " -d                 run as a daemon" << endl;
            exit(EXIT_SUCCESS);
        }
        default:
            cerr << "Invalid option: -" << static_cast<char>(optopt) << endl;
            exit(EXIT_FAILURE);
        }
    }
}
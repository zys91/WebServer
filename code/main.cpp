/*
 * @Author       : mark
 * @Date         : 2020-06-18
 * @copyleft Apache 2.0
 */

#include <iostream>
#include <unistd.h> // daemon

#include "server/webserver.h"
#include "config/config.h"

using namespace std;

int main(int argc, char *argv[])
{
    // Mysql相关
    const char *db_addr = "192.168.8.215"; // 数据库地址
    int db_port = 3306;                    // 数据库端口
    const char *db_user = "root";          // 数据库用户名
    const char *db_pwd = "root";           // 数据库密码
    const char *db_Name = "webserver";     // 数据库名

    // 命令行解析
    Config config;
    config.parse_arg(argc, argv);

    if (config.sr_daemon)
        daemon(1, 0);

    WebServer server(
        config.sr_port, config.sr_trigMode, config.sr_timeoutMS, config.sr_optLinger, config.sr_optIPv6,            /* 端口 ET模式 超时时间 优雅退出 双栈支持 */
        db_addr, db_port, db_user, db_pwd, db_Name,                                                                 /* Mysql配置 */
        config.sr_connPoolNum, config.sr_threadNum, config.sr_enableLog, config.sr_logLevel, config.sr_logQueSize); /* 连接池数量 线程池数量 日志开关 日志等级 日志异步队列容量 */
    server.Start();
}

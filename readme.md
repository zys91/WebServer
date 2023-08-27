# WebServer

用C++实现的高性能WEB服务器，经过webbenchh压力测试可以实现上万的QPS

## 说明

* 基于原作者项目二次开发，修复若干问题，实现新功能

## 原版功能

* 利用IO复用技术Epoll与线程池实现多线程的Reactor高并发模型
* 利用正则与状态机解析HTTP请求报文，实现处理静态资源的请求
* 利用标准库容器封装char，实现自动增长的缓冲区
* 基于小根堆实现的定时器，关闭超时的非活动连接
* 利用单例模式与阻塞队列实现异步的日志系统，记录服务器运行状态
* 利用RAII机制实现了数据库连接池，减少数据库连接建立与关闭的开销，同时实现了用户注册登录功能
* 增加logsys，threadpool测试单元

## 问题修复

* 优化了头文件的引用方式，以更好地遵循规范和标准
* 对HTTP报文状态机解析逻辑进行了优化，使用主从状态机以支持处理接收不完整的报文
* 优化了主动关闭连接的逻辑，确保及时从计时器中删除节点，避免误删无关节点的情况
* 解决了在关闭连接后未清除`user_`容器中的`HTTPConn`对象所导致的内存持续占用问题
* 修复了`HeapTimer`中一些断言触发的内存越界问题
* 其他若干问题修复

## 新增功能

* 引入了CMake工具来进行项目的构建与编译，以更好地管理项目的依赖和构建过程
* 对`Config`类进行了完善，支持通过命令参数来运行程序，提供了更灵活的配置方式
* 支持IPv6地址的监听，实现了服务端双栈网络的支持
* 增加了文件上传功能，实现基本的文件服务器功能（浏览目录、上传、下载、删除文件）
* 对`Connpool`对象进行了重写，引入了模板类和工厂方法设计模式，实现了MySQL和Redis数据库连接池的统一管理
* 引入了Redis缓存型数据库，通过设置Cookie实现了用户登录注册后的鉴权功能，支持面向多用户的服务应用
* 对前端Web页面进行了排版设计的完善，根据业务逻辑优化了页面的展示效果
* 支持解析不同Content-Type的POST请求主体部分，例如`application/x-www-form-urlencoded`、`multipart/form-data`、`application/json`
* 支持GET请求中`path`中携带`query`参数的解析

## 环境要求

* Linux
* C++14
* MySql
* Redis

## 目录说明

```bash
.
├── bin # 二进制可执行文件生成目录
├── build # 编译缓存文件目录
├── build.sh # 一键编译脚本
├── CMakeLists.txt # CMake编译规则文件
├── code # 源码目录
├── data # 用户文件数据目录
├── include # 三方库引用目录
├── LICENSE # 开源许可证文件 
├── log # 运行日志目录
├── readme.md # README文件
├── resources # Web 静态资源目录
├── test # 测试源码目录
└── webbench-1.5 # 性能测试源码目录
```

## 项目启动

需要先配置好对应的数据库

```bash
# 安装数据库与依赖库，数据库基本配置此处不赘述
apt install mysql-server libmysqlclient-dev
apt install redis-server libhiredis-dev

# 建立webserver库
create database webserver;
# 创建user表
USE webserver;
CREATE TABLE user(
    username char(50) NULL,
    password char(50) NULL
)ENGINE=InnoDB;
# 添加数据
INSERT INTO user(username, password) VALUES('admin', 'password');

# 基本编译与运行
./build.sh
./bin/server
# 带参数编译与运行
./build.sh clean
./bin/server -h
./bin/server -d -p 1316 -e 3 -t 60000 -L -I -C 12 -T 8 -l -D 1 -q 1024
```

运行参数说明

```bash
 -p <port>          port
 -e <emm>           epoll mode : 0 LT + LT, 1 LT + ET, 2 ET + LT, 3 ET + ET
 -t <ms>            timeout ms
 -L                 enable linger
 -I                 enable IPv6
 -C <num>           mysql connection pool num
 -T <threadnum>     threadnum
 -l                 enable log
 -D <level>         log level : 0 DEBUG, 1 INFO, 2 WARN, 3 ERROR
 -q <capacity>      log que capacity
 -d                 run as a daemon
```

## 单元测试

```bash
cd test
make
./test
```

## 压力测试

```bash
./webbench-1.5/webbench -c 10000 -t 10 http://ip:port/
```

## TODO

* 完善单元测试
* 实现循环缓冲区
* 文件上传支持分批存储
* 完善请求方法 [PUT、DELETE、PATCH、HEAD、OPTIONS、…]
* 支持资源防盗链 [CORS 头部]
* 支持文件断点续传 [Range 标头]
* 支持HTTPS [SSL/TLS]
* 支持HTTP压缩 [Content-Encoding: gzip]  [Transfer-Encoding: chunked]

## 致谢

Linux高性能服务器编程，游双著.

[@qinguoyi](https://github.com/qinguoyi/TinyWebServer)
[@markparticle](https://github.com/markparticle/WebServer)

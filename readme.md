# WebServer
用C++实现的高性能WEB服务器，经过webbenchh压力测试可以实现上万的QPS

## 说明
* 基于原作者项目二次开发，修复若干BUG，添加新功能。

## 功能
* 利用IO复用技术Epoll与线程池实现多线程的Reactor高并发模型；
* 利用正则与状态机解析HTTP请求报文，实现处理静态资源的请求；
* 利用标准库容器封装char，实现自动增长的缓冲区；
* 基于小根堆实现的定时器，关闭超时的非活动连接；
* 利用单例模式与阻塞队列实现异步的日志系统，记录服务器运行状态；
* 利用RAII机制实现了数据库连接池，减少数据库连接建立与关闭的开销，同时实现了用户注册登录功能；
* 增加logsys,threadpool测试单元。

## 环境要求
* Linux
* C++14
* MySql [apt install mysql-server libmysqlclient-dev]
* Redis [apt install redis-server libhiredis-dev]

## 项目启动
需要先配置好对应的数据库
```bash
// 建立yourdb库
create database yourdb;

// 创建user表
USE yourdb;
CREATE TABLE user(
    username char(50) NULL,
    password char(50) NULL
)ENGINE=InnoDB;

// 添加数据
INSERT INTO user(username, password) VALUES('name', 'password');
```

```bash
./build.sh
./bin/server
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

## 致谢
Linux高性能服务器编程，游双著.

[@qinguoyi](https://github.com/qinguoyi/TinyWebServer)
[@markparticle](markparticle/WebServer)

/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */
#ifndef CONNPOOL_H
#define CONNPOOL_H

#include <queue>
#include <mutex>
#include <semaphore.h>
#include <mysql/mysql.h>
#include <hiredis/hiredis.h>

#include "log/log.h"

template <typename T>
class ConnPool
{
public:
    ConnPool() : MAX_CONN_(0), useCount_(0), freeCount_(0)
    {
        sem_init(&semId_, 0, 0);
    }

    virtual ~ConnPool() { sem_destroy(&semId_); }
    virtual bool InitPool(const char *host, int port, const char *user, const char *pwd, const char *dbName, int connSize) = 0;
    virtual void ClosePool() = 0;

    T *GetConn();
    void FreeConn(T *conn);
    int GetFreeConnCount();

protected:
    int MAX_CONN_;
    int useCount_;
    int freeCount_;

    std::queue<T *> connQue_;
    std::mutex mtx_;
    sem_t semId_;
};

template <typename T>
T *ConnPool<T>::GetConn()
{
    T *coon = nullptr;
    std::lock_guard<std::mutex> locker(mtx_);
    if (!connQue_.empty())
    {
        sem_wait(&semId_);
        coon = connQue_.front();
        connQue_.pop();
        freeCount_--;
        useCount_++;
    }
    else
    {
        LOG_WARN("ConnPool busy!");
        return nullptr;
    }
    return coon;
}

template <typename T>
void ConnPool<T>::FreeConn(T *conn)
{
    if (conn == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> locker(mtx_);
    connQue_.push(conn);
    freeCount_++;
    useCount_--;
    sem_post(&semId_);
}

template <typename T>
int ConnPool<T>::GetFreeConnCount()
{
    return freeCount_;
}

class MySQLConnPool : public ConnPool<MYSQL>
{
public:
    static MySQLConnPool *Instance()
    {
        static MySQLConnPool instance;
        return &instance;
    }

    ~MySQLConnPool() override
    {
        ClosePool();
        sem_destroy(&semId_);
    }

    bool InitPool(const char *host, int port, const char *user, const char *pwd, const char *dbName, int connSize) override
    {
        MAX_CONN_ = connSize;
        for (int i = 0; i < MAX_CONN_; i++)
        {
            MYSQL *sql = nullptr;
            sql = mysql_init(sql);
            if (!sql)
            {
                LOG_ERROR("MySql Init error!");
                return false;
            }
            sql = mysql_real_connect(sql, host, user, pwd, dbName, port, nullptr, 0);
            if (!sql)
            {
                LOG_ERROR("MySql Connect error!");
                return false;
            }
            connQue_.push(sql);
            freeCount_++;
        }
        sem_init(&semId_, 0, MAX_CONN_);
        return true;
    }

    void ClosePool() override
    {
        std::lock_guard<std::mutex> locker(mtx_);
        while (!connQue_.empty())
        {
            auto item = connQue_.front();
            connQue_.pop();
            mysql_close(item);
        }
        mysql_library_end();
        freeCount_ = 0;
        useCount_ = 0;
    }
};

class RedisConnPool : public ConnPool<redisContext>
{
public:
    static RedisConnPool *Instance()
    {
        static RedisConnPool instance;
        return &instance;
    }

    ~RedisConnPool() override
    {
        ClosePool();
        sem_destroy(&semId_);
    }

    bool InitPool(const char *host, int port, const char *user, const char *pwd, const char *dbName, int connSize) override
    {
        MAX_CONN_ = connSize;
        for (int i = 0; i < MAX_CONN_; i++)
        {
            redisContext *conn = redisConnect(host, port);
            if (conn == nullptr)
            {
                LOG_ERROR("Redis Connect error: can't allocate redis context!");
                return false;
            }
            else if (conn->err)
            {
                LOG_ERROR("Redis Connect error: %s", conn->errstr);
                return false;
            }

            // Authenticate with the Redis server if needed
            if (pwd)
            {
                std::string authCommand;
                if (user)
                {
                    authCommand = "AUTH " + std::string(user) + " " + std::string(pwd);
                }
                else
                {
                    authCommand = "AUTH " + std::string(pwd);
                }

                redisReply *authReply = (redisReply *)redisCommand(conn, authCommand.c_str());
                if (authReply == nullptr || authReply->type == REDIS_REPLY_ERROR)
                {
                    // Handle authentication error
                    LOG_ERROR("Redis Auth error: %s", authReply->str);
                    return false;
                }
                freeReplyObject(authReply);
            }

            // Select the specified database
            if (dbName)
            {
                redisReply *selectReply = (redisReply *)redisCommand(conn, "SELECT %s", dbName);
                if (selectReply == nullptr || selectReply->type == REDIS_REPLY_ERROR)
                {
                    // Handle SELECT error
                    LOG_ERROR("Redis Select error: %s", selectReply->str);
                    return false;
                }
                freeReplyObject(selectReply);
            }

            connQue_.push(conn);
            freeCount_++;
        }
        sem_init(&semId_, 0, MAX_CONN_);
        return true;
    }

    void ClosePool() override
    {
        std::lock_guard<std::mutex> locker(mtx_);
        while (!connQue_.empty())
        {
            auto item = connQue_.front();
            connQue_.pop();
            redisFree(item);
        }
        freeCount_ = 0;
        useCount_ = 0;
    }
};

#endif // CONNPOOL_H
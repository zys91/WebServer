/*
 * @Author       : mark
 * @Date         : 2020-06-19
 * @copyleft Apache 2.0
 */

#ifndef CONNRAII_H
#define CONNRAII_H

#include <assert.h>

#include "connpool.h"

/* 资源在对象构造初始化 资源在对象析构时释放*/
template <typename T>
class ConnRAII {
public:
    ConnRAII(T** conn, ConnPool<T>* connpool) {
        assert(connpool);
        *conn = connpool->GetConn();
        conn_ = *conn;
        connpool_ = connpool;
    }

    ~ConnRAII() {
        if (conn_) {
            connpool_->FreeConn(conn_);
        }
    }

private:
    T* conn_;
    ConnPool<T>* connpool_;
};

#endif // CONNRAII_H
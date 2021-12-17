#ifndef HTTP_PROXY_SERVER_WQ_H
#define HTTP_PROXY_SERVER_WQ_H

#include <queue>
#include <pthread.h>
#include "log.h"

class WQ
{
    pthread_cond_t cond;
    pthread_mutex_t lock;
    std::queue<LogMsg*> *msg_queue;

    static WQ *instance;
    WQ();

public:
    ~WQ();
    static WQ* getInstance();
    void push(LogMsg *msg);
    LogMsg *pop();
};

#endif

#include "wq.h"

#include <cstdlib>
#include <cstdio>

WQ* WQ::instance = nullptr;

WQ::WQ()
{
    this->msg_queue = new std::queue<LogMsg*>();
    pthread_mutex_init(&this->lock, nullptr);
    pthread_cond_init(&this->cond, nullptr);
}

WQ::~WQ()
{
    delete this->msg_queue;
}

WQ* WQ::getInstance()
{
    if (instance == nullptr)
        instance = new WQ();
    return instance;
}

LogMsg *WQ::pop()
{
    pthread_mutex_lock(&this->lock);

    while (this->msg_queue->empty())
    {
        pthread_cond_wait(&this->cond, &this->lock);
    }

    LogMsg *pMsg = this->msg_queue->front();
    this->msg_queue->pop();

    pthread_mutex_unlock(&this->lock);
    return pMsg;
}

void WQ::push(LogMsg *msg)
{
    pthread_mutex_lock(&this->lock);

    this->msg_queue->push(msg);

    pthread_cond_signal(&this->cond);
    pthread_mutex_unlock(&this->lock);
}

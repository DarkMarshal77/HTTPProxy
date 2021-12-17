#ifndef HTTP_PROXY_SERVER_LOG_H
#define HTTP_PROXY_SERVER_LOG_H

#include <cstdlib>
#include <pthread.h>
#include <arpa/inet.h>

extern pthread_mutex_t log_mutex;

#define LOG(fmt, args...)                              \
{                                                      \
    pthread_mutex_lock(&log_mutex);                    \
    printf("Thread %lu: ", pthread_self());           \
    printf(fmt, ##args);                               \
    pthread_mutex_unlock(&log_mutex);                  \
}

class LogMsg
{
public:
    int client_socket = 0, server_socket = 0;
    uint16_t client_port = 0, server_port = 0;
    char *client_addr = nullptr, *server_addr = nullptr;
    char *req = nullptr, *resp = nullptr;

    inline ~LogMsg()
    {
        if (this->client_addr)
            free(this->client_addr);
        if (this->server_addr)
            free(this->server_addr);
        if (this->req)
            free(this->req);
        if (this->resp)
            free(this->resp);
    }
};

#endif //HTTP_PROXY_SERVER_LOG_H

#include <arpa/inet.h>
#include <dirent.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"
#include "management.h"

#define PROXY_PORT          8090

using namespace std;

pthread_mutex_t log_mutex;
int num_threads = 16;

void connection_handler(void *input)
{
    LogMsg **args = (LogMsg **)input;
    LogMsg *msg = args[0], *client_to_server = args[1];
    int src_fd = client_to_server ? msg->client_socket : msg->server_socket;
    int dst_fd = client_to_server ? msg->server_socket : msg->client_socket;

    char *buffer = (char*) calloc(LIBHTTP_REQUEST_MAX_SIZE + 1, sizeof(char));

    if (client_to_server != nullptr)
    {
        struct http_request *request;
        while ((request = client_http_request_parse(src_fd, buffer, msg)) != nullptr)
        {
            if (request->client_req)
                http_start_request(dst_fd, request->method, request->path, request->version);
            http_send_data(dst_fd, buffer, strlen(buffer));
            free_request(request);
        }
        http_send_response(src_fd, 400);
    }
    else
    {
        size_t bytes_read = 0;
        while ((bytes_read = server_http_request_parse(src_fd, buffer, msg)) > 0)
        {
            http_send_data(dst_fd, buffer, bytes_read);
        }
    }

    free(buffer);
    shutdown(dst_fd, SHUT_RDWR);
    close(dst_fd);
}

int connect_to_target(const char* server_proxy_hostname, uint16_t server_proxy_port)
{
    struct sockaddr_in target_address;
    memset(&target_address, 0, sizeof(target_address));
    target_address.sin_family = AF_INET;
    target_address.sin_port = htons(server_proxy_port);

    int target_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (target_fd == -1)
    {
        fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
        return -1;
    }

    // set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    setsockopt(target_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);
    if (target_dns_entry == nullptr)
    {
        fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
        close(target_fd);
        return -1;
    }

    char *dns_address = target_dns_entry->h_addr_list[0];
    memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
    int connection_status = connect(target_fd, (struct sockaddr*) &target_address, sizeof(target_address));
    if (connection_status < 0)
    {
        close(target_fd);
        return -1;
    }

    return target_fd;
}

void handle_proxy_request(LogMsg* msg)
{
    // set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    setsockopt(msg->client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    timeout.tv_sec = 60;
    timeout.tv_usec = 0;
    setsockopt(msg->client_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    char *buffer = (char*) calloc(LIBHTTP_REQUEST_MAX_SIZE + 1, sizeof(char));
    struct http_request *request = client_http_request_parse(msg->client_socket, buffer, msg);
    if (request == nullptr || !request->client_req)
    {
        http_send_response(msg->client_socket, 400);
        free(buffer);
        close(msg->client_socket);
        delete(msg);
        return;
    }

    msg->server_socket = connect_to_target(request->host, request->port);
    if (msg->server_socket < 0)
    {
        http_send_response(msg->client_socket, 502);

        free_request(request);
        free(buffer);
        close(msg->client_socket);
        delete(msg);
        return;
    }
    http_start_request(msg->server_socket, request->method, request->path, request->version);
    http_send_data(msg->server_socket, buffer, strlen(buffer) + 1);
    free_request(request);
    free(buffer);

    LogMsg *t_c_args[2];
    t_c_args[0] = msg; t_c_args[1] = nullptr;
    pthread_t target_to_client_thread;
    pthread_create(&target_to_client_thread, nullptr, (void *(*)(void *))connection_handler, (void *)t_c_args);

    LogMsg *c_t_args[2];
    c_t_args[0] = msg; c_t_args[1] = (LogMsg*)1;
    connection_handler((void *)c_t_args);  // receive data from client and send it to target server

    pthread_join(target_to_client_thread, nullptr);
    delete(msg);
}

void worker_thread_loop(void *input)
{
    void (*request_handler)(LogMsg*) = (void (*)(LogMsg*))input;
    while (true)
    {
        LogMsg *msg = WQ::getInstance()->pop();
        request_handler(msg);
    }
}

void init_thread_pool(int num_threads)
{
    for (int i = 0; i < num_threads; i++)
    {
        pthread_t worker_thread;
        pthread_create(&worker_thread, nullptr, (void *(*)(void *))worker_thread_loop, (void*)handle_proxy_request);
    }
}

void serve_forever(int *socket_number)
{
    struct sockaddr_in server_address, client_address;
    size_t client_address_length = sizeof(client_address);
    int client_socket_number;

    *socket_number = socket(PF_INET, SOCK_STREAM, 0);
    if (*socket_number == -1)
    {
        perror("Failed to create a new socket");
        exit(errno);
    }

    int socket_option = 1;
    if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option, sizeof(socket_option)) == -1)
    {
        perror("Failed to set socket options.");
        exit(errno);
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
   server_address.sin_addr.s_addr = INADDR_ANY;
//     server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_address.sin_port = htons(PROXY_PORT);

    if (bind(*socket_number, (struct sockaddr *) &server_address, sizeof(server_address)) == -1)
    {
        perror("Failed to bind on socket");
        exit(errno);
    }

    if (listen(*socket_number, num_threads * 16) == -1)
    {
        perror("Failed to listen on socket");
        exit(errno);
    }

    LOG("Listening on port %d...\n", PROXY_PORT);

    WQ::getInstance();
    init_thread_pool(num_threads);

    while (true)
    {
        client_socket_number = accept(*socket_number, (struct sockaddr *)&client_address, (socklen_t *)&client_address_length);
        if (client_socket_number < 0)
        {
            perror("Error accepting socket");
            continue;
        }

        LOG("Accepted connection from %s on port %d\n", inet_ntoa(client_address.sin_addr), client_address.sin_port);

//        pthread_t worker_thread;
//        pthread_create(&worker_thread, nullptr, (void *(*)(void *))handle_proxy_request, (void*)(intptr_t)client_socket_number);
//        pthread_detach(worker_thread);

        LogMsg *msg = new LogMsg();
        msg->client_addr = (char*)calloc(strlen(inet_ntoa(client_address.sin_addr))+1, sizeof(char));
        msg->client_socket = client_socket_number;
        strcpy(msg->client_addr, inet_ntoa(client_address.sin_addr));
        msg->client_port = client_address.sin_port;

        WQ::getInstance()->push(msg);
    }

    shutdown(*socket_number, SHUT_RDWR);
    close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum)
{
    printf("Caught signal %d: %s\n", signum, strsignal(signum));
    if (signum == SIGINT)
    {
        printf("Closing socket %d\n", server_fd);
        if (close(server_fd) < 0)
            perror("Failed to close server_fd (ignoring)\n");
        exit(0);
    }
    else if (signum == SIGSEGV)
    {
        close(server_fd);
        exit(-1);
    }
}


int main(int argc, char **argv)
{
    signal(SIGINT, signal_callback_handler);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGSEGV, signal_callback_handler);

    pthread_mutex_init(&log_mutex, nullptr);
    Management::getInstance();

    pthread_t pthread;
    pthread_create(&pthread, nullptr, (void *(*)(void *))Management::handle_requests, nullptr);

    serve_forever(&server_fd);

    pthread_join(pthread, nullptr);

    return EXIT_SUCCESS;
}

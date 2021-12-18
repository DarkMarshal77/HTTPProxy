#include "management.h"

#include <cstring>
#include <algorithm>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "log.h"

using namespace std;

Management::Management()
{
    this->client_pkt_len = RunningStat();
    this->server_pkt_len = RunningStat();
    this->server_bd_len = RunningStat();

    pthread_mutex_init(&this->management_lock, nullptr);
}
Management* Management::instance = nullptr;

Management *Management::getInstance()
{
    if (instance == nullptr)
        instance = new Management();
    return instance;
}

bool cmp(pair<string, uint32_t>& a, pair<string, uint32_t>& b)
{
    return a.second > b.second;
}

Management::Types Management::http_get_mime_type(char *buffer)
{
    char *content_type = strstr(buffer, "Content-Type: ");
    if (content_type == nullptr)
    {
        return NOTHING;
    }

    content_type += strlen("Content-Type: ");
    if (strstr(content_type, "text/html") == content_type)
    {
        return HTML;
    }
    else if (strstr(content_type, "image/jpg") == content_type)
    {
        return JPG;
    }
    else if (strstr(content_type, "image/jpeg") == content_type)
    {
        return JPEG;
    }
    else if (strstr(content_type, "image/png") == content_type)
    {
        return PNG;
    }
    else if (strstr(content_type, "text/css") == content_type)
    {
        return CSS;
    }
    else if (strstr(content_type, "application/javascript") == content_type)
    {
        return JS;
    }
    else if (strstr(content_type, "application/pdf") == content_type)
    {
        return PDF;
    }
    else
    {
        return PLAIN;
    }
}

const char *Management::http_get_mime_type_str(Management::Types types)
{
    switch (types)
    {
        case PLAIN:
            return "text/plain";
        case HTML:
            return "text/html";
        case JPG:
            return "image/jpg";
        case JPEG:
            return "image/jpeg";
        case PNG:
            return "image/png";
        case CSS:
            return "text/css";
        case PDF:
            return "application/pdf";
        case JS:
            return "application/javascript";
        default:
            return "unknown type";
    }
}

void Management::sort_host_count(vector<pair<string, uint32_t>>& A)
{
    A.clear();

    for (auto& it : this->host_count)
    {
        A.push_back(it);
    }

    sort(A.begin(), A.end(), cmp);
}

void Management::packet_len_stats(int fd)
{
    pthread_mutex_lock(&this->management_lock);
    dprintf(fd, "Packet length received from servers(mean, std): (%f, %f)\n",
            this->server_pkt_len.Mean(), this->server_pkt_len.StandardDeviation());
    dprintf(fd, "Packet length received from clients(mean, std): (%f, %f)\n",
            this->client_pkt_len.Mean(), this->client_pkt_len.StandardDeviation());
    dprintf(fd, "Body length received from servers(mean, std): (%f, %f)\n",
            this->server_bd_len.Mean(), this->server_bd_len.StandardDeviation());
    pthread_mutex_unlock(&this->management_lock);
}

void Management::type_cnt(int fd)
{
    pthread_mutex_lock(&this->management_lock);
    for (auto it: this->type_count)
        dprintf(fd, "%s: %d\n", this->http_get_mime_type_str(it.first), it.second);
    pthread_mutex_unlock(&this->management_lock);
}

void Management::status_cnt(int fd)
{
    pthread_mutex_lock(&this->management_lock);
    for (auto it: this->status_count)
        dprintf(fd, "%d %s: %d\n", it.first, http_get_response_message(it.first), it.second);
    pthread_mutex_unlock(&this->management_lock);
}

void Management::top_visited_hosts(int fd, size_t k)
{
    vector<pair<string, uint32_t>> sorted_hosts;

    pthread_mutex_lock(&this->management_lock);
    this->sort_host_count(sorted_hosts);
    pthread_mutex_unlock(&this->management_lock);

    k = k > sorted_hosts.size() ? sorted_hosts.size() : k;
    for (int i = 0; i < k; i++)
        dprintf(fd, "%s\n", sorted_hosts[i].first.c_str());
}

void Management::handle_requests(void *input)
{
    struct sockaddr_in server_address, client_address;
    size_t client_address_length = sizeof(client_address);

    instance->management_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (instance->management_socket == -1)
    {
        perror("Failed to create a new socket");
        exit(errno);
    }

    int socket_option = 1;
    if (setsockopt(instance->management_socket, SOL_SOCKET, SO_REUSEADDR, &socket_option, sizeof(socket_option)) == -1)
    {
        perror("Failed to set socket options.");
        exit(errno);
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
//    server_address.sin_addr.s_addr = INADDR_ANY;
     server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_address.sin_port = htons(MANAGEMENT_PORT);

    if (bind(instance->management_socket, (struct sockaddr *) &server_address, sizeof(server_address)) == -1)
    {
        perror("Failed to bind on socket");
        exit(errno);
    }

    if (listen(instance->management_socket, 1) == -1)
    {
        perror("Failed to listen on socket");
        exit(errno);
    }

    LOG("Listening on port %d...\n", MANAGEMENT_PORT);

    while (true)
    {
        int fd = accept(instance->management_socket, (struct sockaddr *) &client_address, (socklen_t *) &client_address_length);
        if (fd < 0)
        {
            perror("Error accepting socket");
            continue;
        }

        LOG("Accepted connection from %s on port %d\n", inet_ntoa(client_address.sin_addr), client_address.sin_port);

        while (true)
        {
            char *buffer = (char*)calloc(128 + 1, sizeof(char));

            size_t read_bytes = http_receive_data(fd, buffer);
            if (read_bytes == 0)
            {
                perror("Error reading message.");
                break;
            }

            buffer[read_bytes - 2] = '\0';
            if (strstr(buffer, "packet length stats"))
            {
                instance->packet_len_stats(fd);
            }
            else if (strstr(buffer, "type count"))
            {
                instance->type_cnt(fd);
            }
            else if (strstr(buffer, "status count"))
            {
                instance->status_cnt(fd);
            }
            else if (strstr(buffer, "top"))
            {
                size_t k = 0;
                char *tmp = strchr(buffer, ' ');
                if (tmp != nullptr)
                {
                    *tmp = '\0';
                    k = (size_t)atoi(tmp + 1);
                }

                instance->top_visited_hosts(fd, k);
            }
            else if (strstr(buffer, "exit"))
            {
                dprintf(fd, "Bye\n");
                free(buffer);
                break;
            }
            else
            {
                dprintf(fd, "Bad Request\n");
            }

            free(buffer);
        }

        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}

void Management::handle_stats(char *buffer, struct http_request *request, LogMsg *msg)
{
    int msg_len = -1, header_len = 0;

    /* extract header len */
    char *header_end = strstr(buffer, "\r\n\r\n") + 4;
    if (strstr(header_end, "\r\n") == header_end)
        header_end += 2;
    header_len = (int)(header_end - buffer);

    /* extract body length */
    msg_len = 0;
    char *content_len = strstr(buffer, "Content-Length: ");
    if (content_len)
    {
        content_len += strlen("Content-Length: ");
        size_t len = 0;
        while (*content_len != '\r')
        {
            content_len++;
            len++;
        }
        char msg_len_str[len];
        strncpy(msg_len_str, content_len - len, len);
        msg_len = atoi(msg_len_str);
    }

    if (request->client_req)
    {
        {
            if (msg->req)
                free(msg->req);
            msg->req = (char *) calloc(strlen(request->method) + strlen(request->path) + strlen(request->version) + 3, sizeof(char));
            sprintf(msg->req, "%s %s %s", request->method, request->path, request->version);
            msg->server_port = request->port;
            if (msg->server_addr)
                free(msg->server_addr);
            msg->server_addr = (char *) calloc(strlen(request->host) + 1, sizeof(char));
            strcpy(msg->server_addr, request->host);

            time_t now = time(nullptr);
            struct tm *ptm = gmtime(&now);
            char time_buf[256] = {0};
            strftime(time_buf, 256, "%a, %d %b %G %T %Z", ptm);

            LOG("\r\nRequest: [%s] [%s:%d] [%s:%d]\n\"%s\"\r\n",
                time_buf, msg->client_addr, msg->client_port, msg->server_addr, msg->server_port, msg->req);
        }

        pthread_mutex_lock(&this->management_lock);
        this->client_pkt_len.Push(header_len + msg_len);
        this->host_count[request->host]++;
        pthread_mutex_unlock(&this->management_lock);
    }
    else
    {
        /* extract status code */
        int status_code = 0;
        char *status_code_str_begin = buffer + strlen("HTTP/1.") + 2;
        char *status_code_str_end = strchr(status_code_str_begin, ' ');
        char status_code_str[status_code_str_end - status_code_str_begin + 1] = {0};
        strncpy(status_code_str, status_code_str_begin, status_code_str_end - status_code_str_begin);
        status_code = atoi(status_code_str);

        /* extract type */
        Types types = this->http_get_mime_type(buffer);

        /* print response */
        if (status_code > 0)
        {
            time_t now = time(nullptr);
            struct tm *ptm = gmtime(&now);
            char time_buf[256] = {0};
            strftime(time_buf, 256, "%a, %d %b %G %T %Z", ptm);

            LOG("\r\nResponse: [%s] [%s:%d] [%s:%d]\n\"HTTP/1.1 %d %s\" for \"%s\"\r\n",
                time_buf, msg->client_addr, msg->client_port, msg->server_addr, msg->server_port,
                status_code, http_get_response_message(status_code), msg->req);
        }

        /* update stats */
        pthread_mutex_lock(&this->management_lock);
        if (status_code > 0)
            this->status_count[(StatusCode)status_code]++;
        if (types != NOTHING)
            this->type_count[types]++;
        this->server_pkt_len.Push(header_len + msg_len);
        this->server_bd_len.Push(msg_len);
        pthread_mutex_unlock(&this->management_lock);
    }
}

Management::~Management()
{
    shutdown(this->management_socket, SHUT_RDWR);
    close(this->management_socket);
}

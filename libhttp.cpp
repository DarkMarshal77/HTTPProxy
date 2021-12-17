#include "libhttp.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "management.h"

void free_request(struct http_request *request)
{
    if (request->method)
        free(request->method);
    if (request->path)
        free(request->path);
    if (request->version)
        free(request->version);
    if (request->host)
        free(request->host);
    free(request);
}

struct http_request *client_http_request_parse(int fd, char *buffer, LogMsg *msg)
{
    struct http_request *request = (http_request*) calloc(1, sizeof(struct http_request));
    if (request == nullptr)
    {
        fprintf(stderr, "Malloc failed\n");
        exit(ENOBUFS);
    }

    char *read_buffer = (char*) calloc(LIBHTTP_REQUEST_MAX_SIZE + 1, sizeof(char));
    if (read_buffer == nullptr)
    {
        fprintf(stderr, "Malloc failed\n");
        exit(ENOBUFS);
    }

    size_t bytes_read;
    if ((bytes_read = http_receive_data(fd, read_buffer)) == 0)
    {
        free_request(request);
        free(read_buffer);
        return nullptr;
    }

    /* return if the buffer dose'nt have header */
    char *tmp = strstr(read_buffer, " HTTP/1.");
    if (tmp == nullptr)
    {
        memcpy(buffer, read_buffer, bytes_read);
        free(read_buffer);
        return request;
    }

    char *read_start, *read_end;
    size_t read_size;
    do
    {
        /* Read in the HTTP method: "[A-Z]*" */
        read_start = read_end = read_buffer;
        while (*read_end >= 'A' && *read_end <= 'Z')
            read_end++;
        read_size = read_end - read_start;
        if (read_size == 0)
            break;
        request->method = (char*) malloc(read_size + 1);
        memcpy(request->method, read_start, read_size);
        request->method[read_size] = '\0';

        /* Read in a space character. */
        read_start = read_end;
        if (*read_end != ' ')
            break;
        read_end++;

        /* Read in the path: "[^ \r\n]*" */
        read_start = read_end;
        while (*read_end != '\0' && *read_end != ' ' && *read_end != '\n' && *read_end != '\r')
            read_end++;
        read_size = read_end - read_start;
        if (read_size == 0)
            break;
        request->path = (char*) malloc(read_size + 1);
        memcpy(request->path, read_start, read_size);
        request->path[read_size] = '\0';

        /* Read in a space character. */
        read_start = read_end;
        if (*read_end != ' ')
            break;
        read_end++;

        /* Read in the HTTP version: "[^ \r\n]*" */
        read_start = read_end;
        while (*read_end != '\0' && *read_end != ' ' && *read_end != '\n' && *read_end != '\r')
            read_end++;
        read_size = read_end - read_start;
        if (read_size == 0)
            break;
        request->version = (char*) malloc(read_size + 1);
        memcpy(request->version, read_start, read_size);
        request->version[read_size] = '\0';

        /* Read in rest of request line: ".*" */
        read_start = read_end;
        while (*read_end != '\0' && *read_end != '\n')
            read_end++;
        if (*read_end != '\n')
            break;
        read_end++;

        /* copy rest of buffer */
        read_size = read_end - read_buffer;
        memcpy(buffer, read_end, bytes_read - read_size + 1);

        /* Read in the host: "[^ \r\n]*" */
        read_end = strstr(read_end, "Host: ");
        if (read_end == nullptr)
            break;
        read_start = read_end = read_end + strlen("Host: ");
        while (*read_end != '\0' && *read_end != ' ' && *read_end != '\n' && *read_end != '\r')
            read_end++;
        read_size = read_end - read_start;
        if (read_size == 0)
            break;
        request->host = (char*) malloc(read_size + 1);
        memcpy(request->host, read_start, read_size);
        request->host[read_size] = '\0';

        /* remove host from path */
        read_start = strstr(request->path, request->host);
        if (read_start != nullptr)
        {
            read_start += strlen(request->host);
            read_end = read_start;
            while (*read_end != '\0')
                read_end++;
            read_size = read_end - read_start;
            if (read_size == 0 && strcmp(request->method, "CONNECT") != 0)
                break;

            if (strcmp(request->method, "CONNECT") != 0)
            {
                char *tmp = (char*)calloc(read_size + 1, sizeof(char));
                memcpy(tmp, read_start, read_size);

                free(request->path);
                request->path = (char*) malloc(read_size + 1);
                memcpy(request->path, tmp, read_size + 1);
                free(tmp);
            }
        }

        /* extract port from host */
        request->port = 80;
        if (strchr(request->host, ':'))
        {
            char *colon = strchr(request->host, ':');
            *colon = '\0';
            request->port = (uint16_t)atoi(colon + 1);
        }

        request->client_req = true;
        Management::getInstance()->handle_stats(read_buffer, request, msg);

        free(read_buffer);
        return request;
    }
    while (0);

    /* An error occurred. */
//    LOG("\nError in parsing packet:\n%s\n", read_buffer);
    free_request(request);
    free(read_buffer);
    return nullptr;
}

size_t server_http_request_parse(int fd, char *buffer, LogMsg *msg)
{
    struct http_request request;
    request.client_req = false;

    size_t bytes_read;
    if ((bytes_read = http_receive_data(fd, buffer)) == 0)
    {
        return 0;
    }

    if (strstr(buffer, "HTTP/1.") == buffer)
        Management::getInstance()->handle_stats(buffer, &request, msg);
    return bytes_read;
}

const char* http_get_response_message(int status_code)
{
    switch (status_code)
    {
        case CONTINUE:
            return "Continue";
        case OK:
            return "OK";
        case MOVED_PERMANENTLY:
            return "Moved Permanently";
        case FOUND:
            return "Found";
        case NOT_MODIFIED:
            return "Not Modified";
        case BAD_REQUEST:
            return "Bad Request";
        case UNAUTHORIZED:
            return "Unauthorized";
        case FORBIDDEN:
            return "Forbidden";
        case NOT_FOUND:
            return "Not Found";
        case METHOD_NOT_ALLOWED:
            return "Method Not Allowed";
        case NOT_IMPLEMENTED:
            return "Not Implemented";
        case BAD_GATEWAY:
            return "Bad Gateway";
        default:
            return "Internal Server Error";
    }
}

void http_start_response(int fd, int status_code)
{
    dprintf(fd, "HTTP/1.0 %d %s\r\n", status_code, http_get_response_message(status_code));
}

void http_start_request(int fd, char *method, char *path, char *version)
{
    dprintf(fd, "%s %s %s\r\n", method, path, version);
}

void http_send_header(int fd, const char *key, const char *value)
{
    dprintf(fd, "%s: %s\r\n", key, value);
}

void http_end_headers(int fd)
{
    dprintf(fd, "\r\n");
}

void http_send_string(int fd, const char *data)
{
    http_send_data(fd, data, strlen(data));
}

void http_send_data(int fd, const char *data, size_t size)
{
    ssize_t bytes_sent;
    while (size > 0)
    {
        bytes_sent = write(fd, data, size);
        if (bytes_sent < 0)
            return;
        size -= bytes_sent;
        data += bytes_sent;
    }
}

size_t http_receive_data(int fd, char *buffer)
{
    size_t bytes_read = 0;
    ssize_t tmp;
    if ((tmp = read(fd, buffer, LIBHTTP_REQUEST_MAX_SIZE)) > 0)
        bytes_read = (size_t)tmp;
    buffer[bytes_read] = '\0';
    return bytes_read;
}

void http_send_response(int fd, int status_code)
{
    char *msg = (char*)calloc(128, sizeof(char));
    sprintf(msg, "<center><h1>%d %s</h1><hr></center>", status_code, http_get_response_message(status_code));

    http_start_response(fd, status_code);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, msg);

    free(msg);
}

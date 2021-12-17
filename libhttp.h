#ifndef HTTP_PROXY_SERVER_LIBHTTP_H
#define HTTP_PROXY_SERVER_LIBHTTP_H

#include "log.h"

#define LIBHTTP_REQUEST_MAX_SIZE 8192

enum StatusCode
{
    CONTINUE = 100,
    OK = 200,
    MOVED_PERMANENTLY = 301, FOUND = 302, NOT_MODIFIED = 304,
    BAD_REQUEST = 400, UNAUTHORIZED = 401, FORBIDDEN = 403, NOT_FOUND = 404, METHOD_NOT_ALLOWED = 405,
    NOT_IMPLEMENTED = 501, BAD_GATEWAY = 502
};

struct http_request
{
    char *method;
    char *path;
    char *version;

    char *host;
    uint16_t port;

    bool client_req;
};

void free_request(struct http_request *request);

const char* http_get_response_message(int status_code);

struct http_request *client_http_request_parse(int fd, char *buffer, LogMsg *msg);

size_t server_http_request_parse(int fd, char *buffer, LogMsg *msg);

void http_start_response(int fd, int status_code);
void http_start_request(int fd, char *method, char *path, char *version);

void http_send_header(int fd, const char *key, const char *value);
void http_end_headers(int fd);
void http_send_string(int fd, const char *data);
void http_send_data(int fd, const char *data, size_t size);

size_t http_receive_data(int fd, char *buffer);

void http_send_response(int fd, int status_code);

#endif

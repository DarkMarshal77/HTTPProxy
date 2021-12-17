#ifndef HTTP_PROXY_SERVER_MANAGEMENT_H
#define HTTP_PROXY_SERVER_MANAGEMENT_H

#include <cstdint>
#include <vector>
#include <map>
#include <string>
#include <pthread.h>
#include <cmath>

#include "libhttp.h"
#include "log.h"

#define MANAGEMENT_PORT     8091

class RunningStat
{
public:
    RunningStat() : m_n(0) {}

    void Clear()
    {
        m_n = 0;
    }

    void Push(double x)
    {
        m_n++;

        // See Knuth TAOCP vol 2, 3rd edition, page 232
        if (m_n == 1)
        {
            m_oldM = m_newM = x;
            m_oldS = 0.0;
        }
        else
        {
            m_newM = m_oldM + (x - m_oldM)/m_n;
            m_newS = m_oldS + (x - m_oldM)*(x - m_newM);

            // set up for next iteration
            m_oldM = m_newM;
            m_oldS = m_newS;
        }
    }

    int NumDataValues() const
    {
        return m_n;
    }

    double Mean()
    {
        return (m_n > 0) ? m_newM : 0.0;
    }

    double Variance()
    {
        return ( (m_n > 1) ? m_newS/(m_n - 1) : 0.0 );
    }

    double StandardDeviation()
    {
        return sqrt( Variance() );
    }

private:
    uint32_t m_n;
    double m_oldM, m_newM, m_oldS, m_newS;
};

class Management
{
private:
    enum Types {PLAIN, HTML, JPG, JPEG, PNG, CSS, JS, PDF, NOTHING};

    RunningStat client_pkt_len, server_pkt_len, server_bd_len;
    int management_socket{};
    std::map<StatusCode, uint32_t> status_count;
    std::map<Types, uint32_t> type_count;
    std::map<std::string, uint32_t> host_count;

    pthread_mutex_t management_lock{};

    static Management *instance;
    Management();
    ~Management();
    static Types http_get_mime_type(char *buffer);
    static const char *http_get_mime_type_str(Management::Types);
    void sort_host_count(std::vector<std::pair<std::string, uint32_t>>& A);

    void packet_len_stats(int fd);
    void type_cnt(int fd);
    void status_cnt(int fd);
    void top_visited_hosts(int fd, size_t k);

public:
    static Management* getInstance();
    static void handle_requests(void *input);
    void handle_stats(char *buffer, struct http_request *request, LogMsg *msg);
};

#endif //HTTP_PROXY_SERVER_MANAGEMENT_H

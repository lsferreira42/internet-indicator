#include "ping.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <netdb.h>

static uint16_t icmp_checksum(const void *data, size_t len)
{
    const uint16_t *p = data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1)
        sum += *(const uint8_t *)p;

    sum  = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);

    return (uint16_t)~sum;
}

PingResult ping_host(const char *host, int timeout_sec)
{
    PingResult result = { false, -1.0, "" };
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        struct addrinfo *res = NULL;
        if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) {
            snprintf(result.error_msg, sizeof(result.error_msg),
                     "Cannot resolve hostname: %s", host);
            fprintf(stderr, "internet-indicator: %s\n", result.error_msg);
            return result;
        }
        addr.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }

    bool is_dgram = false;
    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        is_dgram = true;
        if (fd < 0) {
            snprintf(result.error_msg, sizeof(result.error_msg),
                     "ICMP socket failed: %s (hint: run with CAP_NET_RAW)", strerror(errno));
            fprintf(stderr, "internet-indicator: %s\n", result.error_msg);
            return result;
        }
    }

    struct icmphdr icmp;
    memset(&icmp, 0, sizeof(icmp));
    static uint16_t seq = 1;
    uint16_t current_seq = seq;
    icmp.type             = ICMP_ECHO;
    icmp.code             = 0;
    icmp.un.echo.id       = htons((uint16_t)(getpid() & 0xffff));
    icmp.un.echo.sequence = htons(seq++);
    icmp.checksum         = 0;
    icmp.checksum         = icmp_checksum(&icmp, sizeof(icmp));

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    ssize_t n = sendto(fd, &icmp, sizeof(icmp), 0,
                       (struct sockaddr *)&addr, sizeof(addr));
    if (n < 0) {
        close(fd);
        return result;
    }

    struct timespec t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    long time_left_ms = timeout_sec * 1000;

    while (time_left_ms > 0) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int ret = poll(&pfd, 1, time_left_ms);

        if (ret <= 0) {
            break; // timeout or error
        }

        char buf[1024];
        n = recv(fd, buf, sizeof(buf), 0);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        
        if (n >= 0) {
            struct icmphdr *reply = NULL;
            if (n >= (ssize_t)(sizeof(struct iphdr) + sizeof(struct icmphdr))) {
                struct iphdr *ip = (struct iphdr *)buf;
                if (ip->version == 4 && ip->protocol == IPPROTO_ICMP) {
                    if (n >= (ssize_t)(ip->ihl * 4 + sizeof(struct icmphdr))) {
                        reply = (struct icmphdr *)(buf + (ip->ihl * 4));
                    }
                }
            }
            if (!reply && n >= (ssize_t)sizeof(struct icmphdr)) {
                reply = (struct icmphdr *)buf;
            }

            if (reply && reply->type == ICMP_ECHOREPLY) {
                bool match_id = is_dgram || (reply->un.echo.id == htons((uint16_t)(getpid() & 0xffff)));
                if (match_id && reply->un.echo.sequence == htons(current_seq)) {
                    result.ok = true;
                    result.latency_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
                    break;
                }
            }
        }

        // recalculate time left
        struct timespec t_now;
        clock_gettime(CLOCK_MONOTONIC, &t_now);
        long elapsed_ms = (t_now.tv_sec - t0.tv_sec) * 1000 + (t_now.tv_nsec - t0.tv_nsec) / 1000000;
        time_left_ms = (timeout_sec * 1000) - elapsed_ms;
    }

    close(fd);

    return result;
}

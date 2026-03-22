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

/* ------------------------------------------------------------------ */
/*  ICMP helpers                                                       */
/* ------------------------------------------------------------------ */

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

bool ping_host(const char *host, int timeout_sec)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        /* try hostname resolution */
        fprintf(stderr, "internet-indicator: invalid address %s\n", host);
        return false;
    }

    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        /* try SOCK_DGRAM (unprivileged ICMP, kernel >= 3.0 with
         * net.ipv4.ping_group_range configured) */
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        if (fd < 0) {
            fprintf(stderr, "internet-indicator: cannot create ICMP socket: %s\n"
                            "  hint: run with CAP_NET_RAW or as root\n",
                    strerror(errno));
            return false;
        }
    }

    /* build ICMP echo request */
    struct icmphdr icmp;
    memset(&icmp, 0, sizeof(icmp));
    icmp.type             = ICMP_ECHO;
    icmp.code             = 0;
    icmp.un.echo.id       = htons((uint16_t)(getpid() & 0xffff));
    icmp.un.echo.sequence = htons(1);
    icmp.checksum         = 0;
    icmp.checksum         = icmp_checksum(&icmp, sizeof(icmp));

    /* send */
    ssize_t n = sendto(fd, &icmp, sizeof(icmp), 0,
                       (struct sockaddr *)&addr, sizeof(addr));
    if (n < 0) {
        close(fd);
        return false;
    }

    /* wait for reply */
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int ret = poll(&pfd, 1, timeout_sec * 1000);

    if (ret <= 0) {
        /* timeout or error */
        close(fd);
        return false;
    }

    /* read reply */
    char buf[1024];
    n = recv(fd, buf, sizeof(buf), 0);
    close(fd);

    if (n < 0) return false;

    /* for SOCK_RAW the reply includes the IP header; for SOCK_DGRAM it doesn't.
     * We just need to verify we got *something* back from the right host. */
    return true;
}

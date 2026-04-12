#ifndef PING_H
#define PING_H

#include <stdbool.h>

typedef struct {
    bool ok;
    double latency_ms;
    char error_msg[256];
} PingResult;

/* Sends an ICMP Echo Request to `host` and waits up to `timeout_sec` for a reply.
 * Returns a PingResult with ok = true if a valid Echo Reply was received, and latency_ms > 0.
 * If unable to resolve, or timed out, ok = false. */
PingResult ping_host(const char *host, int timeout_sec);

#endif /* PING_H */

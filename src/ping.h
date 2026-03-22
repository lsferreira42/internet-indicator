#ifndef PING_H
#define PING_H

#include <stdbool.h>

/* Send an ICMP echo request to `host` and wait up to `timeout_sec`
 * for a reply.  Returns true if a valid echo reply was received. */
bool ping_host(const char *host, int timeout_sec);

#endif /* PING_H */

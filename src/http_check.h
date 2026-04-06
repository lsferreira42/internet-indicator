#ifndef HTTP_CHECK_H
#define HTTP_CHECK_H

#include <stdbool.h>

/* Perform an HTTP(S) request to `url` and check the response status code.
 *   port           – override port (0 = use default from URL scheme)
 *   verify_ssl     – if true, verify the server's SSL certificate
 *   acceptable_codes – comma-separated list of acceptable HTTP status codes (e.g. "200,301")
 *   headers        – newline-separated "name=value" pairs (may be NULL or empty)
 *   timeout_sec    – request timeout in seconds
 * Returns true if the response code matches one of the acceptable codes. */
bool http_check_host(const char *url, int port, bool verify_ssl,
                     const char *acceptable_codes, const char *headers,
                     const char *method, int timeout_sec);

#endif /* HTTP_CHECK_H */

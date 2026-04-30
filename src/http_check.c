#include "http_check.h"
#include "logger.h"

#ifdef HAVE_LIBCURL

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool is_http_url(const char *url)
{
    return url &&
           (strncasecmp(url, "http://", 7) == 0 ||
            strncasecmp(url, "https://", 8) == 0);
}

/* Discard response body */
static size_t discard_body(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    (void)ptr;
    (void)userdata;
    return size * nmemb;
}

/* Check if `code` appears in `acceptable_codes` (comma-separated, e.g. "200,301,302") */
static bool is_code_acceptable(long code, const char *acceptable_codes)
{
    if (!acceptable_codes || acceptable_codes[0] == '\0')
        return code >= 200 && code < 400; /* default: any 2xx/3xx */

    char buf[256];
    strncpy(buf, acceptable_codes, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *tok = strtok_r(buf, ",", &saveptr);
    while (tok) {
        /* trim whitespace */
        while (*tok == ' ' || *tok == '\t') tok++;
        long c = strtol(tok, NULL, 10);
        if (c == code) return true;
        tok = strtok_r(NULL, ",", &saveptr);
    }
    return false;
}

/* Parse "name=value\nname=value\n..." into a curl_slist */
static struct curl_slist *parse_headers(const char *headers)
{
    if (!headers || headers[0] == '\0') return NULL;

    struct curl_slist *list = NULL;
    char buf[2048];
    strncpy(buf, headers, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line) {
        /* trim CR if present */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
            line[--len] = '\0';

        /* must have '=' separator – convert to ': ' for HTTP */
        if (len > 0) {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                char header[512];
                snprintf(header, sizeof(header), "%s: %s", line, eq + 1);
                list = curl_slist_append(list, header);
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    return list;
}

PingResult http_check_host(const char *url, int port, bool verify_ssl,
                     const char *acceptable_codes, const char *headers,
                     const char *method, int timeout_sec)
{
    PingResult result = { false, -1.0, "" };

    if (!is_http_url(url)) {
        snprintf(result.error_msg, sizeof(result.error_msg),
                 "Invalid HTTP URL scheme (only http:// and https:// are allowed)");
        log_msg(LOG_ERROR, "%s", result.error_msg);
        return result;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(result.error_msg, sizeof(result.error_msg), "Failed to init libcurl");
        log_msg(LOG_ERROR, "%s", result.error_msg);
        return result;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout_sec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)timeout_sec);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

    /* Set HTTP method */
    if (method && strcasecmp(method, "HEAD") == 0) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    } else if (method && strcasecmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    } else {
        /* default: GET */
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    if (!verify_ssl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    if (port > 0) {
        curl_easy_setopt(curl, CURLOPT_PORT, (long)port);
    }

    struct curl_slist *header_list = parse_headers(headers);
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        long response_code = 0;
        double total_time = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
        result.ok = is_code_acceptable(response_code, acceptable_codes);
        if (result.ok) {
            result.latency_ms = total_time * 1000.0;
        }
    } else {
        snprintf(result.error_msg, sizeof(result.error_msg), "HTTP check failed: %s", curl_easy_strerror(res));
        log_msg(LOG_ERROR, "%s", result.error_msg);
    }

    if (header_list) curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return result;
}

#else /* !HAVE_LIBCURL */

PingResult http_check_host(const char *url, int port, bool verify_ssl,
                     const char *acceptable_codes, const char *headers,
                     const char *method, int timeout_sec)
{
    (void)url; (void)port; (void)verify_ssl;
    (void)acceptable_codes; (void)headers;
    (void)method; (void)timeout_sec;
    PingResult result = { false, -1.0, "HTTP check not available (built without libcurl)" };
    log_msg(LOG_ERROR, "%s", result.error_msg);
    return result;
}

#endif /* HAVE_LIBCURL */

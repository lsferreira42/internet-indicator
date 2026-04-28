#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>
#include <gio/gio.h>
#include <stdbool.h>

#define CONFIG_DIR          ".config/internet-indicator"
#define CONFIG_FILE         "config"
#define DEFAULT_ADDRESS     "8.8.8.8"
#define DEFAULT_INTERVAL    30
#define DEFAULT_LOG_ENABLED false
#define DEFAULT_SLEEP_DETECTION_ENABLED true
#define DEFAULT_LOCK_DETECTION_ENABLED true
#define DEFAULT_MAX_RETRIES 3
#define DEFAULT_RETRY_DELAY 5
#define DEFAULT_DEBUG       false

/* HTTP defaults */
#define DEFAULT_HTTP_URL              "https://www.google.com"
#define DEFAULT_HTTP_PORT             0
#define DEFAULT_HTTP_VERIFY_SSL       true
#define DEFAULT_HTTP_ACCEPTABLE_CODES "200"
#define DEFAULT_HTTP_HEADERS          ""
#define DEFAULT_HTTP_METHOD           "GET"
#define DEFAULT_HTTP_TIMEOUT          10

typedef enum {
    CHECK_MODE_ICMP = 0,
    CHECK_MODE_HTTP = 1
} CheckMode;

#define DEFAULT_CHECK_MODE CHECK_MODE_ICMP

typedef struct {
    char address[256];
    int  interval;          /* seconds between pings */
    bool log_enabled;       /* UP/DOWN logging toggle */
    bool debug;             /* print to stdout when true */
    bool sleep_detection_enabled; /* pause pings during sleep */
    bool lock_detection_enabled;  /* pause pings during screen lock */
    bool notify_enabled;    /* desktop notifications toggle */
    char log_file_path[512];/* output path for .log */
    int  log_max_size_kb;   /* max log file size for rotation */
    int  max_retries;       /* number of consecutive failures before DOWN */
    int  retry_delay;       /* seconds to wait between retries */

    CheckMode check_mode;   /* icmp or http */

    /* HTTP mode settings */
    char http_url[512];             /* URL to check */
    int  http_port;                 /* port (0 = default from URL scheme) */
    bool http_verify_ssl;           /* verify SSL certificates */
    char http_acceptable_codes[256]; /* e.g. "200,301,302" */
    char http_headers[2048];        /* "name=value\nname=value\n..." */
    char http_method[16];           /* GET, HEAD, POST */
    int  http_timeout;              /* HTTP request timeout in seconds */

    /* internal */
    char          config_path[512];
    GFileMonitor *monitor;
    gulong        monitor_handler_id;
} Config;

/* Load or create the config file. Returns true on success. */
bool config_init(Config *cfg);

/* Start watching the config file for changes.
 * callback is invoked (on the main thread) whenever the file changes. */
void config_watch(Config *cfg, GCallback callback, gpointer user_data);

/* Re-read the config file into cfg. */
bool config_reload(Config *cfg);

/* Save the current cfg values to the config file (triggers monitor). */
bool config_save(const Config *cfg);

/* Clean up. */
void config_destroy(Config *cfg);

#endif /* CONFIG_H */

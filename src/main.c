#define _GNU_SOURCE

#include "config.h"
#include "logger.h"
#include "ping.h"
#include "http_check.h"
#include "tray.h"
#include "settings_ui.h"

#ifdef HAVE_LIBSYSTEMD
#include "dbus_monitor.h"
#endif

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <libgen.h>
#include <unistd.h>
#include <linux/limits.h>
#include <time.h>
#include <stdbool.h>

#ifdef HAVE_LIBNOTIFY
#include <libnotify/notify.h>
#endif

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

static Config  g_config;
static guint   g_timer_id    = 0;
static char    g_icon_dir[PATH_MAX];
static GMutex  g_config_mutex;

#ifdef STANDALONE
#include "icons_embedded.h"
#include <ftw.h>

static char g_standalone_dir[PATH_MAX] = "";

static int remove_item(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)typeflag;
    (void)ftwbuf;
    return remove(fpath);
}

static void cleanup_standalone(void) {
    if (g_standalone_dir[0]) {
        nftw(g_standalone_dir, remove_item, 64, FTW_DEPTH | FTW_PHYS);
    }
}
#endif

#define PING_TIMEOUT 4

static volatile int ping_in_progress = 0;
static int g_last_state = -1;
static bool g_have_last_target = false;
static CheckMode g_last_mode = DEFAULT_CHECK_MODE;
static char g_last_target[512] = "";

/* Return the "target" label for the current mode */
static const char *current_target(void) {
    if (g_config.check_mode == CHECK_MODE_HTTP)
        return g_config.http_url;
    return g_config.address;
}

static void log_state_change(bool ok) {
    g_mutex_lock(&g_config_mutex);
    log_msg(LOG_STATUS, "Internet is %s (%s)", ok ? "UP" : "DOWN", current_target());
    g_mutex_unlock(&g_config_mutex);
}

#ifdef HAVE_LIBNOTIFY
#define NOTIFICATION_TIMEOUT_MS 5000

static void send_notification(bool connected, const char *target) {
    if (!g_config.notify_enabled) {
        return;
    }

    if (!notify_is_initted()) {
        notify_init("Internet Indicator");
    }

    const char *icon_name = connected ? "net-good" : "net-bad";
    NotifyNotification *n = notify_notification_new(
        connected ? "Internet Connected" : "Internet Disconnected",
        target,
        icon_name
    );

    gchar *icon_path = g_build_filename(g_icon_dir,
                                        connected ? "net-good.png" : "net-bad.png",
                                        NULL);
    GError *error = NULL;
    GdkPixbuf *icon = gdk_pixbuf_new_from_file(icon_path, &error);
    if (icon) {
        notify_notification_set_image_from_pixbuf(n, icon);
        g_object_unref(icon);
    } else if (error) {
        log_msg(LOG_WARN, "failed to load notification icon %s: %s", icon_path, error->message);
        g_error_free(error);
    }
    g_free(icon_path);

    notify_notification_set_timeout(n, NOTIFICATION_TIMEOUT_MS);
    notify_notification_set_urgency(n, connected ? NOTIFY_URGENCY_LOW : NOTIFY_URGENCY_CRITICAL);
    notify_notification_show(n, NULL);
    g_object_unref(n);
}
#endif

static gboolean on_ping_result(gpointer data) {
    PingResult *pr = (PingResult *)data;
    bool ok = pr->ok;

    g_mutex_lock(&g_config_mutex);
    tray_set_status(ok, current_target(), pr->latency_ms);
    tray_set_error(pr->error_msg);
    g_mutex_unlock(&g_config_mutex);
    
    if (g_last_state == -1 || g_last_state != (int)ok) {
        log_state_change(ok);
#ifdef HAVE_LIBNOTIFY
        send_notification(ok, current_target());
#endif
        g_last_state = (int)ok;
    }
    
    g_free(pr);
    g_atomic_int_set(&ping_in_progress, 0);
    return G_SOURCE_REMOVE;
}

/* Worker data passed to the ping thread */
typedef struct {
    CheckMode mode;
    char      address[256];
    int       max_retries;
    int       retry_delay;
    /* HTTP fields */
    char      http_url[512];
    int       http_port;
    bool      http_verify_ssl;
    char      http_acceptable_codes[256];
    char      http_headers[2048];
    char      http_method[16];
    int       http_timeout;
} WorkerData;

static gpointer ping_worker(gpointer data) {
    WorkerData *wd = (WorkerData *)data;
    PingResult pr = { false, -1.0, "" };
    int attempts = wd->max_retries > 0 ? wd->max_retries : 1;

    for (int i = 0; i < attempts; i++) {
        if (wd->mode == CHECK_MODE_HTTP) {
            pr = http_check_host(wd->http_url, wd->http_port, wd->http_verify_ssl,
                                  wd->http_acceptable_codes, wd->http_headers,
                                  wd->http_method, wd->http_timeout);
        } else {
            pr = ping_host(wd->address, PING_TIMEOUT);
        }
        if (pr.ok) break;
        if (i < attempts - 1) {
            g_usleep((gulong)wd->retry_delay * 1000 * 1000);
        }
    }
    PingResult *heap_pr = g_new(PingResult, 1);
    *heap_pr = pr;
    g_idle_add(on_ping_result, heap_pr);
    g_free(wd);
    return NULL;
}

static gboolean on_ping_timer(gpointer data)
{
    (void)data;
#ifdef HAVE_LIBSYSTEMD
    if (dbus_monitor_is_sleeping() || dbus_monitor_is_locked()) {
        return G_SOURCE_CONTINUE;
    }
#endif
    if (g_atomic_int_get(&ping_in_progress)) {
        return G_SOURCE_CONTINUE;
    }
    g_atomic_int_set(&ping_in_progress, 1);

    g_mutex_lock(&g_config_mutex);
    /* Snapshot config into worker data */
    WorkerData *wd = g_new0(WorkerData, 1);
    wd->mode = g_config.check_mode;
    snprintf(wd->address, sizeof(wd->address), "%s", g_config.address);
    wd->max_retries = g_config.max_retries;
    wd->retry_delay = g_config.retry_delay;
    snprintf(wd->http_url, sizeof(wd->http_url), "%s", g_config.http_url);
    wd->http_port = g_config.http_port;
    wd->http_verify_ssl = g_config.http_verify_ssl;
    snprintf(wd->http_acceptable_codes, sizeof(wd->http_acceptable_codes), "%s", g_config.http_acceptable_codes);
    snprintf(wd->http_headers, sizeof(wd->http_headers), "%s", g_config.http_headers);
    snprintf(wd->http_method, sizeof(wd->http_method), "%s", g_config.http_method);
    wd->http_timeout = g_config.http_timeout;
    g_mutex_unlock(&g_config_mutex);

    g_thread_unref(g_thread_new("ping", ping_worker, wd));
    return G_SOURCE_CONTINUE;
}

static void apply_runtime_config(void)
{
    int interval;
    bool log_enabled;
    bool debug;
    int log_max_size_kb;
    CheckMode mode;
    char target[512];
    char log_file_path[sizeof(g_config.log_file_path)];

    g_mutex_lock(&g_config_mutex);
    interval = g_config.interval;
    log_enabled = g_config.log_enabled;
    debug = g_config.debug;
    log_max_size_kb = g_config.log_max_size_kb;
    mode = g_config.check_mode;
    snprintf(target, sizeof(target), "%s",
             g_config.check_mode == CHECK_MODE_HTTP ? g_config.http_url : g_config.address);
    snprintf(log_file_path, sizeof(log_file_path), "%s", g_config.log_file_path);
    g_mutex_unlock(&g_config_mutex);

    if (log_enabled) {
        logger_configure(log_file_path, log_max_size_kb, debug);
    } else {
        logger_configure(NULL, 0, debug);
    }

    if (!g_have_last_target || g_last_mode != mode || strcmp(g_last_target, target) != 0) {
        if (g_have_last_target) {
            log_msg(LOG_INFO, "target changed → mode=%s target=%s",
                    mode == CHECK_MODE_HTTP ? "http" : "icmp", target);
        }
        g_last_mode = mode;
        snprintf(g_last_target, sizeof(g_last_target), "%s", target);
        g_have_last_target = true;
        g_last_state = -1;
    }

    if (g_timer_id) {
        g_source_remove(g_timer_id);
    }
    g_timer_id = g_timeout_add_seconds((guint)interval,
                                       on_ping_timer, NULL);
    log_msg(LOG_INFO, "timer restarted with interval=%ds", interval);

#ifdef HAVE_LIBSYSTEMD
    dbus_monitor_init(&g_config);
#endif

    on_ping_timer(NULL);
}

static void on_config_changed(gpointer data)
{
    (void)data;
    g_mutex_lock(&g_config_mutex);
    bool ok = config_reload(&g_config);
    g_mutex_unlock(&g_config_mutex);

    if (!ok) {
        return;
    }

    apply_runtime_config();
}

static void on_settings_saved(void) {
    apply_runtime_config();
}

static void on_open_config(void)
{
    settings_ui_show(&g_config, on_settings_saved);
}

static void resolve_icon_dir(void)
{
#ifdef STANDALONE
    char tmpl[] = "/tmp/internet-indicator-XXXXXX";
    if (mkdtemp(tmpl)) {
        strncpy(g_icon_dir, tmpl, sizeof(g_icon_dir) - 1);
        g_icon_dir[sizeof(g_icon_dir) - 1] = '\0';
        strncpy(g_standalone_dir, tmpl, sizeof(g_standalone_dir) - 1);
        g_standalone_dir[sizeof(g_standalone_dir) - 1] = '\0';
        
        char path[PATH_MAX];
        FILE *f;
        
        snprintf(path, sizeof(path), "%s/net-good.png", tmpl);
        f = fopen(path, "wb");
        if (f) {
            fwrite(icons_net_good_png, 1, icons_net_good_png_len, f);
            fclose(f);
        }
        
        snprintf(path, sizeof(path), "%s/net-bad.png", tmpl);
        f = fopen(path, "wb");
        if (f) {
            fwrite(icons_net_bad_png, 1, icons_net_bad_png_len, f);
            fclose(f);
        }
        
        atexit(cleanup_standalone);
        return;
    }
#endif

    const char *installed = "/usr/local/share/internet-indicator/icons";
    if (g_file_test(installed, G_FILE_TEST_IS_DIR)) {
        strncpy(g_icon_dir, installed, sizeof(g_icon_dir) - 1);
        g_icon_dir[sizeof(g_icon_dir) - 1] = '\0';
        return;
    }

    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        char *dir = dirname(exe_path);
        snprintf(g_icon_dir, sizeof(g_icon_dir), "%s/icons", dir);

        char *real = realpath(g_icon_dir, NULL);
        if (real) {
            strncpy(g_icon_dir, real, sizeof(g_icon_dir) - 1);
            g_icon_dir[sizeof(g_icon_dir) - 1] = '\0';
            free(real);
            if (g_file_test(g_icon_dir, G_FILE_TEST_IS_DIR))
                return;
        }
    }

    strncpy(g_icon_dir, "icons", sizeof(g_icon_dir) - 1);
    g_icon_dir[sizeof(g_icon_dir) - 1] = '\0';
}

static volatile sig_atomic_t g_quit_requested = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_quit_requested = 1;
}

static gboolean check_quit(gpointer data)
{
    (void)data;
    if (g_quit_requested) gtk_main_quit();
    return G_SOURCE_CONTINUE;
}

static bool string_contains_dark(const char *value)
{
    if (!value || value[0] == '\0') return false;

    gchar *lower = g_ascii_strdown(value, -1);
    bool has_dark = strstr(lower, "dark") != NULL;
    g_free(lower);
    return has_dark;
}

static void apply_system_theme_preference(void)
{
    GtkSettings *gtk_settings = gtk_settings_get_default();
    if (!gtk_settings) return;

    bool prefer_dark = false;
    bool detected_preference = false;

    const char *gtk_theme_env = g_getenv("GTK_THEME");
    if (gtk_theme_env && gtk_theme_env[0] != '\0') {
        prefer_dark = string_contains_dark(gtk_theme_env);
        detected_preference = true;
    }

    GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default();
    if (schema_source) {
        GSettingsSchema *schema = g_settings_schema_source_lookup(
            schema_source, "org.gnome.desktop.interface", TRUE);

        if (schema) {
            GSettings *interface_settings = g_settings_new("org.gnome.desktop.interface");

            if (g_settings_schema_has_key(schema, "color-scheme")) {
                gchar *color_scheme = g_settings_get_string(interface_settings, "color-scheme");
                if (g_strcmp0(color_scheme, "prefer-dark") == 0) {
                    prefer_dark = true;
                    detected_preference = true;
                } else if (g_strcmp0(color_scheme, "prefer-light") == 0) {
                    prefer_dark = false;
                    detected_preference = true;
                }
                g_free(color_scheme);
            }

            if (!detected_preference && g_settings_schema_has_key(schema, "gtk-theme")) {
                gchar *gtk_theme = g_settings_get_string(interface_settings, "gtk-theme");
                prefer_dark = string_contains_dark(gtk_theme);
                detected_preference = true;
                g_free(gtk_theme);
            }

            g_object_unref(interface_settings);
            g_settings_schema_unref(schema);
        }
    }

    if (!detected_preference) {
        gchar *gtk_theme_name = NULL;
        g_object_get(gtk_settings, "gtk-theme-name", &gtk_theme_name, NULL);
        if (gtk_theme_name) {
            prefer_dark = string_contains_dark(gtk_theme_name);
            detected_preference = true;
            g_free(gtk_theme_name);
        }
    }

    g_object_set(gtk_settings,
                 "gtk-application-prefer-dark-theme", prefer_dark,
                 NULL);
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);
    apply_system_theme_preference();

#ifdef HAVE_LIBCURL
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "failed to initialize libcurl\n");
        return 1;
    }
#endif

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    g_timeout_add(200, check_quit, NULL);

    g_mutex_init(&g_config_mutex);

    if (!config_init(&g_config)) {
        log_msg(LOG_ERROR, "config initialization failed");
#ifdef HAVE_LIBCURL
        curl_global_cleanup();
#endif
        return 1;
    }

    resolve_icon_dir();

    if (!tray_init(g_icon_dir)) {
        log_msg(LOG_ERROR, "tray initialization failed");
        config_destroy(&g_config);
#ifdef HAVE_LIBCURL
        curl_global_cleanup();
#endif
        return 1;
    }
    tray_set_config_callback(on_open_config);
    
#ifdef HAVE_LIBSYSTEMD
    dbus_monitor_init(&g_config);
#endif

    config_watch(&g_config, G_CALLBACK(on_config_changed), NULL);
    on_ping_timer(NULL);

    g_timer_id = g_timeout_add_seconds((guint)g_config.interval,
                                       on_ping_timer, NULL);

    gtk_main();

    if (g_timer_id) g_source_remove(g_timer_id);
    tray_destroy();
    config_destroy(&g_config);
#ifdef HAVE_LIBSYSTEMD
    dbus_monitor_cleanup();
#endif
#ifdef HAVE_LIBCURL
    curl_global_cleanup();
#endif

    log_msg(LOG_INFO, "exiting");
    return 0;
}

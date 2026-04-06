#define _GNU_SOURCE

#include "config.h"
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

static Config  g_config;
static guint   g_timer_id    = 0;
static char    g_icon_dir[PATH_MAX];

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

/* Return the "target" label for the current mode */
static const char *current_target(void) {
    if (g_config.check_mode == CHECK_MODE_HTTP)
        return g_config.http_url;
    return g_config.address;
}

static void log_state_change(bool ok) {
    if (!g_config.log_enabled) return;
    
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    
    if (ok) {
        printf("[%s] STATUS: Internet is UP (%s)\n", buf, current_target());
    } else {
        printf("[%s] STATUS: Internet is DOWN (%s)\n", buf, current_target());
    }
    fflush(stdout);
}

static gboolean on_ping_result(gpointer data) {
    bool ok = GPOINTER_TO_INT(data);
    tray_set_status(ok, current_target());
    
    if (g_last_state == -1 || g_last_state != (int)ok) {
        log_state_change(ok);
        g_last_state = (int)ok;
    }
    
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
    bool ok = false;
    int attempts = wd->max_retries > 0 ? wd->max_retries : 1;

    for (int i = 0; i < attempts; i++) {
        if (wd->mode == CHECK_MODE_HTTP) {
            ok = http_check_host(wd->http_url, wd->http_port, wd->http_verify_ssl,
                                  wd->http_acceptable_codes, wd->http_headers,
                                  wd->http_method, wd->http_timeout);
        } else {
            ok = ping_host(wd->address, PING_TIMEOUT);
        }
        if (ok) break;
        if (i < attempts - 1) {
            g_usleep((gulong)wd->retry_delay * 1000 * 1000);
        }
    }
    g_idle_add(on_ping_result, GINT_TO_POINTER((gint)ok));
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

    g_thread_unref(g_thread_new("ping", ping_worker, wd));
    return G_SOURCE_CONTINUE;
}

static void on_config_changed(gpointer data)
{
    (void)data;
    config_reload(&g_config);

    if (g_timer_id) {
        g_source_remove(g_timer_id);
    }
    g_timer_id = g_timeout_add_seconds((guint)g_config.interval,
                                       on_ping_timer, NULL);
    printf("internet-indicator: timer restarted with interval=%ds\n",
           g_config.interval);

    on_ping_timer(NULL);
}

static void on_settings_saved(void) {
#ifdef HAVE_LIBSYSTEMD
    dbus_monitor_init(&g_config);
#endif
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

static void on_signal(int sig)
{
    (void)sig;
    gtk_main_quit();
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    if (!config_init(&g_config)) {
        fprintf(stderr, "internet-indicator: config initialization failed\n");
        return 1;
    }

    resolve_icon_dir();

    if (!tray_init(g_icon_dir)) {
        fprintf(stderr, "internet-indicator: tray initialization failed\n");
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

    printf("internet-indicator: exiting\n");
    return 0;
}

#include "config.h"
#include "ping.h"
#include "tray.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <libgen.h>
#include <unistd.h>
#include <linux/limits.h>

/* ------------------------------------------------------------------ */
/*  Globals                                                            */
/* ------------------------------------------------------------------ */

static Config  g_config;
static guint   g_timer_id    = 0;
static char    g_icon_dir[PATH_MAX];

#define PING_TIMEOUT 2  /* seconds */

/* ------------------------------------------------------------------ */
/*  Ping timer callback                                                */
/* ------------------------------------------------------------------ */

static gboolean on_ping_timer(gpointer data G_GNUC_UNUSED)
{
    bool ok = ping_host(g_config.address, PING_TIMEOUT);
    tray_set_status(ok, g_config.address);
    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/*  Config change callback                                             */
/* ------------------------------------------------------------------ */

static void on_config_changed(gpointer data G_GNUC_UNUSED)
{
    int old_interval = g_config.interval;
    config_reload(&g_config);

    /* restart the timer if the interval changed */
    if (g_config.interval != old_interval && g_timer_id) {
        g_source_remove(g_timer_id);
        g_timer_id = g_timeout_add_seconds((guint)g_config.interval,
                                           on_ping_timer, NULL);
        printf("internet-indicator: timer restarted with interval=%ds\n",
               g_config.interval);
    }

    /* do an immediate check with the new address */
    on_ping_timer(NULL);
}

/* ------------------------------------------------------------------ */
/*  Resolve icon directory                                             */
/* ------------------------------------------------------------------ */

static void resolve_icon_dir(void)
{
    /* 1. Check installed location */
    const char *installed = "/usr/local/share/internet-indicator/icons";
    if (g_file_test(installed, G_FILE_TEST_IS_DIR)) {
        strncpy(g_icon_dir, installed, sizeof(g_icon_dir) - 1);
        return;
    }

    /* 2. Check relative to the binary (development mode) */
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        char *dir = dirname(exe_path);
        snprintf(g_icon_dir, sizeof(g_icon_dir), "%s/icons", dir);

        /* canonicalize */
        char *real = realpath(g_icon_dir, NULL);
        if (real) {
            strncpy(g_icon_dir, real, sizeof(g_icon_dir) - 1);
            free(real);
            if (g_file_test(g_icon_dir, G_FILE_TEST_IS_DIR))
                return;
        }
    }

    /* 3. Fallback: current directory */
    strncpy(g_icon_dir, "icons", sizeof(g_icon_dir) - 1);
}

/* ------------------------------------------------------------------ */
/*  Signal handling                                                    */
/* ------------------------------------------------------------------ */

static void on_signal(int sig G_GNUC_UNUSED)
{
    gtk_main_quit();
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* load config */
    if (!config_init(&g_config)) {
        fprintf(stderr, "internet-indicator: config initialization failed\n");
        return 1;
    }

    /* resolve icon directory */
    resolve_icon_dir();
    printf("internet-indicator: icon dir = %s\n", g_icon_dir);

    /* init tray */
    if (!tray_init(g_icon_dir)) {
        fprintf(stderr, "internet-indicator: tray initialization failed\n");
        return 1;
    }

    /* watch config for changes */
    config_watch(&g_config, G_CALLBACK(on_config_changed), NULL);

    /* do an initial check right away */
    on_ping_timer(NULL);

    /* start periodic timer */
    g_timer_id = g_timeout_add_seconds((guint)g_config.interval,
                                       on_ping_timer, NULL);

    /* run main loop */
    gtk_main();

    /* cleanup */
    if (g_timer_id) g_source_remove(g_timer_id);
    tray_destroy();
    config_destroy(&g_config);

    printf("internet-indicator: exiting\n");
    return 0;
}

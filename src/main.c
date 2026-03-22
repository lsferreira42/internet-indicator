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
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Globals                                                            */
/* ------------------------------------------------------------------ */

static Config  g_config;
static guint   g_timer_id    = 0;
static char    g_icon_dir[PATH_MAX];

#ifdef STANDALONE
#include "icons_embedded.h"
static char g_standalone_dir[PATH_MAX] = "";

static void cleanup_standalone(void) {
    if (g_standalone_dir[0]) {
        char cmd[PATH_MAX + 16];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", g_standalone_dir);
        if (system(cmd) == -1) { /* ignore error */ }
    }
}
#endif

#define PING_TIMEOUT 2  /* seconds */

/* ------------------------------------------------------------------ */
/*  Ping timer callback (async)                                        */
/* ------------------------------------------------------------------ */

static volatile int ping_in_progress = 0;
static int g_last_state = -1;

static void log_state_change(bool ok) {
    if (!g_config.log_enabled) return;
    
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    
    if (ok) {
        printf("[%s] STATUS: Internet is UP (%s)\n", buf, g_config.address);
    } else {
        printf("[%s] STATUS: Internet is DOWN (%s)\n", buf, g_config.address);
    }
    fflush(stdout);
}

static gboolean on_ping_result(gpointer data) {
    bool ok = GPOINTER_TO_INT(data);
    tray_set_status(ok, g_config.address);
    
    if (g_last_state == -1 || g_last_state != (int)ok) {
        log_state_change(ok);
        g_last_state = (int)ok;
    }
    
    g_atomic_int_set(&ping_in_progress, 0);
    return G_SOURCE_REMOVE;
}

static gpointer ping_worker(gpointer data) {
    char *addr = (char*)data;
    bool ok = ping_host(addr, PING_TIMEOUT);
    g_idle_add(on_ping_result, GINT_TO_POINTER((gint)ok));
    g_free(addr);
    return NULL;
}

static gboolean on_ping_timer(gpointer data G_GNUC_UNUSED)
{
    if (g_atomic_int_get(&ping_in_progress)) {
        return G_SOURCE_CONTINUE; /* skip tick if previous ping still running */
    }
    g_atomic_int_set(&ping_in_progress, 1);
    char *addr = g_strdup(g_config.address);
    g_thread_unref(g_thread_new("ping", ping_worker, addr));
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
/*  Config UI callback (non-blocking GTK dialog)                       */
/* ------------------------------------------------------------------ */

static void on_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data G_GNUC_UNUSED)
{
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkWidget *entry_addr = g_object_get_data(G_OBJECT(dialog), "entry_addr");
        GtkWidget *entry_intv = g_object_get_data(G_OBJECT(dialog), "entry_intv");
        GtkWidget *chk_log    = g_object_get_data(G_OBJECT(dialog), "chk_log");
        
        const char *new_addr = gtk_entry_get_text(GTK_ENTRY(entry_addr));
        int new_interval = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(entry_intv));
        bool new_log = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_log));
        
        strncpy(g_config.address, new_addr, sizeof(g_config.address) - 1);
        g_config.address[sizeof(g_config.address) - 1] = '\0';
        g_config.interval = new_interval;
        g_config.log_enabled = new_log;
        
        config_save(&g_config);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void on_open_config(void)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Settings",
                                                    NULL,
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Save", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);
    
    GtkWidget *lbl_address = gtk_label_new("Address (IP/Host):");
    gtk_widget_set_halign(lbl_address, GTK_ALIGN_END);
    GtkWidget *entry_address = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry_address), g_config.address);
    
    GtkWidget *lbl_interval = gtk_label_new("Interval (seconds):");
    gtk_widget_set_halign(lbl_interval, GTK_ALIGN_END);
    GtkWidget *entry_interval = gtk_spin_button_new_with_range(1, 3600, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(entry_interval), g_config.interval);
    
    GtkWidget *chk_log = gtk_check_button_new_with_label("Enable Connection Logs");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_log), g_config.log_enabled);
    
    gtk_grid_attach(GTK_GRID(grid), lbl_address, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_address, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_interval, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_interval, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), chk_log, 1, 2, 1, 1);
    
    gtk_container_add(GTK_CONTAINER(content_area), grid);
    
    g_object_set_data(G_OBJECT(dialog), "entry_addr", entry_address);
    g_object_set_data(G_OBJECT(dialog), "entry_intv", entry_interval);
    g_object_set_data(G_OBJECT(dialog), "chk_log", chk_log);
    
    g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), NULL);
    
    gtk_widget_show_all(dialog);
}

/* ------------------------------------------------------------------ */
/*  Resolve icon directory                                             */
/* ------------------------------------------------------------------ */

static void resolve_icon_dir(void)
{
#ifdef STANDALONE
    char tmpl[] = "/tmp/internet-indicator-XXXXXX";
    if (mkdtemp(tmpl)) {
        strncpy(g_icon_dir, tmpl, sizeof(g_icon_dir) - 1);
        strncpy(g_standalone_dir, tmpl, sizeof(g_standalone_dir) - 1);
        
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
    tray_set_config_callback(on_open_config);

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

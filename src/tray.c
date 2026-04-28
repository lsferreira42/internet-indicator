#include "tray.h"
#include "logger.h"

#include <gtk/gtk.h>
#ifdef HAVE_AYATANA
#include <libayatana-appindicator/app-indicator.h>
#else
#include <libappindicator/app-indicator.h>
#endif
#include <stdio.h>
#include <string.h>

static AppIndicator *indicator       = NULL;
static GtkWidget    *menu            = NULL;
static GtkWidget    *status_item     = NULL;
static GtkWidget    *error_item      = NULL;
static void (*on_config_cb)(void)    = NULL;

static void on_config(GtkMenuItem *item G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    if (on_config_cb) {
        on_config_cb();
    }
}

static void on_quit(GtkMenuItem *item G_GNUC_UNUSED, gpointer data G_GNUC_UNUSED)
{
    gtk_main_quit();
}

bool tray_init(const char *icon_dir)
{
    indicator = app_indicator_new("internet-indicator",
                                  "net-good",
                                  APP_INDICATOR_CATEGORY_COMMUNICATIONS);

    if (!indicator) {
        log_msg(LOG_ERROR, "failed to create AppIndicator");
        return false;
    }

    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_icon_theme_path(indicator, icon_dir);
    app_indicator_set_icon(indicator, "net-good");
    app_indicator_set_title(indicator, "Internet Indicator");

    menu = gtk_menu_new();

    status_item = gtk_menu_item_new_with_label("Checking...");
    gtk_widget_set_sensitive(status_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), status_item);
    
    error_item = gtk_menu_item_new_with_label("");
    gtk_widget_set_sensitive(error_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), error_item);
    gtk_widget_set_no_show_all(error_item, TRUE);

    GtkWidget *sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);

    GtkWidget *config_item = gtk_menu_item_new_with_label("Settings...");
    g_signal_connect(config_item, "activate", G_CALLBACK(on_config), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), config_item);

    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator, GTK_MENU(menu));

    return true;
}

void tray_set_status(bool connected, const char *target, double latency_ms)
{
    if (!indicator) return;

    if (connected) {
        app_indicator_set_icon(indicator, "net-good");
    } else {
        app_indicator_set_icon(indicator, "net-bad");
    }

    char label[384];
    if (connected) {
        if (latency_ms >= 0)
            snprintf(label, sizeof(label), "✓ Connected — %s (%.0fms)", target, latency_ms);
        else
            snprintf(label, sizeof(label), "✓ Connected — %s", target);
    } else {
        snprintf(label, sizeof(label), "✗ Disconnected — %s", target);
    }

    gtk_menu_item_set_label(GTK_MENU_ITEM(status_item), label);
}

void tray_set_error(const char *error_msg)
{
    if (!error_item) return;
    if (error_msg && error_msg[0] != '\0') {
        gtk_menu_item_set_label(GTK_MENU_ITEM(error_item), error_msg);
        gtk_widget_show(error_item);
    } else {
        gtk_widget_hide(error_item);
    }
}

void tray_set_config_callback(void (*cb)(void))
{
    on_config_cb = cb;
}

void tray_destroy(void)
{
    if (indicator) {
        g_object_unref(indicator);
        indicator = NULL;
    }
}

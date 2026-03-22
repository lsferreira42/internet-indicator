#include "tray.h"

#include <gtk/gtk.h>
#include <libappindicator/app-indicator.h>
#include <stdio.h>
#include <string.h>

static AppIndicator *indicator       = NULL;
static GtkWidget    *menu            = NULL;
static GtkWidget    *status_item     = NULL;

/* ------------------------------------------------------------------ */

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
        fprintf(stderr, "internet-indicator: failed to create AppIndicator\n");
        return false;
    }

    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_icon_theme_path(indicator, icon_dir);
    app_indicator_set_icon(indicator, "net-good");
    app_indicator_set_title(indicator, "Internet Indicator");

    /* build menu */
    menu = gtk_menu_new();

    status_item = gtk_menu_item_new_with_label("Checking...");
    gtk_widget_set_sensitive(status_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), status_item);

    GtkWidget *sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);

    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator, GTK_MENU(menu));

    return true;
}

void tray_set_status(bool connected, const char *target)
{
    if (!indicator) return;

    if (connected) {
        app_indicator_set_icon(indicator, "net-good");
    } else {
        app_indicator_set_icon(indicator, "net-bad");
    }

    /* update menu label */
    char label[384];
    if (connected)
        snprintf(label, sizeof(label), "✓ Connected — %s", target);
    else
        snprintf(label, sizeof(label), "✗ Disconnected — %s", target);

    gtk_menu_item_set_label(GTK_MENU_ITEM(status_item), label);
}

void tray_destroy(void)
{
    if (indicator) {
        g_object_unref(indicator);
        indicator = NULL;
    }
}

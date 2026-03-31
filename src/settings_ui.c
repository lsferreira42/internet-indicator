#include "settings_ui.h"
#include <gtk/gtk.h>
#include <string.h>

typedef struct {
    Config *cfg;
    void (*on_save)(void);
} SettingsData;

static void on_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    SettingsData *sd = (SettingsData *)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkWidget *entry_addr = g_object_get_data(G_OBJECT(dialog), "entry_addr");
        GtkWidget *entry_intv = g_object_get_data(G_OBJECT(dialog), "entry_intv");
        GtkWidget *entry_retries = g_object_get_data(G_OBJECT(dialog), "entry_retries");
        GtkWidget *chk_log    = g_object_get_data(G_OBJECT(dialog), "chk_log");
        GtkWidget *chk_sleep  = g_object_get_data(G_OBJECT(dialog), "chk_sleep");
        GtkWidget *chk_lock   = g_object_get_data(G_OBJECT(dialog), "chk_lock");
        
        const char *new_addr = gtk_entry_get_text(GTK_ENTRY(entry_addr));
        int new_interval = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(entry_intv));
        int new_retries = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(entry_retries));
        bool new_log = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_log));
        bool new_sleep = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_sleep));
        bool new_lock = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_lock));
        
        strncpy(sd->cfg->address, new_addr, sizeof(sd->cfg->address) - 1);
        sd->cfg->address[sizeof(sd->cfg->address) - 1] = '\0';
        sd->cfg->interval = new_interval;
        sd->cfg->max_retries = new_retries;
        sd->cfg->log_enabled = new_log;
        sd->cfg->sleep_detection_enabled = new_sleep;
        sd->cfg->lock_detection_enabled = new_lock;

        config_save(sd->cfg);

        if (sd->on_save) {
            sd->on_save();
        }
    }
    
    g_free(sd);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

void settings_ui_show(Config *cfg, void (*on_save)(void)) {
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
    gtk_entry_set_text(GTK_ENTRY(entry_address), cfg->address);
    
    GtkWidget *lbl_interval = gtk_label_new("Interval (seconds):");
    gtk_widget_set_halign(lbl_interval, GTK_ALIGN_END);
    GtkWidget *entry_interval = gtk_spin_button_new_with_range(1, 3600, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(entry_interval), cfg->interval);

    GtkWidget *lbl_retries = gtk_label_new("Max Retries (on fail):");
    gtk_widget_set_halign(lbl_retries, GTK_ALIGN_END);
    GtkWidget *entry_retries = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(entry_retries), cfg->max_retries);
    
    GtkWidget *chk_log = gtk_check_button_new_with_label("Enable Connection Logs");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_log), cfg->log_enabled);
    
    GtkWidget *chk_sleep = gtk_check_button_new_with_label("Detect Sleep/Suspension (D-Bus)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_sleep), cfg->sleep_detection_enabled);
    
    GtkWidget *chk_lock = gtk_check_button_new_with_label("Detect Screen Lock (D-Bus)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_lock), cfg->lock_detection_enabled);

#ifndef HAVE_LIBSYSTEMD
    gtk_widget_set_sensitive(chk_sleep, FALSE);
    gtk_widget_set_sensitive(chk_lock, FALSE);
    gtk_widget_set_tooltip_text(chk_sleep, "Feature not available on this system (libsystemd missing)");
    gtk_widget_set_tooltip_text(chk_lock, "Feature not available on this system (libsystemd missing)");
#endif
    
    gtk_grid_attach(GTK_GRID(grid), lbl_address, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_address, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_interval, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_interval, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_retries, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_retries, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), chk_log, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), chk_sleep, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), chk_lock, 1, 5, 1, 1);
    
    gtk_container_add(GTK_CONTAINER(content_area), grid);
    
    g_object_set_data(G_OBJECT(dialog), "entry_addr", entry_address);
    g_object_set_data(G_OBJECT(dialog), "entry_intv", entry_interval);
    g_object_set_data(G_OBJECT(dialog), "entry_retries", entry_retries);
    g_object_set_data(G_OBJECT(dialog), "chk_log", chk_log);
    g_object_set_data(G_OBJECT(dialog), "chk_sleep", chk_sleep);
    g_object_set_data(G_OBJECT(dialog), "chk_lock", chk_lock);
    
    SettingsData *sd = g_new0(SettingsData, 1);
    sd->cfg = cfg;
    sd->on_save = on_save;

    g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), sd);
    
    gtk_widget_show_all(dialog);
}

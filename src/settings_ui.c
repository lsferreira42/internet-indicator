#include "settings_ui.h"
#include <gtk/gtk.h>
#include <string.h>

static GtkWidget *s_settings_dialog = NULL;

typedef struct {
    Config *cfg;
    void (*on_save)(void);
    /* Mode toggles */
    GtkWidget *chk_icmp_enabled;
    GtkWidget *chk_http_enabled;
    /* ICMP widgets */
    GtkWidget *entry_addr;
    /* HTTP widgets */
    GtkWidget *entry_url;
    GtkWidget *spin_port;
    GtkWidget *chk_ssl;
    GtkWidget *entry_codes;
    GtkWidget *combo_method;
    GtkWidget *spin_timeout;
    char       http_headers[2048]; /* edited via popup */
    /* Global widgets */
    GtkWidget *entry_intv;
    GtkWidget *entry_retries;
    GtkWidget *spin_retry_delay;
    GtkWidget *chk_log;
    GtkWidget *chk_sleep;
    GtkWidget *chk_lock;
    GtkWidget *chk_notify;
    GtkWidget *entry_log_path;
    GtkWidget *spin_log_size;
    /* Log tab widgets */
    GtkListStore *log_store;
    GtkTreeModel *log_filter;
    GtkWidget *log_filter_entry;
} SettingsData;

/* ---------- mutual exclusion between ICMP / HTTP enabled ---------- */

static void on_http_toggled(GtkToggleButton *toggle, gpointer user_data);

static void on_icmp_toggled_real(GtkToggleButton *toggle, gpointer user_data) {
    SettingsData *sd = (SettingsData *)user_data;
    if (gtk_toggle_button_get_active(toggle)) {
        g_signal_handlers_block_by_func(sd->chk_http_enabled,
                                         G_CALLBACK(on_http_toggled), user_data);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sd->chk_http_enabled), FALSE);
        g_signal_handlers_unblock_by_func(sd->chk_http_enabled,
                                           G_CALLBACK(on_http_toggled), user_data);
    }
}

static void on_http_toggled(GtkToggleButton *toggle, gpointer user_data) {
    SettingsData *sd = (SettingsData *)user_data;
    if (gtk_toggle_button_get_active(toggle)) {
        g_signal_handlers_block_by_func(sd->chk_icmp_enabled,
                                         G_CALLBACK(on_icmp_toggled_real), user_data);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sd->chk_icmp_enabled), FALSE);
        g_signal_handlers_unblock_by_func(sd->chk_icmp_enabled,
                                           G_CALLBACK(on_icmp_toggled_real), user_data);
    }
}

/* ---------- headers editor popup ---------- */

static void on_headers_clicked(GtkButton *button G_GNUC_UNUSED, gpointer user_data) {
    SettingsData *sd = (SettingsData *)user_data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("HTTP Headers",
                                                     NULL,
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_OK", GTK_RESPONSE_ACCEPT,
                                                     NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 300);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget *lbl = gtk_label_new("One header per line, format: name=value");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_start(lbl, 10);
    gtk_widget_set_margin_top(lbl, 10);
    gtk_container_add(GTK_CONTAINER(content), lbl);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_margin_start(scroll, 10);
    gtk_widget_set_margin_end(scroll, 10);
    gtk_widget_set_margin_top(scroll, 5);
    gtk_widget_set_margin_bottom(scroll, 10);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buf, sd->http_headers, -1);
    gtk_container_add(GTK_CONTAINER(scroll), text_view);
    gtk_container_add(GTK_CONTAINER(content), scroll);

    gtk_widget_show_all(dialog);

    gint resp = gtk_dialog_run(GTK_DIALOG(dialog));
    if (resp == GTK_RESPONSE_ACCEPT) {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buf, &start, &end);
        gchar *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
        strncpy(sd->http_headers, text ? text : "", sizeof(sd->http_headers) - 1);
        sd->http_headers[sizeof(sd->http_headers) - 1] = '\0';
        g_free(text);
    }
    gtk_widget_destroy(dialog);
}

/* ---------- dialog response (save / cancel) ---------- */

static void on_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    SettingsData *sd = (SettingsData *)user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        /* Determine active mode */
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sd->chk_http_enabled)))
            sd->cfg->check_mode = CHECK_MODE_HTTP;
        else
            sd->cfg->check_mode = CHECK_MODE_ICMP;

        /* Global fields */
        sd->cfg->interval = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sd->entry_intv));
        sd->cfg->max_retries = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sd->entry_retries));
        sd->cfg->retry_delay = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sd->spin_retry_delay));
        sd->cfg->log_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sd->chk_log));
        sd->cfg->sleep_detection_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sd->chk_sleep));
        sd->cfg->lock_detection_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sd->chk_lock));
        sd->cfg->notify_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sd->chk_notify));
        sd->cfg->log_max_size_kb = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sd->spin_log_size));
        
        const char *new_log_path = gtk_entry_get_text(GTK_ENTRY(sd->entry_log_path));
        strncpy(sd->cfg->log_file_path, new_log_path, sizeof(sd->cfg->log_file_path) - 1);
        sd->cfg->log_file_path[sizeof(sd->cfg->log_file_path) - 1] = '\0';

        /* ICMP fields */
        const char *new_addr = gtk_entry_get_text(GTK_ENTRY(sd->entry_addr));
        strncpy(sd->cfg->address, new_addr, sizeof(sd->cfg->address) - 1);
        sd->cfg->address[sizeof(sd->cfg->address) - 1] = '\0';

        /* HTTP fields */
        const char *new_url = gtk_entry_get_text(GTK_ENTRY(sd->entry_url));
        strncpy(sd->cfg->http_url, new_url, sizeof(sd->cfg->http_url) - 1);
        sd->cfg->http_url[sizeof(sd->cfg->http_url) - 1] = '\0';
        sd->cfg->http_port = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sd->spin_port));
        sd->cfg->http_verify_ssl = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sd->chk_ssl));
        sd->cfg->http_timeout = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sd->spin_timeout));

        const char *new_codes = gtk_entry_get_text(GTK_ENTRY(sd->entry_codes));
        strncpy(sd->cfg->http_acceptable_codes, new_codes, sizeof(sd->cfg->http_acceptable_codes) - 1);
        sd->cfg->http_acceptable_codes[sizeof(sd->cfg->http_acceptable_codes) - 1] = '\0';

        /* Method from combo */
        const gchar *method = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(sd->combo_method));
        if (method) {
            strncpy(sd->cfg->http_method, method, sizeof(sd->cfg->http_method) - 1);
            sd->cfg->http_method[sizeof(sd->cfg->http_method) - 1] = '\0';
        }

        /* Headers */
        snprintf(sd->cfg->http_headers, sizeof(sd->cfg->http_headers), "%s", sd->http_headers);

        if (config_save(sd->cfg) && sd->on_save) {
            sd->on_save();
        }
    }
    
    g_free(sd);
    s_settings_dialog = NULL;
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

/* ---------- Logs Tab Filtering ---------- */

static void on_log_filter_changed(GtkSearchEntry *entry G_GNUC_UNUSED, gpointer user_data) {
    SettingsData *sd = (SettingsData *)user_data;
    if (sd->log_filter) {
        gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(sd->log_filter));
    }
}

static gboolean log_filter_func(GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    SettingsData *sd = (SettingsData *)data;
    const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(sd->log_filter_entry));
    if (!search_text || search_text[0] == '\0') return TRUE;

    gchar *row_text;
    gtk_tree_model_get(model, iter, 0, &row_text, -1);
    if (!row_text) return FALSE;

    gboolean match = FALSE;
    gchar *lower_row = g_utf8_strdown(row_text, -1);
    gchar *lower_search = g_utf8_strdown(search_text, -1);
    if (lower_row && lower_search && strstr(lower_row, lower_search) != NULL) {
        match = TRUE;
    }
    if (lower_row) g_free(lower_row);
    if (lower_search) g_free(lower_search);
    g_free(row_text);
    return match;
}

static GtkWidget *build_logs_tab(SettingsData *sd) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);

    /* Search entry */
    sd->log_filter_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(sd->log_filter_entry), "Search logs by date or text...");
    g_signal_connect(sd->log_filter_entry, "search-changed", G_CALLBACK(on_log_filter_changed), sd);
    gtk_box_pack_start(GTK_BOX(vbox), sd->log_filter_entry, FALSE, FALSE, 0);

    /* Scrolled Window */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    /* List Store */
    sd->log_store = gtk_list_store_new(1, G_TYPE_STRING);
    
    /* Load file into store (newest first) */
    FILE *f = fopen(sd->cfg->log_file_path, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
            GtkTreeIter iter;
            gtk_list_store_prepend(sd->log_store, &iter);
            gtk_list_store_set(sd->log_store, &iter, 0, line, -1);
        }
        fclose(f);
    }

    /* Filter Model */
    sd->log_filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(sd->log_store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(sd->log_filter), log_filter_func, sd, NULL);

    /* Tree View */
    GtkWidget *tree = gtk_tree_view_new_with_model(sd->log_filter);
    g_object_unref(sd->log_store);
    g_object_unref(sd->log_filter);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Connection Events", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    gtk_container_add(GTK_CONTAINER(scroll), tree);

    return vbox;
}

/* ---------- build the Global tab ---------- */

static GtkWidget *build_global_tab(SettingsData *sd) {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);

    /* Interval */
    GtkWidget *lbl_intv = gtk_label_new("Interval (seconds):");
    gtk_widget_set_halign(lbl_intv, GTK_ALIGN_END);
    sd->entry_intv = gtk_spin_button_new_with_range(1, 3600, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sd->entry_intv), sd->cfg->interval);
    gtk_grid_attach(GTK_GRID(grid), lbl_intv, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sd->entry_intv, 1, 0, 1, 1);

    /* Max Retries */
    GtkWidget *lbl_ret = gtk_label_new("Max Retries (on fail):");
    gtk_widget_set_halign(lbl_ret, GTK_ALIGN_END);
    sd->entry_retries = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sd->entry_retries), sd->cfg->max_retries);
    gtk_grid_attach(GTK_GRID(grid), lbl_ret, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sd->entry_retries, 1, 1, 1, 1);

    /* Retry Delay */
    GtkWidget *lbl_delay = gtk_label_new("Time Between Retries (s):");
    gtk_widget_set_halign(lbl_delay, GTK_ALIGN_END);
    sd->spin_retry_delay = gtk_spin_button_new_with_range(1, 300, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sd->spin_retry_delay), sd->cfg->retry_delay);
    gtk_grid_attach(GTK_GRID(grid), lbl_delay, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sd->spin_retry_delay, 1, 2, 1, 1);

    /* Log */
    sd->chk_log = gtk_check_button_new_with_label("Enable Connection Logs");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sd->chk_log), sd->cfg->log_enabled);
    gtk_grid_attach(GTK_GRID(grid), sd->chk_log, 0, 3, 2, 1);

    /* Notify */
    sd->chk_notify = gtk_check_button_new_with_label("Enable Desktop Notifications");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sd->chk_notify), sd->cfg->notify_enabled);
    gtk_grid_attach(GTK_GRID(grid), sd->chk_notify, 0, 4, 2, 1);

    /* Sleep detection */
    sd->chk_sleep = gtk_check_button_new_with_label("Detect Sleep/Suspension (D-Bus)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sd->chk_sleep), sd->cfg->sleep_detection_enabled);
    gtk_grid_attach(GTK_GRID(grid), sd->chk_sleep, 0, 5, 2, 1);

    /* Lock detection */
    sd->chk_lock = gtk_check_button_new_with_label("Detect Screen Lock (D-Bus)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sd->chk_lock), sd->cfg->lock_detection_enabled);
    gtk_grid_attach(GTK_GRID(grid), sd->chk_lock, 0, 6, 2, 1);

    /* Log Path */
    GtkWidget *lbl_lpath = gtk_label_new("Log Path:");
    gtk_widget_set_halign(lbl_lpath, GTK_ALIGN_END);
    sd->entry_log_path = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(sd->entry_log_path), sd->cfg->log_file_path);
    gtk_grid_attach(GTK_GRID(grid), lbl_lpath, 0, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sd->entry_log_path, 1, 7, 1, 1);

    /* Log Size */
    GtkWidget *lbl_lsize = gtk_label_new("Max Log Size (KB):");
    gtk_widget_set_halign(lbl_lsize, GTK_ALIGN_END);
    sd->spin_log_size = gtk_spin_button_new_with_range(1, 102400, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sd->spin_log_size), sd->cfg->log_max_size_kb);
    gtk_grid_attach(GTK_GRID(grid), lbl_lsize, 0, 8, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sd->spin_log_size, 1, 8, 1, 1);

#ifndef HAVE_LIBSYSTEMD
    gtk_widget_set_sensitive(sd->chk_sleep, FALSE);
    gtk_widget_set_sensitive(sd->chk_lock, FALSE);
    gtk_widget_set_tooltip_text(sd->chk_sleep, "Feature not available on this system (libsystemd missing)");
    gtk_widget_set_tooltip_text(sd->chk_lock, "Feature not available on this system (libsystemd missing)");
#endif

    return grid;
}

/* ---------- build the ICMP tab ---------- */

static GtkWidget *build_icmp_tab(SettingsData *sd) {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);

    /* Enabled */
    sd->chk_icmp_enabled = gtk_check_button_new_with_label("Enabled");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sd->chk_icmp_enabled),
                                  sd->cfg->check_mode == CHECK_MODE_ICMP);
    gtk_grid_attach(GTK_GRID(grid), sd->chk_icmp_enabled, 0, 0, 2, 1);

    /* Address */
    GtkWidget *lbl_addr = gtk_label_new("Address (IP/Host):");
    gtk_widget_set_halign(lbl_addr, GTK_ALIGN_END);
    sd->entry_addr = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(sd->entry_addr), sd->cfg->address);
    gtk_grid_attach(GTK_GRID(grid), lbl_addr, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sd->entry_addr, 1, 1, 1, 1);

    return grid;
}

/* ---------- build the HTTP tab ---------- */

static GtkWidget *build_http_tab(SettingsData *sd) {
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);

    /* Enabled */
    sd->chk_http_enabled = gtk_check_button_new_with_label("Enabled");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sd->chk_http_enabled),
                                  sd->cfg->check_mode == CHECK_MODE_HTTP);
    gtk_grid_attach(GTK_GRID(grid), sd->chk_http_enabled, 0, 0, 2, 1);

#ifndef HAVE_LIBCURL
    gtk_widget_set_sensitive(sd->chk_http_enabled, FALSE);
    gtk_widget_set_tooltip_text(sd->chk_http_enabled,
                                "HTTP mode not available (built without libcurl)");
#endif

    /* URL */
    GtkWidget *lbl_url = gtk_label_new("URL:");
    gtk_widget_set_halign(lbl_url, GTK_ALIGN_END);
    sd->entry_url = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(sd->entry_url), sd->cfg->http_url);
    gtk_widget_set_hexpand(sd->entry_url, TRUE);
    gtk_grid_attach(GTK_GRID(grid), lbl_url, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sd->entry_url, 1, 1, 1, 1);

    /* Port */
    GtkWidget *lbl_port = gtk_label_new("Port (0 = default):");
    gtk_widget_set_halign(lbl_port, GTK_ALIGN_END);
    sd->spin_port = gtk_spin_button_new_with_range(0, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sd->spin_port), sd->cfg->http_port);
    gtk_grid_attach(GTK_GRID(grid), lbl_port, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sd->spin_port, 1, 2, 1, 1);

    /* Method */
    GtkWidget *lbl_method = gtk_label_new("Method:");
    gtk_widget_set_halign(lbl_method, GTK_ALIGN_END);
    sd->combo_method = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->combo_method), "GET");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->combo_method), "HEAD");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->combo_method), "POST");
    /* Set active based on current config */
    if (strcasecmp(sd->cfg->http_method, "HEAD") == 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(sd->combo_method), 1);
    else if (strcasecmp(sd->cfg->http_method, "POST") == 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(sd->combo_method), 2);
    else
        gtk_combo_box_set_active(GTK_COMBO_BOX(sd->combo_method), 0);
    gtk_grid_attach(GTK_GRID(grid), lbl_method, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sd->combo_method, 1, 3, 1, 1);

    /* Timeout */
    GtkWidget *lbl_timeout = gtk_label_new("Timeout (seconds):");
    gtk_widget_set_halign(lbl_timeout, GTK_ALIGN_END);
    sd->spin_timeout = gtk_spin_button_new_with_range(1, 120, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sd->spin_timeout), sd->cfg->http_timeout);
    gtk_grid_attach(GTK_GRID(grid), lbl_timeout, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sd->spin_timeout, 1, 4, 1, 1);

    /* Verify SSL */
    sd->chk_ssl = gtk_check_button_new_with_label("Verify SSL");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sd->chk_ssl), sd->cfg->http_verify_ssl);
    gtk_grid_attach(GTK_GRID(grid), sd->chk_ssl, 1, 5, 1, 1);

    /* Acceptable codes */
    GtkWidget *lbl_codes = gtk_label_new("Acceptable Codes:");
    gtk_widget_set_halign(lbl_codes, GTK_ALIGN_END);
    sd->entry_codes = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(sd->entry_codes), sd->cfg->http_acceptable_codes);
    gtk_widget_set_tooltip_text(sd->entry_codes, "Comma-separated HTTP status codes, e.g. 200,301,302");
    gtk_grid_attach(GTK_GRID(grid), lbl_codes, 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sd->entry_codes, 1, 6, 1, 1);

    /* Headers button */
    GtkWidget *btn_headers = gtk_button_new_with_label("Headers...");
    g_signal_connect(btn_headers, "clicked", G_CALLBACK(on_headers_clicked), sd);
    gtk_grid_attach(GTK_GRID(grid), btn_headers, 1, 7, 1, 1);

    return grid;
}

/* ---------- public function ---------- */

void settings_ui_show(Config *cfg, void (*on_save)(void)) {
    if (s_settings_dialog) {
        gtk_window_present(GTK_WINDOW(s_settings_dialog));
        return;
    }

    SettingsData *sd = g_new0(SettingsData, 1);
    sd->cfg = cfg;
    sd->on_save = on_save;
    strncpy(sd->http_headers, cfg->http_headers, sizeof(sd->http_headers) - 1);
    sd->http_headers[sizeof(sd->http_headers) - 1] = '\0';

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Settings",
                                                     NULL,
                                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Save", GTK_RESPONSE_ACCEPT,
                                                     NULL);
    s_settings_dialog = dialog;
    gtk_window_set_default_size(GTK_WINDOW(dialog), 460, 480);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    /* ---- Notebook (tabs) ---- */
    GtkWidget *notebook = gtk_notebook_new();

    GtkWidget *global_tab = build_global_tab(sd);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), global_tab, gtk_label_new("Global"));

    GtkWidget *icmp_tab = build_icmp_tab(sd);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), icmp_tab, gtk_label_new("ICMP"));

    GtkWidget *http_tab = build_http_tab(sd);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), http_tab, gtk_label_new("HTTP"));

    GtkWidget *logs_tab = build_logs_tab(sd);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), logs_tab, gtk_label_new("Logs"));

    gtk_container_add(GTK_CONTAINER(content_area), notebook);

    /* Wire up mutual exclusion after both checkboxes exist */
    g_signal_connect(sd->chk_icmp_enabled, "toggled",
                     G_CALLBACK(on_icmp_toggled_real), sd);
    g_signal_connect(sd->chk_http_enabled, "toggled",
                     G_CALLBACK(on_http_toggled), sd);

    /* Switch to the currently active mode's tab */
    if (cfg->check_mode == CHECK_MODE_HTTP)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 2);
    else
        gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 1);

    g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), sd);

    gtk_widget_show_all(dialog);
}

#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>
#include <gio/gio.h>
#include <stdbool.h>

#define CONFIG_DIR          ".config/internet-indicator"
#define CONFIG_FILE         "config"
#define DEFAULT_ADDRESS     "8.8.8.8"
#define DEFAULT_INTERVAL    1

typedef struct {
    char address[256];
    int  interval;          /* seconds between pings */

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

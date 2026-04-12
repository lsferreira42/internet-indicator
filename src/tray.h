#ifndef TRAY_H
#define TRAY_H

#include <stdbool.h>

/* Initialize the tray indicator. icon_dir is the absolute path to the
 * directory containing net-good.png and net-bad.png. */
bool tray_init(const char *icon_dir);

/* Set the callback invoked when the user clicks 'Configurações' */
void tray_set_config_callback(void (*cb)(void));

/* Update the tray icon. target is the currently checked URL/address.
 * latency_ms is the measured latency in milliseconds, or < 0 if failed or N/A. */
void tray_set_status(bool connected, const char *target, double latency_ms);

/* Set error message visible in the tray menu. Empty string to hide. */
void tray_set_error(const char *error_msg);

/* Clean up resources. */
void tray_destroy(void);

#endif /* TRAY_H */

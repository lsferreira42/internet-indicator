#ifndef TRAY_H
#define TRAY_H

#include <stdbool.h>

/* Initialize the tray indicator. icon_dir is the absolute path to the
 * directory containing net-good.png and net-bad.png. */
bool tray_init(const char *icon_dir);

/* Set the callback invoked when the user clicks 'Configurações' */
void tray_set_config_callback(void (*cb)(void));

/* Update the tray icon and tooltip.
 *   connected = true  → green icon
 *   connected = false → red icon
 * `target` is shown in the tooltip / menu label. */
void tray_set_status(bool connected, const char *target);

/* Clean up resources. */
void tray_destroy(void);

#endif /* TRAY_H */

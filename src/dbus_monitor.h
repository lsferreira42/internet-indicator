#ifndef DBUS_MONITOR_H
#define DBUS_MONITOR_H

#include "config.h"

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-bus.h>
#include <stdbool.h>

void dbus_monitor_init(Config *cfg);
void dbus_monitor_cleanup(void);
bool dbus_monitor_is_sleeping(void);
bool dbus_monitor_is_locked(void);
#endif

#endif // DBUS_MONITOR_H

#include "dbus_monitor.h"

#ifdef HAVE_LIBSYSTEMD
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <glib.h>

static sd_bus      *g_bus  = NULL;
static sd_bus_slot *g_slot = NULL;
static guint        g_bus_watch_id = 0;
static bool         g_is_sleeping = false;
static bool         g_is_locked   = false;
static char         g_session_path[512] = "";
static Config       *g_active_config = NULL;

static int on_prepare_for_sleep(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata;
    (void)ret_error;
    int going_down;
    int r = sd_bus_message_read(m, "b", &going_down);
    if (r < 0) return r;

    if (going_down) {
        g_is_sleeping = true;
        log_msg(LOG_STATUS, "System entering sleep mode");
    } else {
        g_is_sleeping = false;
        log_msg(LOG_STATUS, "System awake");
    }

    return 0;
}

static int on_session_lock(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)userdata;
    (void)ret_error;
    const char *member = sd_bus_message_get_member(m);

    if (strcmp(member, "Lock") == 0) {
        if (!g_is_locked) {
            g_is_locked = true;
            log_msg(LOG_STATUS, "Screen locked");
        }
    } else if (strcmp(member, "Unlock") == 0) {
        if (g_is_locked) {
            g_is_locked = false;
            log_msg(LOG_STATUS, "Screen unlocked");
        }
    } else if (strcmp(member, "PropertiesChanged") == 0) {
        const char *interface;
        int r = sd_bus_message_read(m, "s", &interface);
        if (r >= 0 && strcmp(interface, "org.freedesktop.login1.Session") == 0) {
            r = sd_bus_message_enter_container(m, 'a', "{sv}");
            if (r >= 0) {
                const char *key;
                while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
                    sd_bus_message_read(m, "s", &key);
                    if (strcmp(key, "LockedHint") == 0) {
                        int locked;
                        sd_bus_message_enter_container(m, 'v', "b");
                        sd_bus_message_read(m, "b", &locked);
                        sd_bus_message_exit_container(m);
                        
                        if (g_is_locked != (bool)locked) {
                            g_is_locked = (bool)locked;
                            log_msg(LOG_STATUS, "Screen %s", locked ? "locked" : "unlocked");
                        }
                    } else {
                        sd_bus_message_skip(m, "v");
                    }
                    sd_bus_message_exit_container(m);
                }
                sd_bus_message_exit_container(m);
            }
        }
    }
    return 0;
}

static gboolean sdbus_dispatch(GIOChannel *source, GIOCondition condition, gpointer data) {
    (void)source;
    (void)condition;
    sd_bus *bus = (sd_bus *)data;
    int r;
    do {
        r = sd_bus_process(bus, NULL);
    } while (r > 0);
    return TRUE;
}

void dbus_monitor_cleanup(void) {
    if (g_bus_watch_id) {
        g_source_remove(g_bus_watch_id);
        g_bus_watch_id = 0;
    }
    if (g_slot) {
        sd_bus_slot_unref(g_slot);
        g_slot = NULL;
    }
    if (g_bus) {
        sd_bus_flush_close_unref(g_bus);
        g_bus = NULL;
    }
    g_is_sleeping = false;
    g_is_locked = false;
    g_session_path[0] = '\0';
}

static void init_session_lock_detection(void) {
    if (!g_active_config || !g_active_config->lock_detection_enabled) return;

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *session_path = NULL;

    int r = sd_bus_call_method(
        g_bus, "org.freedesktop.login1",
        "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager",
        "GetSessionByPID",
        &error, &reply,
        "u", (uint32_t)getpid()
    );

    if (r >= 0) {
        r = sd_bus_message_read(reply, "o", &session_path);
        if (r >= 0) {
            strncpy(g_session_path, session_path, sizeof(g_session_path) - 1);
            g_session_path[sizeof(g_session_path) - 1] = '\0';
        }
    } else {
        const char *sid = getenv("XDG_SESSION_ID");
        if (sid) {
            sd_bus_error_free(&error);
            r = sd_bus_call_method(
                g_bus, "org.freedesktop.login1",
                "/org/freedesktop/login1",
                "org.freedesktop.login1.Manager",
                "GetSession",
                &error, &reply,
                "s", sid
            );
            if (r >= 0) {
                r = sd_bus_message_read(reply, "o", &session_path);
                if (r >= 0) {
                    strncpy(g_session_path, session_path, sizeof(g_session_path) - 1);
                    g_session_path[sizeof(g_session_path) - 1] = '\0';
                }
            } else {
                log_msg(LOG_ERROR, "GetSessionByPID and GetSession failed: %s", error.message);
            }
        } else {
            log_msg(LOG_ERROR, "GetSessionByPID failed: %s (and XDG_SESSION_ID not set)", error.message);
        }
        sd_bus_error_free(&error);
    }

    if (g_session_path[0]) {
        r = sd_bus_match_signal(
            g_bus, NULL,
            "org.freedesktop.login1",
            g_session_path,
            "org.freedesktop.login1.Session",
            "Lock",
            on_session_lock, NULL
        );
        if (r < 0) log_msg(LOG_ERROR, "failed to match Lock: %s", strerror(-r));

        r = sd_bus_match_signal(
            g_bus, NULL,
            "org.freedesktop.login1",
            g_session_path,
            "org.freedesktop.login1.Session",
            "Unlock",
            on_session_lock, NULL
        );
        if (r < 0) log_msg(LOG_ERROR, "failed to match Unlock: %s", strerror(-r));

        r = sd_bus_match_signal(
            g_bus, NULL,
            "org.freedesktop.login1",
            g_session_path,
            "org.freedesktop.DBus.Properties",
            "PropertiesChanged",
            on_session_lock, NULL
        );
        if (r < 0) log_msg(LOG_ERROR, "failed to match PropertiesChanged: %s", strerror(-r));

        int locked = 0;
        r = sd_bus_get_property_trivial(g_bus, "org.freedesktop.login1", g_session_path, "org.freedesktop.login1.Session", "LockedHint", NULL, 'b', &locked);
        if (r >= 0) {
            g_is_locked = (bool)locked;
        }
    }

    if (reply) sd_bus_message_unref(reply);
}

void dbus_monitor_init(Config *cfg) {
    g_active_config = cfg;
    dbus_monitor_cleanup();
    
    if (!cfg || (!cfg->sleep_detection_enabled && !cfg->lock_detection_enabled)) {
        return;
    }

    int r = sd_bus_default_system(&g_bus);
    if (r < 0) {
        log_msg(LOG_ERROR, "failed to connect to system bus: %s", strerror(-r));
        return;
    }

    if (cfg->sleep_detection_enabled) {
        r = sd_bus_match_signal(
            g_bus, &g_slot,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "PrepareForSleep",
            on_prepare_for_sleep, NULL
        );

        if (r < 0) {
            log_msg(LOG_ERROR, "failed to match sleep signal: %s", strerror(-r));
        }
    }

    init_session_lock_detection();

    int fd = sd_bus_get_fd(g_bus);
    if (fd >= 0) {
        GIOChannel *channel = g_io_channel_unix_new(fd);
        g_bus_watch_id = g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR, sdbus_dispatch, g_bus);
        g_io_channel_unref(channel);
    }
}

bool dbus_monitor_is_sleeping(void) {
    return g_is_sleeping;
}

bool dbus_monitor_is_locked(void) {
    return g_is_locked;
}

#endif

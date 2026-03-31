#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void build_config_path(Config *cfg) {
  gchar *path = g_build_filename(g_get_user_config_dir(), "internet-indicator", CONFIG_FILE, NULL);
  strncpy(cfg->config_path, path, sizeof(cfg->config_path) - 1);
  cfg->config_path[sizeof(cfg->config_path) - 1] = '\0';
  g_free(path);
}

static bool ensure_dir(const char *path) {
  if (g_mkdir_with_parents(path, 0755) != 0) {
    fprintf(stderr, "internet-indicator: cannot create directory %s: %s\n",
            path, strerror(errno));
    return false;
  }
  return true;
}

static bool write_defaults(const char *path) {
  FILE *fp = fopen(path, "w");
  if (!fp) {
    fprintf(stderr, "internet-indicator: cannot write %s: %s\n", path,
            strerror(errno));
    return false;
  }
  fprintf(fp,
          "[settings]\n"
          "address=%s\n"
          "interval=%d\n"
          "max_retries=%d\n"
          "log_enabled=%s\n"
          "sleep_detection_enabled=%s\n"
          "lock_detection_enabled=%s\n",
          DEFAULT_ADDRESS, DEFAULT_INTERVAL, DEFAULT_MAX_RETRIES, DEFAULT_LOG_ENABLED ? "true" : "false",
          DEFAULT_SLEEP_DETECTION_ENABLED ? "true" : "false",
          DEFAULT_LOCK_DETECTION_ENABLED ? "true" : "false");
  fclose(fp);
  return true;
}

static void trim(char *s) {
  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                     s[len - 1] == ' ' || s[len - 1] == '\t'))
    s[--len] = '\0';
}

static bool parse_ini(Config *cfg, const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp)
    return false;

  strncpy(cfg->address, DEFAULT_ADDRESS, sizeof(cfg->address) - 1);
  cfg->interval = DEFAULT_INTERVAL;
  cfg->max_retries = DEFAULT_MAX_RETRIES;
  cfg->log_enabled = DEFAULT_LOG_ENABLED;
  cfg->sleep_detection_enabled = DEFAULT_SLEEP_DETECTION_ENABLED;
  cfg->lock_detection_enabled = DEFAULT_LOCK_DETECTION_ENABLED;

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    trim(line);

    if (line[0] == '#' || line[0] == ';' || line[0] == '[' || line[0] == '\0')
      continue;

    char *eq = strchr(line, '=');
    if (!eq)
      continue;

    *eq = '\0';
    char *key = line;
    char *value = eq + 1;
    trim(key);

    if (strcmp(key, "address") == 0) {
      strncpy(cfg->address, value, sizeof(cfg->address) - 1);
      cfg->address[sizeof(cfg->address) - 1] = '\0';
    } else if (strcmp(key, "interval") == 0) {
      int v = atoi(value);
      if (v > 0)
        cfg->interval = v;
    } else if (strcmp(key, "max_retries") == 0) {
      int v = atoi(value);
      if (v >= 0)
        cfg->max_retries = v;
    } else if (strcmp(key, "log_enabled") == 0) {
      if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0)
        cfg->log_enabled = true;
      else
        cfg->log_enabled = false;
    } else if (strcmp(key, "sleep_detection_enabled") == 0) {
      if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0)
        cfg->sleep_detection_enabled = true;
      else
        cfg->sleep_detection_enabled = false;
    } else if (strcmp(key, "lock_detection_enabled") == 0) {
      if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0)
        cfg->lock_detection_enabled = true;
      else
        cfg->lock_detection_enabled = false;
    }
  }

  fclose(fp);
  return true;
}

bool config_init(Config *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  build_config_path(cfg);

  gchar *dir = g_build_filename(g_get_user_config_dir(), "internet-indicator", NULL);
  if (!ensure_dir(dir)) {
    g_free(dir);
    return false;
  }
  g_free(dir);

  if (!g_file_test(cfg->config_path, G_FILE_TEST_EXISTS)) {
    if (!write_defaults(cfg->config_path))
      return false;
    printf("internet-indicator: created default config at %s\n",
           cfg->config_path);
  }

  if (!parse_ini(cfg, cfg->config_path)) {
    fprintf(stderr, "internet-indicator: failed to parse %s\n",
            cfg->config_path);
    return false;
  }

  printf("internet-indicator: target=%s  interval=%ds  max_retries=%d\n", cfg->address,
         cfg->interval, cfg->max_retries);
  return true;
}

bool config_reload(Config *cfg) {
  if (!parse_ini(cfg, cfg->config_path)) {
    fprintf(stderr, "internet-indicator: failed to reload %s\n",
            cfg->config_path);
    return false;
  }
  printf("internet-indicator: config reloaded → target=%s  interval=%ds  max_retries=%d\n",
         cfg->address, cfg->interval, cfg->max_retries);
  return true;
}

bool config_save(const Config *cfg) {
  gchar *dir = g_build_filename(g_get_user_config_dir(), "internet-indicator", NULL);
  if (!ensure_dir(dir)) {
    g_free(dir);
    return false;
  }
  g_free(dir);

  FILE *fp = fopen(cfg->config_path, "w");
  if (!fp) {
    fprintf(stderr, "internet-indicator: cannot save %s: %s\n",
            cfg->config_path, strerror(errno));
    return false;
  }
  fprintf(fp,
          "[settings]\n"
          "address=%s\n"
          "interval=%d\n"
          "max_retries=%d\n"
          "log_enabled=%s\n"
          "sleep_detection_enabled=%s\n"
          "lock_detection_enabled=%s\n",
          cfg->address, cfg->interval, cfg->max_retries, cfg->log_enabled ? "true" : "false",
          cfg->sleep_detection_enabled ? "true" : "false",
          cfg->lock_detection_enabled ? "true" : "false");
  fclose(fp);
  return true;
}

static void on_file_changed(GFileMonitor *monitor, GFile *file G_GNUC_UNUSED,
                            GFile *other G_GNUC_UNUSED, GFileMonitorEvent event,
                            gpointer user_data G_GNUC_UNUSED) {
  if (event == G_FILE_MONITOR_EVENT_CHANGED ||
      event == G_FILE_MONITOR_EVENT_CREATED ||
      event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {

    GCallback cb =
        G_CALLBACK(g_object_get_data(G_OBJECT(monitor), "user-callback"));
    gpointer ud = g_object_get_data(G_OBJECT(monitor), "user-data");
    if (cb) {
      ((void (*)(gpointer))cb)(ud);
    }
  }
}

void config_watch(Config *cfg, GCallback callback, gpointer user_data) {
  GFile *file = g_file_new_for_path(cfg->config_path);
  GError *error = NULL;

  cfg->monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, &error);
  g_object_unref(file);

  if (!cfg->monitor) {
    fprintf(stderr, "internet-indicator: cannot watch config: %s\n",
            error->message);
    g_error_free(error);
    return;
  }

  g_object_set_data(G_OBJECT(cfg->monitor), "user-callback",
                    (gpointer)callback);
  g_object_set_data(G_OBJECT(cfg->monitor), "user-data", user_data);

  cfg->monitor_handler_id = g_signal_connect(
      cfg->monitor, "changed", G_CALLBACK(on_file_changed), user_data);

  g_file_monitor_set_rate_limit(cfg->monitor, 1000);
}

void config_destroy(Config *cfg) {
  if (cfg->monitor) {
    g_file_monitor_cancel(cfg->monitor);
    g_object_unref(cfg->monitor);
    cfg->monitor = NULL;
  }
}

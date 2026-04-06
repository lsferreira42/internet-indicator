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
          "[global]\n"
          "interval=%d\n"
          "max_retries=%d\n"
          "retry_delay=%d\n"
          "log_enabled=%s\n"
          "sleep_detection_enabled=%s\n"
          "lock_detection_enabled=%s\n"
          "\n"
          "[icmp]\n"
          "enabled=true\n"
          "address=%s\n"
          "\n"
          "[http]\n"
          "enabled=false\n"
          "url=%s\n"
          "port=%d\n"
          "verify_ssl=%s\n"
          "acceptable_codes=%s\n"
          "method=%s\n"
          "timeout=%d\n"
          "headers=%s\n",
          DEFAULT_INTERVAL, DEFAULT_MAX_RETRIES, DEFAULT_RETRY_DELAY,
          DEFAULT_LOG_ENABLED ? "true" : "false",
          DEFAULT_SLEEP_DETECTION_ENABLED ? "true" : "false",
          DEFAULT_LOCK_DETECTION_ENABLED ? "true" : "false",
          DEFAULT_ADDRESS,
          DEFAULT_HTTP_URL, DEFAULT_HTTP_PORT,
          DEFAULT_HTTP_VERIFY_SSL ? "true" : "false",
          DEFAULT_HTTP_ACCEPTABLE_CODES,
          DEFAULT_HTTP_METHOD,
          DEFAULT_HTTP_TIMEOUT,
          DEFAULT_HTTP_HEADERS);
  fclose(fp);
  return true;
}

static void trim(char *s) {
  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                     s[len - 1] == ' ' || s[len - 1] == '\t'))
    s[--len] = '\0';
}

/* Unescape \\n sequences into real newlines (for headers stored as single line) */
static void unescape_newlines(char *s) {
  char *r = s, *w = s;
  while (*r) {
    if (r[0] == '\\' && r[1] == 'n') {
      *w++ = '\n';
      r += 2;
    } else {
      *w++ = *r++;
    }
  }
  *w = '\0';
}

/* Escape real newlines into \\n for single-line storage */
static void escape_newlines(const char *src, char *dst, size_t dst_size) {
  size_t j = 0;
  for (size_t i = 0; src[i] && j < dst_size - 2; i++) {
    if (src[i] == '\n') {
      dst[j++] = '\\';
      dst[j++] = 'n';
    } else {
      dst[j++] = src[i];
    }
  }
  dst[j] = '\0';
}

typedef enum { SECTION_NONE, SECTION_GLOBAL, SECTION_ICMP, SECTION_HTTP } IniSection;

static bool parse_ini(Config *cfg, const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp)
    return false;

  /* Set defaults */
  strncpy(cfg->address, DEFAULT_ADDRESS, sizeof(cfg->address) - 1);
  cfg->interval = DEFAULT_INTERVAL;
  cfg->max_retries = DEFAULT_MAX_RETRIES;
  cfg->retry_delay = DEFAULT_RETRY_DELAY;
  cfg->log_enabled = DEFAULT_LOG_ENABLED;
  cfg->sleep_detection_enabled = DEFAULT_SLEEP_DETECTION_ENABLED;
  cfg->lock_detection_enabled = DEFAULT_LOCK_DETECTION_ENABLED;
  cfg->check_mode = DEFAULT_CHECK_MODE;

  strncpy(cfg->http_url, DEFAULT_HTTP_URL, sizeof(cfg->http_url) - 1);
  cfg->http_port = DEFAULT_HTTP_PORT;
  cfg->http_verify_ssl = DEFAULT_HTTP_VERIFY_SSL;
  strncpy(cfg->http_acceptable_codes, DEFAULT_HTTP_ACCEPTABLE_CODES, sizeof(cfg->http_acceptable_codes) - 1);
  strncpy(cfg->http_method, DEFAULT_HTTP_METHOD, sizeof(cfg->http_method) - 1);
  cfg->http_timeout = DEFAULT_HTTP_TIMEOUT;
  cfg->http_headers[0] = '\0';

  IniSection section = SECTION_NONE;
  char line[2048];
  while (fgets(line, sizeof(line), fp)) {
    trim(line);

    if (line[0] == '#' || line[0] == ';' || line[0] == '\0')
      continue;

    /* Detect section headers */
    if (line[0] == '[') {
      if (strcmp(line, "[global]") == 0)
        section = SECTION_GLOBAL;
      else if (strcmp(line, "[icmp]") == 0)
        section = SECTION_ICMP;
      else if (strcmp(line, "[http]") == 0)
        section = SECTION_HTTP;
      else
        section = SECTION_NONE;
      continue;
    }

    char *eq = strchr(line, '=');
    if (!eq)
      continue;

    *eq = '\0';
    char *key = line;
    char *value = eq + 1;
    trim(key);

    if (section == SECTION_GLOBAL) {
      if (strcmp(key, "interval") == 0) {
        int v = atoi(value);
        if (v > 0) cfg->interval = v;
      } else if (strcmp(key, "max_retries") == 0) {
        int v = atoi(value);
        if (v >= 0) cfg->max_retries = v;
      } else if (strcmp(key, "retry_delay") == 0) {
        int v = atoi(value);
        if (v > 0) cfg->retry_delay = v;
      } else if (strcmp(key, "log_enabled") == 0) {
        cfg->log_enabled = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
      } else if (strcmp(key, "sleep_detection_enabled") == 0) {
        cfg->sleep_detection_enabled = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
      } else if (strcmp(key, "lock_detection_enabled") == 0) {
        cfg->lock_detection_enabled = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
      }
    } else if (section == SECTION_ICMP) {
      if (strcmp(key, "enabled") == 0) {
        if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0)
          cfg->check_mode = CHECK_MODE_ICMP;
      } else if (strcmp(key, "address") == 0) {
        strncpy(cfg->address, value, sizeof(cfg->address) - 1);
        cfg->address[sizeof(cfg->address) - 1] = '\0';
      }
    } else if (section == SECTION_HTTP) {
      if (strcmp(key, "enabled") == 0) {
        if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0)
          cfg->check_mode = CHECK_MODE_HTTP;
      } else if (strcmp(key, "url") == 0) {
        strncpy(cfg->http_url, value, sizeof(cfg->http_url) - 1);
        cfg->http_url[sizeof(cfg->http_url) - 1] = '\0';
      } else if (strcmp(key, "port") == 0) {
        cfg->http_port = atoi(value);
      } else if (strcmp(key, "verify_ssl") == 0) {
        cfg->http_verify_ssl = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
      } else if (strcmp(key, "acceptable_codes") == 0) {
        strncpy(cfg->http_acceptable_codes, value, sizeof(cfg->http_acceptable_codes) - 1);
        cfg->http_acceptable_codes[sizeof(cfg->http_acceptable_codes) - 1] = '\0';
      } else if (strcmp(key, "method") == 0) {
        strncpy(cfg->http_method, value, sizeof(cfg->http_method) - 1);
        cfg->http_method[sizeof(cfg->http_method) - 1] = '\0';
      } else if (strcmp(key, "timeout") == 0) {
        int v = atoi(value);
        if (v > 0) cfg->http_timeout = v;
      } else if (strcmp(key, "headers") == 0) {
        strncpy(cfg->http_headers, value, sizeof(cfg->http_headers) - 1);
        cfg->http_headers[sizeof(cfg->http_headers) - 1] = '\0';
        unescape_newlines(cfg->http_headers);
      }
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

  printf("internet-indicator: mode=%s  target=%s  interval=%ds  max_retries=%d\n",
         cfg->check_mode == CHECK_MODE_HTTP ? "http" : "icmp",
         cfg->check_mode == CHECK_MODE_HTTP ? cfg->http_url : cfg->address,
         cfg->interval, cfg->max_retries);
  return true;
}

bool config_reload(Config *cfg) {
  if (!parse_ini(cfg, cfg->config_path)) {
    fprintf(stderr, "internet-indicator: failed to reload %s\n",
            cfg->config_path);
    return false;
  }
  printf("internet-indicator: config reloaded → mode=%s  target=%s  interval=%ds  max_retries=%d\n",
         cfg->check_mode == CHECK_MODE_HTTP ? "http" : "icmp",
         cfg->check_mode == CHECK_MODE_HTTP ? cfg->http_url : cfg->address,
         cfg->interval, cfg->max_retries);
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

  /* Escape newlines in headers for single-line storage */
  char escaped_headers[4096];
  escape_newlines(cfg->http_headers, escaped_headers, sizeof(escaped_headers));

  fprintf(fp,
          "[global]\n"
          "interval=%d\n"
          "max_retries=%d\n"
          "retry_delay=%d\n"
          "log_enabled=%s\n"
          "sleep_detection_enabled=%s\n"
          "lock_detection_enabled=%s\n"
          "\n"
          "[icmp]\n"
          "enabled=%s\n"
          "address=%s\n"
          "\n"
          "[http]\n"
          "enabled=%s\n"
          "url=%s\n"
          "port=%d\n"
          "verify_ssl=%s\n"
          "acceptable_codes=%s\n"
          "method=%s\n"
          "timeout=%d\n"
          "headers=%s\n",
          cfg->interval, cfg->max_retries, cfg->retry_delay,
          cfg->log_enabled ? "true" : "false",
          cfg->sleep_detection_enabled ? "true" : "false",
          cfg->lock_detection_enabled ? "true" : "false",
          cfg->check_mode == CHECK_MODE_ICMP ? "true" : "false",
          cfg->address,
          cfg->check_mode == CHECK_MODE_HTTP ? "true" : "false",
          cfg->http_url, cfg->http_port,
          cfg->http_verify_ssl ? "true" : "false",
          cfg->http_acceptable_codes,
          cfg->http_method,
          cfg->http_timeout,
          escaped_headers);
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

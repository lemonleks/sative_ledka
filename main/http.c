#include <errno.h>
#include <esp_http_server.h>
#include <inttypes.h>
#include <stdarg.h>

#include "main.h"

static const char *query_last_key = NULL;

#define PRINT(BUF, P, ...) P += snprintf(P, BUF + sizeof BUF - P, __VA_ARGS__)
#define FLUSH()                                                                \
  do {                                                                         \
    if (p != buf)                                                              \
      httpd_resp_send_chunk(req, buf, p - buf);                                \
    p = buf;                                                                   \
  } while (0)
#define CHECK_ERR(ERR, MSG, ...)                                               \
  do {                                                                         \
    if ((ERR) != ESP_OK)                                                       \
      return return_error(req, (ERR), __FILE__, __LINE__, (MSG),               \
                          ##__VA_ARGS__);                                      \
  } while (0)
#define CHECK_ERR_QUERY_VALUE(ERR, REQUIRED)                                   \
  do {                                                                         \
    if ((ERR) != ESP_OK && ((REQUIRED) || (ERR) != ESP_ERR_NOT_FOUND))         \
      return return_error(req, (ERR), __FILE__, __LINE__,                      \
                          "Invalid value for '%s'", query_last_key);           \
  } while (0)

static int hexdig(char c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                : -1;
}

static void uri_decode(char *buf) {
  char *out = buf;
  int h1, h2;
  while (*buf)
    if (*buf == '%' && (h1 = hexdig(buf[1])) >= 0 && (h2 = hexdig(buf[2])) >= 0)
      *out++ = (h1 << 4) | h2, buf += 3;
    else if (*buf == '+')
      *out++ = ' ', buf++;
    else
      *out++ = *buf++;
  *out = '\0';
}

static esp_err_t return_error(httpd_req_t *req, esp_err_t err, const char *file,
                              int line, const char *msg, ...) {
  httpd_resp_set_status(req, "400 Bad Request");
  char buf[1024], *p = buf;
  PRINT(buf, p, "Error %d at %s:%d\n", err, file, line);

  va_list args;
  va_start(args, msg);
  p += vsnprintf(p, buf + sizeof buf - p, msg, args);
  va_end(args);
  PRINT(buf, p, "\n");

  esp_err_to_name_r(err, p, buf + sizeof buf - p);
  p += strlen(p);
  PRINT(buf, p, "\n");

  httpd_resp_send(req, buf, p - buf);
  return ESP_OK;
}

static esp_err_t query_get_value_str(const char *qry, const char *key,
                                     char *value, size_t value_len) {
  query_last_key = key;
  esp_err_t rc = httpd_query_key_value(qry, key, value, value_len);
  if (rc == ESP_OK)
    uri_decode(value);
  return rc;
}

static esp_err_t query_get_value_bool(const char *qry, const char *key,
                                      char *buf, size_t buf_len, bool *val) {
  esp_err_t rc = query_get_value_str(qry, key, buf, buf_len);
  if (rc == ESP_OK) {
    if (!strcmp(buf, "on"))
      return *val = true, ESP_OK;
    if (!strcmp(buf, "off"))
      return *val = false, ESP_OK;
    return ESP_ERR_INVALID_ARG;
  }
  return rc;
}

static esp_err_t query_get_value_ull(const char *qry, const char *key,
                                     char *buf, size_t buf_len,
                                     unsigned long long *val) {
  esp_err_t rc = query_get_value_str(qry, key, buf, buf_len);
  if (rc == ESP_OK) {
    char *end;
    errno = 0;
    *val = strtoull(buf, &end, 10);
    if (*end || errno)
      return ESP_ERR_INVALID_ARG;
  }
  return rc;
}

static esp_err_t get_handler(httpd_req_t *req) {
  char buf[1024], *p = buf;
  PRINT(buf, p, "Hello, world!\n");
  PRINT(buf, p, "Time: %lld\n", esp_timer_get_time());
  PRINT(buf, p, "Free heap: %d\n", esp_get_free_heap_size());

  httpd_resp_send(req, buf, p - buf);
  return ESP_OK;
}

static esp_err_t get_stats_handler(httpd_req_t *req) {
  char buf[1024], *p = buf;
  PRINT(buf, p, "{\n  \"data_update_time\": [");
  const size_t n =
      sizeof stats.data_update_time / sizeof *stats.data_update_time;
  for (size_t i = 0; i < n; i++) {
    if (i != 0)
      PRINT(buf, p, ",");
    PRINT(buf, p, "%" PRIu32,
          stats.data_update_time[(i + stats.data_update_time_pos) % n]);
    if (p - buf > sizeof buf - 32)
      FLUSH();
  }
  FLUSH();
  PRINT(buf, p, "]\n}\n");
  FLUSH();
  httpd_resp_send_chunk(req, buf, 0);
  return ESP_OK;
}

static esp_err_t post_data_handler(httpd_req_t *req) {
  char buf[1024], *p = buf;
  uint8_t data[sizeof data1];
  if (req->content_len != sizeof data) {
    httpd_resp_set_status(req, "400 Bad Request");
    PRINT(buf, p, "Invalid content length: %d, expected %d\n", req->content_len,
          sizeof data);
    httpd_resp_send(req, buf, p - buf);
    return ESP_FAIL;
  }

  if (httpd_req_recv(req, (char *)data, sizeof data) != sizeof data) {
    httpd_resp_set_status(req, "400 Bad Request");
    PRINT(buf, p, "Invalid content length: %d, expected %d\n", req->content_len,
          sizeof data);
    httpd_resp_send(req, buf, p - buf);
    return ESP_FAIL;
  }

  xSemaphoreTake(data_mutex, portMAX_DELAY);
  const int64_t t = esp_timer_get_time();
  stats.data_update_time[stats.data_update_time_pos++] =
      t - stats.last_data_time;
  stats.last_data_time = t;
  stats.data_update_time_pos %=
      sizeof stats.data_update_time / sizeof *stats.data_update_time;

  data1_active = !data1_active;
  memcpy(data1_active ? data1 : data2, data, sizeof data);
  xSemaphoreGive(data_mutex);

  PRINT(buf, p, "OK\n");
  httpd_resp_send(req, buf, p - buf);
  return ESP_OK;
}

static esp_err_t post_config_handler(httpd_req_t *req) {
  // XXX: using static to avoid stack overflow
  static char query[512], value[512];
  bool bval;
  unsigned long long ullval;

  esp_err_t err = httpd_req_get_url_query_str(req, query, sizeof query);
  CHECK_ERR(err, "Failed to get query string");

  err = query_get_value_bool(query, "gol", value, sizeof value, &bval);
  if (err == ESP_OK)
    config.gol_enabled = bval;
  CHECK_ERR_QUERY_VALUE(err, false);

  err = query_get_value_bool(query, "bars", value, sizeof value, &bval);
  if (err == ESP_OK)
    config.bars_enabled = bval;
  CHECK_ERR_QUERY_VALUE(err, false);

  err = httpd_query_key_value(query, "topo", value, sizeof value);
  if (err == ESP_OK) {
    uint8_t topo[PANELS_X * PANELS_Y] = {0}, *p = topo;
    for (char *pv = value; *pv; pv++) {
      printf("Handling '%c'\n", *pv);
      if (*pv == ',' && p < topo + sizeof topo - 1)
        p++;
      else if (*pv >= '0' && *pv <= '9')
        *p = *p * 10 + (*pv - '0');
      else if (*pv == 'r')
        *p |= 0x80;
      else
        goto error;
    }
    if (p != topo + sizeof topo - 1) {
    error:
      printf("Wrong size: %d\n", p - topo);
      return return_error(req, ESP_ERR_INVALID_ARG, __FILE__, __LINE__,
                          "Invalid value for 'topo': %s", value);
    }

    draw_panels_hint(topo);
    text_timeout = 500;
    char error[128];
    error[0] = 0;
    if (!ledmx_mktopo(topo, error))
      return return_error(req, ESP_ERR_INVALID_ARG, __FILE__, __LINE__,
                          "Invalid value for 'topo': %s", error);
  }

  err = query_get_value_str(query, "field", value, sizeof value);
  if (err == ESP_OK) {
    extern char field_config[128];
    memset(field_config, 0, sizeof field_config);
    // TODO: check length
    strcpy(field_config, value);
  }
  CHECK_ERR_QUERY_VALUE(err, false);

  err = query_get_value_ull(query, "timer", value, sizeof value, &ullval);
  if (err == ESP_OK) {
    extern esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_stop(timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, ullval));
  }
  CHECK_ERR_QUERY_VALUE(err, false);

  httpd_resp_send(req, "got it", -1);
  return ESP_OK;
}

static esp_err_t post_text_handler(httpd_req_t *req) {
  char query[512], font[20], value[512];
  esp_err_t err = httpd_req_get_url_query_str(req, query, sizeof query);
  CHECK_ERR(err, "Failed to get query string");

  unsigned long long timeout = 100, pos_x = 0, pos_y = 0;
  strcpy(font, "BMplain");

  err = query_get_value_ull(query, "timeout", value, sizeof value, &timeout);
  CHECK_ERR_QUERY_VALUE(err, false);
  err = query_get_value_ull(query, "x", value, sizeof value, &pos_x);
  CHECK_ERR_QUERY_VALUE(err, false);
  err = query_get_value_ull(query, "y", value, sizeof value, &pos_y);
  CHECK_ERR_QUERY_VALUE(err, false);
  err = query_get_value_str(query, "font", font, sizeof font);
  CHECK_ERR_QUERY_VALUE(err, false);
  err = query_get_value_str(query, "text", value, sizeof value);
  CHECK_ERR_QUERY_VALUE(err, true);

  const struct font_t *font_ = find_font_by_name(font);
  if (!font_)
    return return_error(req, ESP_ERR_INVALID_ARG, __FILE__, __LINE__,
                        "Font not found: %s", font);
  reset_text();
  draw_text(font_, value, pos_x, pos_y, 0);
  text_timeout = timeout;

  httpd_resp_send(req, "got it", -1);
  return ESP_OK;
}

static httpd_uri_t uri_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = get_handler,
    .user_ctx = NULL,
};

static httpd_uri_t uri_get_stats = {
    .uri = "/stats",
    .method = HTTP_GET,
    .handler = get_stats_handler,
    .user_ctx = NULL,
};

static httpd_uri_t uri_post_data = {
    .uri = "/data",
    .method = HTTP_POST,
    .handler = post_data_handler,
    .user_ctx = NULL,
};

static httpd_uri_t uri_post_config = {
    .uri = "/config",
    .method = HTTP_POST,
    .handler = post_config_handler,
    .user_ctx = NULL,
};

static httpd_uri_t uri_post_text = {
    .uri = "/text",
    .method = HTTP_POST,
    .handler = post_text_handler,
    .user_ctx = NULL,
};

void http_start(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;
  ESP_ERROR_CHECK(httpd_start(&server, &config));
  httpd_register_uri_handler(server, &uri_get);
  httpd_register_uri_handler(server, &uri_get_stats);
  httpd_register_uri_handler(server, &uri_post_data);
  httpd_register_uri_handler(server, &uri_post_config);
  httpd_register_uri_handler(server, &uri_post_text);
}

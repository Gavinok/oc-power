#include "wifi_log_server.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "WIFI_LOG"
#define LOG_BUF_SIZE 256   /* max chars per log line (truncated if longer) */
#define LOG_QUEUE_DEPTH 32 /* lines queued before drops */
#define MAX_WS_CLIENTS 4

/* ---- embedded HTML viewer ---- */
static const char HTML_PAGE[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
    "<title>PowerMeter Log</title>"
    "<style>"
    "body{background:#111;color:#0f0;font:12px/1.4 "
    "monospace;margin:0;padding:44px 8px 8px}"
    "#toolbar{position:fixed;top:0;left:0;right:0;background:#111;padding:8px;"
    "border-bottom:1px solid #333;z-index:1}"
    "#log{white-space:pre-wrap;word-break:break-all}"
    "button{margin-right:6px;padding:4px 12px;cursor:pointer}"
    "button.on{color:#ff0;border-color:#ff0}"
    "#settings{display:none;position:fixed;top:40px;left:0;right:0;background:#1a1a1a;"
    "padding:10px 8px;border-bottom:1px solid #444;z-index:2}"
    "#settings label{margin-right:14px;white-space:nowrap}"
    "#settings input{width:60px;background:#333;color:#0f0;border:1px solid #555;padding:2px 4px}"
    "#settings select{background:#333;color:#0f0;border:1px solid #555;padding:2px}"
    "</style></head><body>"
    "<div id='toolbar'>"
    "<button "
    "onclick=\"document.getElementById('log').textContent=''\">Clear</button>"
    "<button onclick=\"ws.send('recalibrate')\">Recalibrate</button>"
    "<button id='vbtn' onclick=\"var v=this.classList.toggle('on');"
    "ws.send(v?'verbose:on':'verbose:off');"
    "this.textContent='Verbose: '+(v?'ON':'OFF')\">"
    "Verbose: OFF</button>"
    "<button onclick=\"toggleSettings()\">Settings</button>"
    "</div>"
    "<div id='settings'>"
    "<label>Mass (kg):<input id='sm' type='number' step='1' min='10' max='500' "
    "value='100'></label> "
    "<label>Axis:<select id='sa'>"
    "<option>+X</option><option>-X</option>"
    "<option selected>+Y</option><option>-Y</option>"
    "<option>+Z</option><option>-Z</option>"
    "</select></label> "
    "<label>Catch (g):<input id='sc' type='number' step='0.01' min='0.05' max='2.00' "
    "value='0.30'></label> "
    "<label>Recov (g):<input id='sr' type='number' step='0.01' min='0.01' max='1.00' "
    "value='0.10'></label> "
    "<label>Smooth (strokes):<input id='ss' type='number' step='1' min='1' max='8' "
    "value='3'></label> "
    "<label>Zero timeout (s):<input id='st' type='number' step='1' min='0' max='30' "
    "value='5' title='0=never zero'></label> "
    "<button onclick=\"applySettings()\">Apply</button>"
    "</div>"
    "<div id='log'></div>"
    "<script>"
    "var log=document.getElementById('log');"
    "var ws=new WebSocket('ws://'+location.host+'/ws');"
    "ws.onmessage=function(e){"
    "if(e.data[0]==='!'){"
    "var on=e.data==='!verbose:on';"
    "var b=document.getElementById('vbtn');"
    "b.classList.toggle('on',on);b.textContent='Verbose: '+(on?'ON':'OFF');"
    "return;}"
    "log.textContent+=e.data;window.scrollTo(0,document.body.scrollHeight)};"
    "ws.onclose=function(){log.textContent+='\\n[disconnected]\\n'};"
    "function toggleSettings(){"
    "var s=document.getElementById('settings');"
    "s.style.display=s.style.display==='none'?'block':'none';}"
    "function applySettings(){"
    "ws.send('set:mass:'+document.getElementById('sm').value);"
    "ws.send('set:axis:'+document.getElementById('sa').value);"
    "ws.send('set:catch:'+parseFloat(document.getElementById('sc').value).toFixed(3));"
    "ws.send('set:recovery:'+parseFloat(document.getElementById('sr').value).toFixed(3));"
    "ws.send('set:smooth:'+parseInt(document.getElementById('ss').value));"
    "ws.send('set:timeout:'+parseInt(document.getElementById('st').value));}"
    "</script></body></html>";

/* ---- state ---- */
typedef struct {
  char data[LOG_BUF_SIZE];
  int len;
} log_entry_t;

static QueueHandle_t s_log_queue;
static httpd_handle_t s_hd;
static int s_ws_fds[MAX_WS_CLIENTS];
static SemaphoreHandle_t s_fd_mutex;
static vprintf_like_t s_orig_vprintf;
static ws_command_cb_t s_command_cb;
static char s_connect_status[64];

void wifi_log_server_set_command_cb(ws_command_cb_t cb) {
  s_command_cb = cb;
}

void wifi_log_server_set_status(const char* msg) {
  strlcpy(s_connect_status, msg ? msg : "", sizeof(s_connect_status));
}

/* ---- fd list helpers ---- */

static void fd_add(int fd) {
  xSemaphoreTake(s_fd_mutex, portMAX_DELAY);
  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (s_ws_fds[i] < 0) {
      s_ws_fds[i] = fd;
      break;
    }
  }
  xSemaphoreGive(s_fd_mutex);
}

static void fd_remove(int fd) {
  xSemaphoreTake(s_fd_mutex, portMAX_DELAY);
  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (s_ws_fds[i] == fd) {
      s_ws_fds[i] = -1;
      break;
    }
  }
  xSemaphoreGive(s_fd_mutex);
}

/* ---- vprintf hook ---- */
/* Called from any task context; must be fast and non-blocking. */

static int log_vprintf_hook(const char* fmt, va_list args) {
  /* Preserve original UART output */
  va_list args2;
  va_copy(args2, args);
  int ret = s_orig_vprintf(fmt, args);

  /* Queue for WebSocket. Drop if full rather than block. */
  log_entry_t entry;
  entry.len = vsnprintf(entry.data, sizeof(entry.data) - 1, fmt, args2);
  va_end(args2);
  if (entry.len < 0)
    entry.len = 0;
  if (entry.len >= (int)sizeof(entry.data))
    entry.len = sizeof(entry.data) - 1;
  entry.data[entry.len] = '\0';
  xQueueSend(s_log_queue, &entry, 0);

  return ret;
}

/* ---- sender task ---- */
/* Drains the log queue and broadcasts each line to all connected WS clients. */

static void log_sender_task(void* arg) {
  log_entry_t entry;
  while (1) {
    if (xQueueReceive(s_log_queue, &entry, portMAX_DELAY) != pdTRUE)
      continue;

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t*)entry.data,
        .len = (size_t)entry.len,
        .final = true,
    };

    xSemaphoreTake(s_fd_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
      int fd = s_ws_fds[i];
      if (fd < 0)
        continue;
      esp_err_t err = httpd_ws_send_frame_async(s_hd, fd, &frame);
      if (err != ESP_OK) {
        s_ws_fds[i] = -1; /* dead client, skip logging to avoid re-entrancy */
      }
    }
    xSemaphoreGive(s_fd_mutex);
  }
}

/* ---- HTTP handlers ---- */

static esp_err_t root_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t ws_handler(httpd_req_t* req) {
  if (req->method == HTTP_GET) {
    /* New WebSocket client. httpd handles the upgrade; record the fd. */
    int fd = httpd_req_to_sockfd(req);
    fd_add(fd);
    ESP_LOGI(TAG, "WS client connected fd=%d", fd);
    /* Send current status so the browser can restore button state after refresh
     */
    if (s_connect_status[0]) {
      httpd_ws_frame_t sf = {
          .type = HTTPD_WS_TYPE_TEXT,
          .payload = (uint8_t*)s_connect_status,
          .len = strlen(s_connect_status),
          .final = true,
      };
      httpd_ws_send_frame(req, &sf);
    }
    return ESP_OK;
  }

  /* Read frame header to get type and length */
  httpd_ws_frame_t frame = {0};
  esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
  if (ret != ESP_OK)
    return ret;

  if (frame.type == HTTPD_WS_TYPE_TEXT && frame.len > 0) {
    uint8_t* buf = calloc(1, frame.len + 1);
    if (buf) {
      frame.payload = buf;
      ret = httpd_ws_recv_frame(req, &frame, frame.len);
      if (ret == ESP_OK && s_command_cb) {
        s_command_cb((const char*)buf);
      }
      free(buf);
    }
  } else if (frame.type == HTTPD_WS_TYPE_CLOSE) {
    int fd = httpd_req_to_sockfd(req);
    fd_remove(fd);
    ESP_LOGI(TAG, "WS client disconnected fd=%d", fd);
  }
  return ESP_OK;
}

/* ---- public API ---- */

void wifi_log_server_start(const char* ssid, const char* password) {
  /* Init fd list */
  s_fd_mutex = xSemaphoreCreateMutex();
  for (int i = 0; i < MAX_WS_CLIENTS; i++) s_ws_fds[i] = -1;

  /* Log queue */
  s_log_queue = xQueueCreate(LOG_QUEUE_DEPTH, sizeof(log_entry_t));

  /* WiFi SoftAP */
  ESP_ERROR_CHECK(esp_netif_init());
  esp_err_t loop_err = esp_event_loop_create_default();
  if (loop_err != ESP_ERR_INVALID_STATE)
    ESP_ERROR_CHECK(loop_err);

  esp_netif_create_default_wifi_ap();

  wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

  wifi_config_t ap_cfg = {
      .ap =
          {
              .max_connection = MAX_WS_CLIENTS,
              .authmode = (password && strlen(password)) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
          },
  };
  strlcpy((char*)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid));
  ap_cfg.ap.ssid_len = (uint8_t)strlen(ssid);
  if (password && strlen(password)) {
    strlcpy((char*)ap_cfg.ap.password, password, sizeof(ap_cfg.ap.password));
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "SoftAP started: SSID=\"%s\"  IP=192.168.4.1", ssid);

  /* HTTP + WebSocket server */
  httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
  http_cfg.lru_purge_enable = true;

  if (httpd_start(&s_hd, &http_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return;
  }

  static const httpd_uri_t root = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = root_handler,
  };
  static const httpd_uri_t ws = {
      .uri = "/ws",
      .method = HTTP_GET,
      .handler = ws_handler,
      .is_websocket = true,
  };
  httpd_register_uri_handler(s_hd, &root);
  httpd_register_uri_handler(s_hd, &ws);

  /* Sender task, lower priority than IMU and BLE tasks */
  xTaskCreate(log_sender_task, "ws_log_send", 4096, NULL, 3, NULL);

  /* Install vprintf hook last, after everything above is ready to receive */
  s_orig_vprintf = esp_log_set_vprintf(log_vprintf_hook);
}

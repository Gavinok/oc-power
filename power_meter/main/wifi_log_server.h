#pragma once

/* Start a WiFi SoftAP and WebSocket log server.
 * Must be called after nvs_flash_init().
 *
 * Once started:
 *   - Phone connects to the AP (SSID, open if password is "")
 *   - Browser opens http://192.168.4.1 for the HTML log viewer
 *   - Viewer auto-connects to ws://192.168.4.1/ws and streams log lines
 *   - All ESP_LOG* output continues on UART and is also forwarded to WS clients
 */
void wifi_log_server_start(const char* ssid, const char* password);

/* Register a callback invoked when the browser sends a command over WebSocket.
 * Called from the HTTP server task. Keep it short, no blocking. */
typedef void (*ws_command_cb_t)(const char* cmd);
void wifi_log_server_set_command_cb(ws_command_cb_t cb);

/* Set a short status string sent to every new client on connect so the browser
 * can restore UI state after a page refresh. Prefix with '!' to distinguish
 * from log lines (e.g. "!verbose:on"). Pass "" to clear. */
void wifi_log_server_set_status(const char* msg);

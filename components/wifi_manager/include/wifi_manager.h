#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum STA reconnection attempts before giving up */
#define WIFI_MANAGER_MAX_RETRY  10

/**
 * @brief WiFi connection state events delivered to the optional callback.
 */
typedef enum {
    WIFI_MANAGER_EVENT_CONNECTED,     /**< Got IP address — ip_str is valid  */
    WIFI_MANAGER_EVENT_DISCONNECTED,  /**< Lost connection — ip_str is NULL  */
    WIFI_MANAGER_EVENT_FAILED,        /**< Max retries exhausted — ip_str is NULL */
} wifi_manager_event_t;

/**
 * @brief Callback invoked on WiFi connection state changes.
 *
 * Called from the ESP-IDF system event task, so keep it short and
 * non-blocking (post to a queue if you need heavier work).
 *
 * @param event   The connection event type.
 * @param ip_str  Dotted-decimal IP string when event == WIFI_MANAGER_EVENT_CONNECTED,
 *                NULL otherwise.
 */
typedef void (*wifi_manager_event_cb_t)(wifi_manager_event_t event,
                                        const char *ip_str);

/**
 * @brief Initialise the TCP/IP stack, WiFi driver, and connect to an AP.
 *
 * Blocks until the connection succeeds or @c WIFI_MANAGER_MAX_RETRY is
 * exhausted.  Call once from @c app_main() after @c nvs_flash_init().
 *
 * @param ssid      AP SSID (null-terminated, max 32 chars).
 * @param password  AP password (null-terminated, max 64 chars).
 * @param cb        Optional event callback; pass NULL if not needed.
 *
 * @return  ESP_OK   – connected and IP obtained.
 *          ESP_FAIL – could not connect after max retries.
 */
esp_err_t wifi_manager_start(const char *ssid, const char *password,
                             wifi_manager_event_cb_t cb);

/**
 * @brief Return true if the station currently has a valid IP address.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Copy the current IP address string into @p buf.
 *
 * Writes an empty string if not connected.
 *
 * @param buf  Destination buffer.
 * @param len  Size of @p buf in bytes (at least 16 for a full IPv4 string).
 */
void wifi_manager_get_ip(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

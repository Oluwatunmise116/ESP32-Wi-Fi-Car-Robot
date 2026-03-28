#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "wifi_manager.h"

/* ─────────────────────────────────────────────
   Private state
   ───────────────────────────────────────────── */
static const char *TAG = "wifi_manager";

static EventGroupHandle_t   s_event_group;
static wifi_manager_event_cb_t s_user_cb    = NULL;
static int                  s_retry_count  = 0;
static bool                 s_connected    = false;
static char                 s_ip_str[16]   = {0};  /* "xxx.xxx.xxx.xxx\0" */

#define CONNECTED_BIT   BIT0
#define FAIL_BIT        BIT1

/* ─────────────────────────────────────────────
   Internal event handler
   ───────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        memset(s_ip_str, 0, sizeof(s_ip_str));

        if (s_retry_count < WIFI_MANAGER_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "Retrying connection (%d/%d)…",
                     s_retry_count, WIFI_MANAGER_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_event_group, FAIL_BIT);
            ESP_LOGE(TAG, "Max retries reached – giving up");
            if (s_user_cb) {
                s_user_cb(WIFI_MANAGER_EVENT_FAILED, NULL);
            }
        }

        /* Notify disconnect only after the initial connection was established */
        if (s_retry_count > 1 && s_user_cb) {
            s_user_cb(WIFI_MANAGER_EVENT_DISCONNECTED, NULL);
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));

        s_connected   = true;
        s_retry_count = 0;

        ESP_LOGI(TAG, "Connected – IP: %s", s_ip_str);
        xEventGroupSetBits(s_event_group, CONNECTED_BIT);

        if (s_user_cb) {
            s_user_cb(WIFI_MANAGER_EVENT_CONNECTED, s_ip_str);
        }
    }
}

/* ─────────────────────────────────────────────
   Public API
   ───────────────────────────────────────────── */
esp_err_t wifi_manager_start(const char *ssid, const char *password,
                             wifi_manager_event_cb_t cb)
{
    s_user_cb    = cb;
    s_retry_count = 0;
    s_connected  = false;

    s_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,    &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,   IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_cfg.sta.ssid,     ssid,     sizeof(wifi_cfg.sta.ssid)     - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    EventBits_t bits = xEventGroupWaitBits(
        s_event_group,
        CONNECTED_BIT | FAIL_BIT,
        pdFALSE, pdFALSE,
        portMAX_DELAY);

    if (bits & CONNECTED_BIT) {
        ESP_LOGI(TAG, "Station connected to \"%s\"", ssid);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to connect to \"%s\" after %d retries",
             ssid, WIFI_MANAGER_MAX_RETRY);
    return ESP_FAIL;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

void wifi_manager_get_ip(char *buf, size_t len)
{
    if (buf == NULL || len == 0) return;
    strncpy(buf, s_ip_str, len - 1);
    buf[len - 1] = '\0';
}

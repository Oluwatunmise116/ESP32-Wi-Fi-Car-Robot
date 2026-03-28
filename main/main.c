#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_http_server.h"

#include "wifi_manager.h"

/* ─────────────────────────────────────────────
   WiFi credentials  –  change these
   ───────────────────────────────────────────── */
#define WIFI_SSID   "Galaxy Note10+ 5G83b8"
#define WIFI_PASS   "12005john"

/* ─────────────────────────────────────────────
   Motor GPIO pins
   ───────────────────────────────────────────── */
#define MOTOR1_PIN1   GPIO_NUM_27
#define MOTOR1_PIN2   GPIO_NUM_26
#define ENABLE1_GPIO  GPIO_NUM_14   // PWM output

#define MOTOR2_PIN1   GPIO_NUM_33
#define MOTOR2_PIN2   GPIO_NUM_25
#define ENABLE2_GPIO  GPIO_NUM_32   // PWM output

/* ─────────────────────────────────────────────
   LEDC (PWM) configuration
   ───────────────────────────────────────────── */
#define LEDC_TIMER        LEDC_TIMER_0
#define LEDC_MODE         LEDC_LOW_SPEED_MODE
#define LEDC_CH_MOTOR1    LEDC_CHANNEL_0
#define LEDC_CH_MOTOR2    LEDC_CHANNEL_1
#define LEDC_FREQ_HZ      30000
#define LEDC_RESOLUTION   LEDC_TIMER_8_BIT   // 0–255

/* ─────────────────────────────────────────────
   Misc
   ───────────────────────────────────────────── */
static const char *TAG = "MotorCtrl";

/* Current PWM duty cycle (0–255) */
static int s_duty_cycle = 0;

/* ═══════════════════════════════════════════════
   HELPERS
   ═══════════════════════════════════════════════ */

/** Write the same duty cycle to both motor enable channels */
static void set_motor_duty(uint32_t duty)
{
    ledc_set_duty(LEDC_MODE, LEDC_CH_MOTOR1, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CH_MOTOR1);

    ledc_set_duty(LEDC_MODE, LEDC_CH_MOTOR2, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CH_MOTOR2);
}

/** Stop both motors (direction pins LOW) */
static void stop_motors(void)
{
    gpio_set_level(MOTOR1_PIN1, 0);
    gpio_set_level(MOTOR1_PIN2, 0);
    gpio_set_level(MOTOR2_PIN1, 0);
    gpio_set_level(MOTOR2_PIN2, 0);
}

/* ═══════════════════════════════════════════════
   HTTP HANDLERS
   ═══════════════════════════════════════════════ */

/* ── Root: serve the control page (joystick UI) ── */
static const char HTML_PAGE[] =
    "<!DOCTYPE HTML><html><head>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,user-scalable=no\">"
    "<link rel=\"icon\" href=\"data:,\">"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{background:#1a1a2e;display:flex;flex-direction:column;align-items:center;"
    "justify-content:center;min-height:100vh;font-family:Arial,sans-serif;color:#eee}"
    "h1{margin-bottom:16px;font-size:22px;color:#e94560}"
    "#status{font-size:20px;font-weight:bold;letter-spacing:2px;margin-bottom:28px;"
    "color:#eee;background:#16213e;padding:8px 28px;border-radius:20px;min-width:150px;text-align:center}"
    "#jb{width:200px;height:200px;background:#16213e;border-radius:50%;"
    "border:3px solid #0f3460;position:relative;touch-action:none;"
    "user-select:none;-webkit-user-select:none}"
    "#jt{width:80px;height:80px;"
    "background:radial-gradient(circle,#e94560,#c23152);"
    "border-radius:50%;position:absolute;top:50%;left:50%;"
    "margin-top:-40px;margin-left:-40px;"
    "box-shadow:0 4px 15px rgba(233,69,96,0.4);pointer-events:none}"
    "#spd{margin-top:18px;font-size:15px;color:#aaa}"
    "</style></head><body>"
    "<h1>ESP32 Robot Control</h1>"
    "<div id=\"status\">STOP</div>"
    "<div id=\"jb\"><div id=\"jt\"></div></div>"
    "<div id=\"spd\">Speed: 0%</div>"
    "<script>"
    "var b=document.getElementById('jb'),"
    "    t=document.getElementById('jt'),"
    "    s=document.getElementById('status'),"
    "    p=document.getElementById('spd');"
    "var BR=100,MD=60,DZ=0.18,act=false,ox=0,oy=0,lc='',ls=0;"
    "function upd(){var r=b.getBoundingClientRect();ox=r.left+BR;oy=r.top+BR;}"
    "function gp(e){return e.changedTouches?e.changedTouches[0]:e;}"
    "function run(e){"
    "  var q=gp(e),dx=q.clientX-ox,dy=q.clientY-oy,d=Math.sqrt(dx*dx+dy*dy);"
    "  if(d>MD){dx=dx*MD/d;dy=dy*MD/d;d=MD;}"
    "  t.style.transform='translate('+dx+'px,'+dy+'px)';"
    "  var n=d/MD,sp=(n<DZ)?0:Math.min(100,Math.round(n*4)*25);"
    "  var cmd='stop';"
    "  if(n>DZ){"
    "    var a=Math.atan2(dy,dx)*180/Math.PI;"
    "    cmd=(a>-135&&a<=-45)?'forward':(a>45&&a<=135)?'reverse':(a>-45&&a<=45)?'right':'left';"
    "  }"
    "  if(cmd!==lc){fetch('/'+cmd);lc=cmd;s.textContent=cmd.toUpperCase();}"
    "  if(sp!==ls){fetch('/speed?value='+sp);ls=sp;p.textContent='Speed: '+sp+'%';}"
    "}"
    "function go(e){e.preventDefault();act=true;upd();run(e);}"
    "function mv(e){if(!act)return;e.preventDefault();run(e);}"
    "function up(e){"
    "  if(!act)return;act=false;"
    "  t.style.transform='translate(0,0)';"
    "  fetch('/stop');lc='stop';s.textContent='STOP';"
    "  if(ls){fetch('/speed?value=0');ls=0;p.textContent='Speed: 0%';}"
    "}"
    "b.addEventListener('mousedown',go);"
    "b.addEventListener('touchstart',go,{passive:false});"
    "window.addEventListener('mousemove',mv);"
    "window.addEventListener('touchmove',mv,{passive:false});"
    "window.addEventListener('mouseup',up);"
    "window.addEventListener('touchend',up);"
    "</script></body></html>";

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ── /forward ── */
static esp_err_t forward_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Forward");
    gpio_set_level(MOTOR1_PIN1, 0);
    gpio_set_level(MOTOR1_PIN2, 1);
    gpio_set_level(MOTOR2_PIN1, 0);
    gpio_set_level(MOTOR2_PIN2, 1);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ── /reverse ── */
static esp_err_t reverse_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Reverse");
    gpio_set_level(MOTOR1_PIN1, 1);
    gpio_set_level(MOTOR1_PIN2, 0);
    gpio_set_level(MOTOR2_PIN1, 1);
    gpio_set_level(MOTOR2_PIN2, 0);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ── /left ── */
static esp_err_t left_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Left");
    gpio_set_level(MOTOR1_PIN1, 0);
    gpio_set_level(MOTOR1_PIN2, 1);
    gpio_set_level(MOTOR2_PIN1, 0);
    gpio_set_level(MOTOR2_PIN2, 0);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ── /right ── */
static esp_err_t right_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Right");
    gpio_set_level(MOTOR1_PIN1, 0);
    gpio_set_level(MOTOR1_PIN2, 0);
    gpio_set_level(MOTOR2_PIN1, 0);
    gpio_set_level(MOTOR2_PIN2, 1);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ── /stop ── */
static esp_err_t stop_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Stop");
    stop_motors();
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ── /speed?value=<0|25|50|75|100> ── */
static esp_err_t speed_handler(httpd_req_t *req)
{
    /* Extract the "value" query parameter */
    char query[32] = {0};
    char value_str[8] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "value", value_str, sizeof(value_str)) == ESP_OK) {
            int value = atoi(value_str);
            ESP_LOGI(TAG, "Speed value received: %d", value);

            if (value == 0) {
                set_motor_duty(0);
                stop_motors();
            } else {
                /* Map 25–100 → 200–255  (same as Arduino map()) */
                s_duty_cycle = (int)((value - 25) * (255 - 200) / (100 - 25)) + 200;
                set_motor_duty((uint32_t)s_duty_cycle);
                ESP_LOGI(TAG, "Motor speed set to %d (duty=%d)", value, s_duty_cycle);
            }
        }
    }
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════
   HTTP SERVER INIT
   ═══════════════════════════════════════════════ */
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    /* Register URI handlers */
    const httpd_uri_t uris[] = {
        { .uri = "/",        .method = HTTP_GET, .handler = root_handler    },
        { .uri = "/forward", .method = HTTP_GET, .handler = forward_handler },
        { .uri = "/reverse", .method = HTTP_GET, .handler = reverse_handler },
        { .uri = "/left",    .method = HTTP_GET, .handler = left_handler    },
        { .uri = "/right",   .method = HTTP_GET, .handler = right_handler   },
        { .uri = "/stop",    .method = HTTP_GET, .handler = stop_handler    },
        { .uri = "/speed",   .method = HTTP_GET, .handler = speed_handler   },
    };

    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    ESP_LOGI(TAG, "HTTP server started");
    return server;
}

/* ═══════════════════════════════════════════════
   GPIO INIT
   ═══════════════════════════════════════════════ */
static void gpio_init(void)
{
    /* Configure motor direction pins as outputs */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR1_PIN1) |
                        (1ULL << MOTOR1_PIN2) |
                        (1ULL << MOTOR2_PIN1) |
                        (1ULL << MOTOR2_PIN2),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* Start with motors stopped */
    stop_motors();
}

/* ═══════════════════════════════════════════════
   LEDC (PWM) INIT
   ═══════════════════════════════════════════════ */
static void ledc_init(void)
{
    /* Configure the LEDC timer */
    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    /* Motor 1 enable channel */
    ledc_channel_config_t ch1 = {
        .gpio_num   = ENABLE1_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CH_MOTOR1,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch1));

    /* Motor 2 enable channel */
    ledc_channel_config_t ch2 = {
        .gpio_num   = ENABLE2_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CH_MOTOR2,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch2));
}


void app_main(void)
{
    /* NVS is required by the WiFi driver */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initialising GPIO...");
    gpio_init();

    ESP_LOGI(TAG, "Initialising LEDC PWM...");
    ledc_init();

    ESP_LOGI(TAG, "Connecting to WiFi...");
    wifi_manager_start(WIFI_SSID, WIFI_PASS, NULL);

    ESP_LOGI(TAG, "Starting HTTP server...");
    start_webserver();

    /* app_main can return; the FreeRTOS scheduler keeps tasks alive */
}
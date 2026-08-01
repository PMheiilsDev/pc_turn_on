#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "esp_http_server.h"

#define WIFI_SSID "TP-Link_FF7C"
#define WIFI_PASS "123456789"

#define POWER_PIN GPIO_NUM_1
#define LED_PIN   GPIO_NUM_8

static const char *TAG = "PC_POWER";

// HTML page
static const char *html_page =
"<!DOCTYPE html>"
"<html>"
"<head>"
"<title>ESP32 PC Power</title>"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<style>"
"body{font-family:Arial;background:#202124;color:white;text-align:center;margin-top:80px;}"
".btn{background:#28a745;color:white;font-size:28px;padding:20px 40px;"
"border:none;border-radius:10px;cursor:pointer;}"
".btn:hover{background:#218838;}"
"</style>"
"</head>"
"<body>"
"<h1>ESP32-C3 PC Power Switch</h1>"
"<a href=\"/power\"><button class=\"btn\">Turn PC On</button></a>"
"</body>"
"</html>";

// Root page
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// PC Power Button Handler
static esp_err_t power_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Power button pressed");

    // LED ON (active LOW)
    gpio_set_level(LED_PIN, 0);

    // Press PC power button
    gpio_set_level(POWER_PIN, 1);

    // Redirect browser immediately
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);

    // Hold button for 1 second
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Release button
    gpio_set_level(POWER_PIN, 0);

    // LED OFF
    gpio_set_level(LED_PIN, 1);

    ESP_LOGI(TAG, "Power button released");

    return ESP_OK;
}

// URI definitions
static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
    .user_ctx = NULL
};

static const httpd_uri_t power = {
    .uri = "/power",
    .method = HTTP_GET,
    .handler = power_handler,
    .user_ctx = NULL
};

// Start web server
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;

    ESP_LOGI(TAG, "Starting web server...");

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &power);
        return server;
    }

    ESP_LOGE(TAG, "Failed to start web server");
    return NULL;
}

// Wi-Fi events
static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Reconnecting...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Got IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        start_webserver();
    }
}

void app_main(void)
{
    // NVS
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // GPIO setup

    // Onboard LED (active LOW)
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 1);

    // PC Power output
    gpio_reset_pin(POWER_PIN);
    gpio_set_direction(POWER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(POWER_PIN, 0);

    // Network
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL,
            NULL));

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL,
            NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to Wi-Fi...");
}
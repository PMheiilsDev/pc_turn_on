#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_http_server.h"

#define WIFI_SSID      "TP-Link_FF7C"
#define WIFI_PASS      "123456789"

static const char *TAG = "webserver";

// Updated HTML page with "Turn PC On" button text
const char* html_page = 
    "<!DOCTYPE html><html>"
    "<head><title>ESP32-C3 Web Server</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style>body{text-align:center; font-family:Arial;} .btn{padding:20px; font-size:24px; color:white; background-color:#008CBA; border:none; cursor:pointer;}</style></head>"
    "<body><h1>ESP32-C3 SuperMini Control</h1>"
    "<p><a href=\"/toggle\"><button class=\"btn\">Turn PC On</button></a></p>"
    "</body></html>";

// Handler for the root path ("/")
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Handler to trigger the 1-second pulse
static esp_err_t toggle_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Triggering PC Power Button...");

    // 1. Turn Pins ON
    gpio_set_level(GPIO_NUM_8, 0); // Active Low (Onboard LED On)
    gpio_set_level(GPIO_NUM_1, 1); // Active High (GPIO1 On)

    // 2. Respond to the browser immediately so it doesn't timeout/lag
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);

    // 3. Keep the pins active for 1 second (1000ms)
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 4. Turn Pins OFF
    gpio_set_level(GPIO_NUM_8, 1); // Active Low (Onboard LED Off)
    gpio_set_level(GPIO_NUM_1, 0); // Active High (GPIO1 Off)

    ESP_LOGI(TAG, "PC Power Button Released");

    return ESP_OK;
}

// URI structure for the root page
static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

// URI structure for the pulse action
static const httpd_uri_t toggle = {
    .uri       = "/toggle",
    .method    = HTTP_GET,
    .handler   = toggle_get_handler,
    .user_ctx  = NULL
};

// Start the HTTP web server on port 80
static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &toggle);
        return server;
    }
    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

// Wi-Fi event handler to catch the IP assignment
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retry connecting to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        start_webserver();
    }
}

void app_main(void) {
    // Initialize NVS (required for Wi-Fi storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // GPIO8 (LED - inverted)
    gpio_reset_pin(GPIO_NUM_8);
    gpio_set_direction(GPIO_NUM_8, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_8, 1);   // Default OFF

    // GPIO1 (normal)
    gpio_reset_pin(GPIO_NUM_1);
    gpio_set_direction(GPIO_NUM_1, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_1, 0);   // Default OFF

    // Initialize TCP/IP and Event Loops
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register Wi-Fi and IP events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Configure Wi-Fi Credentials
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi initialization finished.");
}
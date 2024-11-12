#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WiFi_Config";
static httpd_handle_t server = NULL;

// Funzione per la connessione Wi-Fi
void connect_to_wifi(const char *ssid, const char *password) {
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

// Gestione della richiesta POST
esp_err_t wifi_config_post_handler(httpd_req_t *req) {
    char buf[100];
    int ret, ssid_len = 0, pwd_len = 0;

    // Legge il corpo della richiesta
    if ((ret = httpd_req_recv(req, buf, sizeof(buf))) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Estrae SSID e password dal JSON ricevuto
    char ssid[32] = {0};
    char password[64] = {0};
    sscanf(buf, "{\"ssid\":\"%31[^\"]\",\"password\":\"%63[^\"]\"}", ssid, password);
    ESP_LOGI(TAG, "SSID ricevuto: %s", ssid);
    ESP_LOGI(TAG, "Password ricevuta: %s", password);

    // Salva le credenziali in NVS
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "ssid", ssid));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "password", password));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);

    // Tenta di connettersi alla rete
    connect_to_wifi(ssid, password);

    // Risponde alla richiesta
    httpd_resp_send(req, "Configurazione Wi-Fi ricevuta", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Avvia il server HTTP
httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t wifi_config_uri = {
            .uri = "/wifi_config",
            .method = HTTP_POST,
            .handler = wifi_config_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_config_uri);
    }
    return server;
}

// Funzione di setup della modalità AP
void start_access_point() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ESP32_Config",
            .ssid_len = strlen("ESP32_Config"),
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "AP avviato con SSID: ESP32_Config");
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    // Controlla se esistono credenziali salvate in NVS
    nvs_handle_t nvs_handle;
    char ssid[32] = {0};
    char password[64] = {0};
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_handle));
    size_t ssid_len = sizeof(ssid);
    size_t pwd_len = sizeof(password);
    esp_err_t ssid_err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
    esp_err_t pwd_err = nvs_get_str(nvs_handle, "password", password, &pwd_len);
    nvs_close(nvs_handle);

    if (ssid_err == ESP_OK && pwd_err == ESP_OK) {
        connect_to_wifi(ssid, password);
        ESP_LOGI(TAG, "Connesso alla rete salvata.");
    } else {
        start_access_point();
        server = start_webserver();
    }
}

#include <string.h>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "WiFi_Manager";
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch(event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Disconnesso dal Wi-Fi, riconnessione in corso...");
                esp_wifi_connect();
                xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Indirizzo IP ottenuto: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_manager_init(void) {
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Inizializza interfacce Wi-Fi di default
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Registra gestori di eventi
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password) {
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

void wifi_manager_start_ap(void) {
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, "ESP32_Config", sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen("ESP32_Config");
    strncpy((char *)wifi_config.ap.password, "12345678", sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    if (strlen((char *)wifi_config.ap.password) < 8) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP avviato. SSID:%s", wifi_config.ap.ssid);
}

void wifi_manager_erase_credentials(void) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("wifi_cred", NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvs_erase_all(nvs_handle));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Credenziali Wi-Fi cancellate.");
}

int wifi_manager_credentials_exist(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    char ssid[32];
    size_t ssid_len = sizeof(ssid);

    err = nvs_open("wifi_cred", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return 0; // Credenziali non esistono
    }

    err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
    nvs_close(nvs_handle);

    if (err == ESP_OK && ssid_len > 0)
        return 1; // Credenziali esistono
    else
        return 0; // Credenziali non esistono
}

void wifi_manager_wait_for_connection(void) {
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
}

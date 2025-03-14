#include "wifi_manager.h"
#include "web_server.h"
#include "http_client.h"
#include "gpio_handler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "nvs.h"

static const char *TAG = "Main";

extern "C" void app_main(void) {
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    // Inizializza NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Configura il pulsante di reset tramite gpio_handler
    gpio_handler_configure_reset_button();
    if (gpio_handler_is_reset_button_pressed()) {
        ESP_LOGI(TAG, "Pulsante di reset premuto. Cancellazione delle credenziali Wi-Fi.");
        wifi_manager_erase_credentials();
        esp_restart();
    }

    // Inizializza Wi-Fi Manager
    wifi_manager_init();

    if (wifi_manager_credentials_exist()) {
        // Leggi le credenziali da NVS
        nvs_handle_t nvs_handle;
        char ssid[32];
        char password[64];
        size_t ssid_len = sizeof(ssid);
        size_t password_len = sizeof(password);

        esp_err_t err = nvs_open("wifi_cred", NVS_READONLY, &nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Errore nell'apertura dell'NVS: %s", esp_err_to_name(err));
            wifi_manager_start_ap();
            web_server_start();
            return;
        }

        err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Errore nel recupero dell'SSID: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            wifi_manager_start_ap();
            web_server_start();
            return;
        }

        err = nvs_get_str(nvs_handle, "password", password, &password_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Errore nel recupero della password: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            wifi_manager_start_ap();
            web_server_start();
            return;
        }

        nvs_close(nvs_handle);

        ESP_LOGI(TAG, "Connessione alla rete Wi-Fi: %s", ssid);
        err = wifi_manager_connect(ssid, password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Errore nella connessione Wi-Fi: %s", esp_err_to_name(err));
            wifi_manager_start_ap();
            web_server_start();
            return;
        }

        // Attendi la connessione Wi-Fi
        wifi_manager_wait_for_connection();

        // Avvia il web server sulla nuova interfaccia STA (IP assegnato dal router)
        web_server_start();

        // Avvia il task HTTP Client
        xTaskCreate(&http_client_task, "http_client_task", 8192, NULL, 5, NULL);

    } else {
        // Avvia Wi-Fi AP e Web Server per la configurazione
        wifi_manager_start_ap();
        web_server_start();
    }
}




// 6tmzbs5meuwn273d
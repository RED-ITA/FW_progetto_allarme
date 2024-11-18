#include <inttypes.h>

#include "http_client.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "HTTP_Client";

void http_client_init(void) {
    // Codice di inizializzazione se necessario
}

void http_client_task(void *param) {
    const char *server_url = "http://192.168.1.132:5001/"; // Sostituisci con l'indirizzo corretto

    while (1) {
        // Esegui HTTP GET
        esp_http_client_config_t config = {
            .url = server_url,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64,
                     esp_http_client_get_status_code(client),
                     esp_http_client_get_content_length(client));
        } else {
            ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);

        // Delay before next request
        vTaskDelay(pdMS_TO_TICKS(10000)); // Delay 10 seconds
    }
}
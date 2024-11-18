#include "web_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "wifi_manager.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"

static const char *TAG = "Web_Server";
static httpd_handle_t server = NULL;

// Prototipo della funzione url_decode
void url_decode(char *str);

// Handler per la richiesta GET (pagina HTML)
static esp_err_t wifi_config_get_handler(httpd_req_t *req) {
    const char* resp_str = "<!DOCTYPE html>"
    "<html>"
    "<body>"
    "<form action=\"/wifi-config\" method=\"post\">"
    "SSID:<br>"
    "<input type=\"text\" name=\"ssid\"><br>"
    "Password:<br>"
    "<input type=\"password\" name=\"password\"><br><br>"
    "<input type=\"submit\" value=\"Submit\">"
    "</form>"
    "</body>"
    "</html>";

    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// Handler per la richiesta POST (salva credenziali)
esp_err_t wifi_config_post_handler(httpd_req_t *req)
{
    char buf[200];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        ESP_LOGE(TAG, "Contenuto troppo lungo");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Contenuto troppo lungo");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Errore nel ricevere i dati");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Errore nel ricevere i dati");
        return ESP_FAIL;
    }

    buf[ret] = '\0'; // Termina la stringa

    // Parsing dei dati del form
    char ssid[32] = {0};
    char password[64] = {0};

    // Utilizza httpd_query_key_value per il parsing
    if (httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid)) != ESP_OK ||
        httpd_query_key_value(buf, "password", password, sizeof(password)) != ESP_OK) {
        ESP_LOGE(TAG, "Errore nel parsing dei dati del form");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Dati del form non validi");
        return ESP_FAIL;
    }

    // Decodifica URL
    url_decode(ssid);
    url_decode(password);

    // Salva SSID e password nell'NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_cred", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore nell'apertura dell'NVS: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Errore nell'apertura dell'NVS");
        return ESP_FAIL;
    }

    // Scrivi SSID e Password nell'NVS
    err = nvs_set_str(nvs_handle, "ssid", ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore nel settare l'SSID nell'NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Errore nel salvare l'SSID");
        return ESP_FAIL;
    }

    err = nvs_set_str(nvs_handle, "password", password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore nel settare la password nell'NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Errore nel salvare la password");
        return ESP_FAIL;
    }

    // Esegui il commit delle modifiche
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore nel commit dell'NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Errore nel salvare le credenziali");
        return ESP_FAIL;
    }

    // Chiudi l'handle NVS
    nvs_close(nvs_handle);

    // Invia una risposta al client
    httpd_resp_send(req, "Credenziali ricevute e salvate. Riavvio in corso...", HTTPD_RESP_USE_STRLEN);

    // Riavvia per applicare le nuove impostazioni Wi-Fi
    esp_restart();

    return ESP_OK;
}

// Funzione per decodificare URL
void url_decode(char *str) {
    char *pstr = str, *buf = str;
    char hex[3];
    while (*pstr) {
        if (*pstr == '%') {
            if (pstr[1] && pstr[2]) {
                hex[0] = pstr[1];
                hex[1] = pstr[2];
                hex[2] = '\0';
                *buf++ = (char) strtol(hex, NULL, 16);
                pstr += 2;
            }
        } else if (*pstr == '+') {
            *buf++ = ' ';
        } else {
            *buf++ = *pstr;
        }
        pstr++;
    }
    *buf = '\0';
}

// Handler per cancellare le credenziali Wi-Fi
static esp_err_t wifi_config_delete_handler(httpd_req_t *req) {
    wifi_manager_erase_credentials();
    httpd_resp_send(req, "Credenziali Wi-Fi cancellate. Riavvio in corso...", HTTPD_RESP_USE_STRLEN);

    // Riavvia
    esp_restart();

    return ESP_OK;
}

void web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK) {
        // Handler GET per la pagina di configurazione
        httpd_uri_t wifi_config_get_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = wifi_config_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_config_get_uri);

        // Handler POST per ricevere le credenziali Wi-Fi
        httpd_uri_t wifi_config_post_uri = {
            .uri = "/wifi-config",
            .method = HTTP_POST,
            .handler = wifi_config_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_config_post_uri);

        // Handler POST per cancellare le credenziali
        httpd_uri_t wifi_config_delete_uri = {
            .uri = "/wifi-config/delete",
            .method = HTTP_POST,
            .handler = wifi_config_delete_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_config_delete_uri);

        ESP_LOGI(TAG, "Web server avviato.");
    } else {
        ESP_LOGE(TAG, "Impossibile avviare il web server.");
    }
}

void web_server_stop(void) {
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG, "Web server fermato.");
    }
}

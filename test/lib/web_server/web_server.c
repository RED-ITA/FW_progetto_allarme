#include "web_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"

// Definisci la versione del software
#define SOFTWARE_VERSION "1.0.0"

static const char *TAG = "Web_Server";
static httpd_handle_t server = NULL;

// Prototipo della funzione url_decode
void url_decode(char *str);

// Handler GET per la route /wifi-config: mostra lo stato del sensore
static esp_err_t wifi_config_get_handler(httpd_req_t *req) {
    char resp_str[1024];
    char saved_ssid[32] = "N/D";
    char active_ssid[32] = "N/D";
    char ip_str[16] = "N/A";

    // Recupera le credenziali salvate dall'NVS
    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_cred", NVS_READONLY, &nvs_handle) == ESP_OK) {
        size_t ssid_len = sizeof(saved_ssid);
        if (nvs_get_str(nvs_handle, "ssid", saved_ssid, &ssid_len) != ESP_OK) {
            strcpy(saved_ssid, "N/D");
        }
        nvs_close(nvs_handle);
    }

    // Se il dispositivo è in modalità STA, recupera la configurazione attiva
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_STA) {
        wifi_config_t wifi_config = {0};
        if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK) {
            strncpy(active_ssid, (char *)wifi_config.sta.ssid, sizeof(active_ssid) - 1);
        }
        // Recupera l'indirizzo IP corrente
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            }
        }
    }

    // Costruisci la pagina HTML dinamica
    snprintf(resp_str, sizeof(resp_str),
        "<!DOCTYPE html>"
        "<html>"
        "<head><meta charset='UTF-8'><title>Status Sensore</title></head>"
        "<body>"
        "<h1>Status Sensore</h1>"
        "<h2>Configurazioni WiFi</h2>"
        "<p><b>Credenziali salvate:</b> SSID: %s</p>"
        "<p><b>Configurazione attiva:</b> SSID: %s, IP: %s</p>"
        "<h2>Versione Software</h2>"
        "<p>Versione: %s</p>"
        "<hr>"
        "<h2>Cambia Configurazione WiFi</h2>"
        "<form action=\"/wifi-config\" method=\"post\">"
        "SSID:<br><input type=\"text\" name=\"ssid\"><br>"
        "Password:<br><input type=\"password\" name=\"password\"><br><br>"
        "<input type=\"submit\" value=\"Submit\">"
        "</form>"
        "</body>"
        "</html>",
        saved_ssid, active_ssid, ip_str, SOFTWARE_VERSION);

    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Handler POST per salvare le credenziali WiFi
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

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore nel commit dell'NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Errore nel salvare le credenziali");
        return ESP_FAIL;
    }

    nvs_close(nvs_handle);

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
    esp_restart();
    return ESP_OK;
}

void web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Se necessario, imposta qui eventuali altri parametri (gli header di richiesta sono definiti tramite macro)
    config.max_resp_headers = 1024;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Registrazione dell'handler GET per la route /wifi-config
        httpd_uri_t wifi_config_get_uri = {
            .uri = "/wifi-config",
            .method = HTTP_GET,
            .handler = wifi_config_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_config_get_uri);

        // Registrazione dell'handler POST per salvare le credenziali
        httpd_uri_t wifi_config_post_uri = {
            .uri = "/wifi-config",
            .method = HTTP_POST,
            .handler = wifi_config_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_config_post_uri);

        // Registrazione dell'handler per cancellare le credenziali
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


/*
#include "web_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"

// Definisci la versione del software
#define SOFTWARE_VERSION "1.0.0"

static const char *TAG = "Web_Server";
static httpd_handle_t server = NULL;

// Prototipo della funzione per decodificare le stringhe URL-encoded
void url_decode(char *str);

// Handler per la pagina default "/" (rimane invariata)
static esp_err_t default_page_handler(httpd_req_t *req) {
    const char* resp_str = "<!DOCTYPE html>"
        "<html>"
        "<head><meta charset='UTF-8'><title>Home</title></head>"
        "<body>"
        "<h1>Pagina Home</h1>"
        "<p>Questa è la pagina di default.</p>"
        "<a href=\"/wifi-config\">Vai alla pagina di configurazione e stato del sensore</a>"
        "</body>"
        "</html>";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Handler GET per /wifi-config: mostra lo stato del sensore e le configurazioni WiFi
static esp_err_t wifi_config_get_handler(httpd_req_t *req) {
    char resp_str[2048];
    char saved_ssid[32] = "N/D";
    char active_ssid[32] = "N/D";
    char ip_str[16] = "N/A";

    // Recupera le credenziali salvate dall'NVS
    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_cred", NVS_READONLY, &nvs_handle) == ESP_OK) {
        size_t ssid_len = sizeof(saved_ssid);
        if (nvs_get_str(nvs_handle, "ssid", saved_ssid, &ssid_len) != ESP_OK) {
            strcpy(saved_ssid, "N/D");
        }
        nvs_close(nvs_handle);
    }

    // Se il dispositivo è in modalità STA, recupera la configurazione attiva
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_STA) {
        wifi_config_t wifi_config = {0};
        if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK) {
            strncpy(active_ssid, (char *)wifi_config.sta.ssid, sizeof(active_ssid) - 1);
        }
        // Recupera l'indirizzo IP corrente
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            }
        }
    }

    // Costruisci la pagina HTML dinamica
    snprintf(resp_str, sizeof(resp_str),
        "<!DOCTYPE html>"
        "<html>"
        "<head><meta charset='UTF-8'><title>Status e Configurazione</title></head>"
        "<body>"
        "<h1>Status Sensore e Configurazione WiFi</h1>"
        "<h2>Informazioni Attuali</h2>"
        "<p><b>Credenziali salvate:</b> SSID: %s</p>"
        "<p><b>Configurazione attiva:</b> SSID: %s, IP: %s</p>"
        "<h2>Versione Software</h2>"
        "<p>Versione: %s</p>"
        "<hr>"
        "<h2>Seleziona la rete da attivare</h2>"
        "<form action=\"/wifi-config\" method=\"post\">"
        "SSID da attivare:<br><input type=\"text\" name=\"ssid\" value=\"%s\"><br>"
        "Password:<br><input type=\"password\" name=\"password\"><br><br>"
        "<input type=\"submit\" value=\"Attiva rete\">"
        "</form>"
        "</body>"
        "</html>",
        saved_ssid, active_ssid, ip_str, SOFTWARE_VERSION, saved_ssid);

    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Handler POST per /wifi-config: salva le nuove credenziali e attiva la rete scelta
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

    // Parsing dei dati inviati dal form
    char ssid[32] = {0};
    char password[64] = {0};

    if (httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid)) != ESP_OK ||
        httpd_query_key_value(buf, "password", password, sizeof(password)) != ESP_OK) {
        ESP_LOGE(TAG, "Errore nel parsing dei dati del form");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Dati del form non validi");
        return ESP_FAIL;
    }

    // Decodifica URL
    url_decode(ssid);
    url_decode(password);

    // Salva le nuove credenziali in NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_cred", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore nell'apertura dell'NVS: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Errore nell'apertura dell'NVS");
        return ESP_FAIL;
    }

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

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Errore nel commit dell'NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Errore nel salvare le credenziali");
        return ESP_FAIL;
    }
    nvs_close(nvs_handle);

    httpd_resp_send(req, "Credenziali salvate e rete attivata. Riavvio in corso...", HTTPD_RESP_USE_STRLEN);

    // Riavvia per applicare le nuove impostazioni Wi-Fi
    esp_restart();

    return ESP_OK;
}

// Funzione per decodificare le stringhe URL-encoded
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
    esp_restart();
    return ESP_OK;
}

void web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Configura eventuali parametri aggiuntivi se necessario (gli header di richiesta sono definiti a livello di compilazione)
    config.max_resp_headers = 1024;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Registrazione della pagina default per "/"
        httpd_uri_t default_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = default_page_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &default_uri);

        // Registrazione degli handler per /wifi-config
        httpd_uri_t wifi_config_get_uri = {
            .uri = "/wifi-config",
            .method = HTTP_GET,
            .handler = wifi_config_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_config_get_uri);

        httpd_uri_t wifi_config_post_uri = {
            .uri = "/wifi-config",
            .method = HTTP_POST,
            .handler = wifi_config_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_config_post_uri);

        // Registrazione dell'handler per cancellare le credenziali
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

*/
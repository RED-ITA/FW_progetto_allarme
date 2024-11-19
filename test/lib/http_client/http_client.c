#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "http_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "gpio_handler.h"
#include "esp_websocket_client.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "HTTP_Client";

static int sensor_pk = -1; // Variabile per memorizzare la SensorPk ottenuta dall'API
static int soglia = 10;    // Valore di soglia, da aggiornare se necessario
volatile bool parameter_update_needed = false; // Flag per indicare se è necessario aggiornare i parametri

static esp_err_t load_sensor_pk_from_nvs(int *sensor_pk) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_i32(nvs_handle, "sensor_pk", sensor_pk);
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t save_sensor_pk_to_nvs(int sensor_pk) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i32(nvs_handle, "sensor_pk", sensor_pk);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    return err;
}

// Funzione per registrare il sensore e ottenere la SensorPk
static esp_err_t register_sensor(int *sensor_pk) {
    const char *server_url = "http://192.168.1.132:5001/sensor";

    // Crea il JSON da inviare
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "tipo", 0); // Sostituisci con il tipo corretto

    char *post_data = cJSON_PrintUnformatted(root);

    esp_http_client_config_t config = {
        .url = server_url,
        .method = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Imposta l'header e il corpo della richiesta
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 || status == 201) {
            // Ottieni la risposta dal server
            char buffer[256];
            int content_length = esp_http_client_get_content_length(client);
            int total_read_len = 0;
            int read_len;
            char *ptr = buffer;

            while (total_read_len < content_length && (read_len = esp_http_client_read(client, ptr, sizeof(buffer) - total_read_len - 1)) > 0) {
                ptr += read_len;
                total_read_len += read_len;
            }
            buffer[total_read_len] = '\0';

            if (total_read_len > 0) {
                // Parsea la risposta JSON
                cJSON *response = cJSON_Parse(buffer);
                if (response) {
                    cJSON *sensor_id_item = cJSON_GetObjectItem(response, "sensor_id");
                    if (sensor_id_item) {
                        *sensor_pk = sensor_id_item->valueint;
                        ESP_LOGI(TAG, "Registrazione sensore riuscita, SensorPk: %d", *sensor_pk);
                    } else {
                        ESP_LOGE(TAG, "Sensor ID non trovato nella risposta");
                        err = ESP_FAIL;
                    }
                    cJSON_Delete(response);
                } else {
                    ESP_LOGE(TAG, "Errore nel parsing della risposta JSON");
                    err = ESP_FAIL;
                }
            } else {
                ESP_LOGE(TAG, "Nessuna risposta dal server");
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "Registrazione sensore fallita, status code: %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_Delete(root);
    free(post_data);

    return err;
}

// Funzione per inviare il valore del sensore all'API
static esp_err_t send_sensor_value(int sensor_pk, int value, int allarme) {
    const char *url = "http://192.168.1.132:5001/sensor/value";

    // Crea il JSON da inviare
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "sensor_pk", sensor_pk);
    cJSON_AddNumberToObject(root, "value", value);
    cJSON_AddNumberToObject(root, "allarme", allarme);

    char *post_data = cJSON_PrintUnformatted(root);

    esp_http_client_config_t config = {
        .url = url,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Imposta il metodo POST e il Content-Type
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 || status == 201) {
            ESP_LOGI(TAG, "Valore inviato con successo");
        } else {
            ESP_LOGE(TAG, "Invio del valore fallito, status code: %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_Delete(root);
    free(post_data);

    return err;
}

// Funzione per ottenere i parametri del sensore
static esp_err_t get_sensor_parameters(int sensor_pk) {
    char url[100];
    snprintf(url, sizeof(url), "http://192.168.1.132:5001/sensor/%d", sensor_pk);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 || status == 201) {
            // Ottieni la risposta dal server
            char buffer[512];
            int content_length = esp_http_client_get_content_length(client);
            int total_read_len = 0;
            int read_len;
            char *ptr = buffer;

            while (total_read_len < content_length && (read_len = esp_http_client_read(client, ptr, sizeof(buffer) - total_read_len - 1)) > 0) {
                ptr += read_len;
                total_read_len += read_len;
            }
            buffer[total_read_len] = '\0';

            if (total_read_len > 0) {
                // Parsea la risposta JSON
                cJSON *response = cJSON_Parse(buffer);
                if (response) {
                    cJSON *sensor_array = cJSON_GetObjectItem(response, "sensor");
                    if (sensor_array && cJSON_IsArray(sensor_array)) {
                        cJSON *sensor_item = cJSON_GetArrayItem(sensor_array, 0);
                        if (sensor_item) {
                            cJSON *soglia_item = cJSON_GetObjectItem(sensor_item, "Soglia");
                            if (soglia_item) {
                                soglia = soglia_item->valueint;
                                ESP_LOGI(TAG, "Aggiornata soglia: %d", soglia);
                            } else {
                                ESP_LOGE(TAG, "Soglia non trovata nella risposta");
                                err = ESP_FAIL;
                            }
                        } else {
                            ESP_LOGE(TAG, "Elemento sensore non trovato nell'array");
                            err = ESP_FAIL;
                        }
                    } else {
                        ESP_LOGE(TAG, "Array sensore non trovato nella risposta");
                        err = ESP_FAIL;
                    }
                    cJSON_Delete(response);
                } else {
                    ESP_LOGE(TAG, "Errore nel parsing della risposta JSON");
                    err = ESP_FAIL;
                }
            } else {
                ESP_LOGE(TAG, "Nessuna risposta dal server");
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "Recupero dei parametri fallito, status code: %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    return err;
}

void http_client_task(void *param) {
    // Carica sensor_pk da NVS
    esp_err_t err = load_sensor_pk_from_nvs(&sensor_pk);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SensorPk caricato da NVS: %d", sensor_pk);
    } else {
        // Registrazione del sensore
        if (register_sensor(&sensor_pk) != ESP_OK) {
            ESP_LOGE(TAG, "Registrazione del sensore fallita. Riavvio in 10 secondi...");
            vTaskDelay(pdMS_TO_TICKS(10000));
            esp_restart();
        }
        // Salva sensor_pk in NVS
        if (save_sensor_pk_to_nvs(sensor_pk) != ESP_OK) {
            ESP_LOGE(TAG, "Errore nel salvataggio di SensorPk in NVS");
        }
    }

    // Ottieni i parametri iniziali del sensore
    get_sensor_parameters(sensor_pk);

    // Inizializza GPIO
    gpio_handler_init();

    while (1) {
        // Controlla se è necessario aggiornare i parametri
        if (parameter_update_needed) {
            ESP_LOGI(TAG, "Ricevuta notifica di aggiornamento, ottenimento nuovi parametri...");
            get_sensor_parameters(sensor_pk);
            parameter_update_needed = false;
        }

        // Leggi il valore ADC
        int adc_value = gpio_handler_read_adc_channel0(); // Usa il canale ADC corretto

        ESP_LOGI(TAG, "Valore ADC: %d", adc_value);

        // Determina se scatta l'allarme
        int allarme = adc_value > soglia ? 1 : 0;

        // Invia il valore al server
        if (send_sensor_value(sensor_pk, adc_value, allarme) != ESP_OK) {
            ESP_LOGE(TAG, "Errore nell'invio del valore. Riprovo...");
        }

        // Delay prima della prossima lettura
        vTaskDelay(pdMS_TO_TICKS(1000)); // Attendi 1 secondo
    }
}

void websocket_client_task(void *param) {
    esp_websocket_client_config_t websocket_cfg = {
        .uri = "ws://192.168.1.132:5001/your_websocket_endpoint", // Sostituisci con l'endpoint corretto
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);

    if (esp_websocket_client_start(client) != ESP_OK) {
        ESP_LOGE(TAG, "Impossibile avviare il client WebSocket");
        vTaskDelete(NULL);
    }

    while (1) {
        char buffer[128];
        int len = esp_websocket_client_receive(client, buffer, sizeof(buffer) - 1, portMAX_DELAY);
        if (len > 0) {
            buffer[len] = '\0'; // Termina la stringa
            ESP_LOGI(TAG, "Messaggio WebSocket ricevuto: %s", buffer);
            // Imposta il flag per aggiornare i parametri
            parameter_update_needed = true;
        }
    }

    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
    vTaskDelete(NULL);
}

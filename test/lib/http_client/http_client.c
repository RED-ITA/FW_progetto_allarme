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
#include "esp_event.h"

static const char *TAG = "HTTP_Client";

static int32_t sensor_pk = -1; // Variabile per memorizzare la SensorPk ottenuta dall'API
static int soglia = 10;        // Valore di soglia, da aggiornare se necessario
volatile bool parameter_update_needed = false; // Flag per indicare se è necessario aggiornare i parametri

// Struttura per memorizzare la risposta HTTP
typedef struct {
    char *buffer;
    int buffer_size;
    int data_received;
} http_response_t;

// Gestore di eventi per il client HTTP
esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    http_response_t *response = (http_response_t *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (response->data_received + evt->data_len < response->buffer_size) {
                    memcpy(response->buffer + response->data_received, evt->data, evt->data_len);
                    response->data_received += evt->data_len;
                } else {
                    ESP_LOGE(TAG, "Buffer di risposta pieno");
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Funzione per caricare sensor_pk da NVS
static esp_err_t load_sensor_pk_from_nvs(int32_t *sensor_pk) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_i32(nvs_handle, "sensor_pk", sensor_pk);
    nvs_close(nvs_handle);
    return err;
}

// Funzione per salvare sensor_pk in NVS
static esp_err_t save_sensor_pk_to_nvs(int32_t sensor_pk) {
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
static esp_err_t register_sensor(int32_t *sensor_pk) {
    const char *server_url = "http://192.168.1.132:5001/sensor";

    // Crea il JSON da inviare
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "tipo", 0); // Sostituisci con il tipo corretto

    char *post_data = cJSON_PrintUnformatted(root);

    // Log dei dati da inviare
    ESP_LOGI(TAG, "Inizio registrazione del sensore...");
    ESP_LOGI(TAG, "URL: %s", server_url);
    ESP_LOGI(TAG, "Dati POST: %s", post_data);

    // Definisci e inizializza la struttura di risposta
    http_response_t response = {
        .buffer = malloc(512),
        .buffer_size = 512,
        .data_received = 0,
    };
    if (response.buffer == NULL) {
        ESP_LOGE(TAG, "Allocazione del buffer di risposta fallita");
        cJSON_Delete(root);
        free(post_data);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = server_url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Imposta l'header e il corpo della richiesta
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    // Esegui la richiesta HTTP
    esp_err_t err = esp_http_client_perform(client);
    ESP_LOGI(TAG, "esp_http_client_perform restituisce: %s", esp_err_to_name(err));

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Codice di stato HTTP: %d", status);

        response.buffer[response.data_received] = '\0'; // Termina la stringa
        ESP_LOGI(TAG, "Corpo della risposta: %s", response.buffer);

        if (response.data_received > 0) {
            // Parsea la risposta JSON
            cJSON *response_json = cJSON_Parse(response.buffer);
            if (response_json) {
                cJSON *sensor_id_item = cJSON_GetObjectItem(response_json, "sensor_id");
                if (sensor_id_item) {
                    *sensor_pk = sensor_id_item->valueint;
                    ESP_LOGI(TAG, "Registrazione sensore riuscita, SensorPk: %d", (int)*sensor_pk);
                    err = ESP_OK;
                } else {
                    ESP_LOGE(TAG, "Sensor ID non trovato nella risposta");
                    err = ESP_FAIL;
                }
                cJSON_Delete(response_json);
            } else {
                ESP_LOGE(TAG, "Errore nel parsing della risposta JSON");
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "Nessuna risposta dal server");
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // Pulizia
    esp_http_client_cleanup(client);
    cJSON_Delete(root);
    free(post_data);
    free(response.buffer);

    return err;
}

// Funzione per inviare il valore del sensore all'API
static esp_err_t send_sensor_value(int32_t sensor_pk, int value, int allarme) {
    const char *url = "http://192.168.1.132:5001/sensor/value";

    // Crea il JSON da inviare
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "sensor_pk", sensor_pk);
    cJSON_AddNumberToObject(root, "value", value);
    cJSON_AddNumberToObject(root, "allarme", allarme);

    char *post_data = cJSON_PrintUnformatted(root);

    // Definisci e inizializza la struttura di risposta
    http_response_t response = {
        .buffer = malloc(512),
        .buffer_size = 512,
        .data_received = 0,
    };
    if (response.buffer == NULL) {
        ESP_LOGE(TAG, "Allocazione del buffer di risposta fallita");
        cJSON_Delete(root);
        free(post_data);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Imposta l'header e il corpo della richiesta
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    // Esegui la richiesta HTTP
    esp_err_t err = esp_http_client_perform(client);
    ESP_LOGI(TAG, "esp_http_client_perform restituisce: %s", esp_err_to_name(err));

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Codice di stato HTTP: %d", status);

        response.buffer[response.data_received] = '\0'; // Termina la stringa
        ESP_LOGI(TAG, "Corpo della risposta: %s", response.buffer);

        if (status == 200 || status == 201) {
            ESP_LOGI(TAG, "Valore inviato con successo");
            err = ESP_OK;
        } else {
            ESP_LOGE(TAG, "Invio del valore fallito, status code: %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // Pulizia
    esp_http_client_cleanup(client);
    cJSON_Delete(root);
    free(post_data);
    free(response.buffer);

    return err;
}

// Funzione per ottenere i parametri del sensore
static esp_err_t get_sensor_parameters(int32_t *sensor_pk) {
    char url[100];
    snprintf(url, sizeof(url), "http://192.168.1.132:5001/sensor/%d", (int)*sensor_pk);

    // Definisci e inizializza la struttura di risposta
    http_response_t response = {
        .buffer = malloc(1024),
        .buffer_size = 1024,
        .data_received = 0,
    };
    if (response.buffer == NULL) {
        ESP_LOGE(TAG, "Allocazione del buffer di risposta fallita");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Esegui la richiesta HTTP
    esp_err_t err = esp_http_client_perform(client);
    ESP_LOGI(TAG, "esp_http_client_perform restituisce: %s", esp_err_to_name(err));

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Codice di stato HTTP: %d", status);

        response.buffer[response.data_received] = '\0'; // Termina la stringa
        ESP_LOGI(TAG, "Corpo della risposta: %s", response.buffer);

        if (status == 200 || status == 201) {
            if (response.data_received > 0) {
                // Parsea la risposta JSON
                cJSON *response_json = cJSON_Parse(response.buffer);
                if (response_json) {
                    cJSON *sensor_array = cJSON_GetObjectItem(response_json, "sensor");
                    if (sensor_array && cJSON_IsArray(sensor_array)) {
                        cJSON *sensor_item = cJSON_GetArrayItem(sensor_array, 0);
                        if (sensor_item) {
                            cJSON *soglia_item = cJSON_GetObjectItem(sensor_item, "Soglia");
                            if (soglia_item) {
                                soglia = soglia_item->valueint;
                                ESP_LOGI(TAG, "Aggiornata soglia: %d", soglia);
                                err = ESP_OK;
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
                    cJSON_Delete(response_json);
                } else {
                    ESP_LOGE(TAG, "Errore nel parsing della risposta JSON");
                    err = ESP_FAIL;
                }
            } else {
                ESP_LOGE(TAG, "Nessuna risposta dal server");
                err = ESP_FAIL;
            }
        } else if (status == 404) {
            ESP_LOGW(TAG, "Sensore non trovato sul server (404). Cancellazione di sensor_pk e riavvio del processo.");
            // Cancellare sensor_pk dalla NVS
            nvs_handle_t nvs_handle;
            if (nvs_open("storage", NVS_READWRITE, &nvs_handle) == ESP_OK) {
                nvs_erase_key(nvs_handle, "sensor_pk");
                nvs_commit(nvs_handle);
                nvs_close(nvs_handle);
                ESP_LOGI(TAG, "sensor_pk cancellato dalla NVS");
            } else {
                ESP_LOGE(TAG, "Errore nell'apertura della NVS per cancellare sensor_pk");
            }
            // Reimposta sensor_pk in memoria
            *sensor_pk = -1;
            err = ESP_FAIL; // Restituisce errore per gestire il caso nel chiamante
        } else {
            ESP_LOGE(TAG, "Recupero dei parametri fallito, status code: %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    // Pulizia
    esp_http_client_cleanup(client);
    free(response.buffer);

    return err;
}

void http_client_task(void *param) {
    // Carica sensor_pk da NVS
    esp_err_t err = load_sensor_pk_from_nvs(&sensor_pk);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SensorPk caricato da NVS: %d", (int)sensor_pk);
    } else {
        sensor_pk = -1; // Assicurarsi che sia impostato a -1
    }

    while (1) {
        if (sensor_pk == -1) {
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

        // Ottieni i parametri del sensore
        err = get_sensor_parameters(&sensor_pk);
        if (err != ESP_OK) {
            if (sensor_pk == -1) {
                // Il sensore non esiste più sul server, riprova a registrarlo
                ESP_LOGI(TAG, "Il sensore non esiste più sul server, registrazione di un nuovo sensore...");
                continue; // Torna all'inizio del loop per registrare un nuovo sensore
            } else {
                // Altro errore, gestisci come preferisci
                ESP_LOGE(TAG, "Errore nel recupero dei parametri del sensore. Riprovo in 10 secondi...");
                vTaskDelay(pdMS_TO_TICKS(10000));
                continue;
            }
        }

        // Inizializza GPIO (se non già inizializzato)
        gpio_handler_init();
        int adc_value_old = -1;
        while (1) {
            // Controlla se è necessario aggiornare i parametri
            if (parameter_update_needed) {
                ESP_LOGI(TAG, "Ricevuta notifica di aggiornamento, ottenimento nuovi parametri...");
                err = get_sensor_parameters(&sensor_pk);
                if (err != ESP_OK) {
                    if (sensor_pk == -1) {
                        // Il sensore non esiste più sul server, riprova a registrarlo
                        ESP_LOGI(TAG, "Il sensore non esiste più sul server, registrazione di un nuovo sensore...");
                        break; // Esci dal loop interno per ricominciare
                    } else {
                        ESP_LOGE(TAG, "Errore nel recupero dei parametri del sensore.");
                    }
                }
                parameter_update_needed = false;
            }

            // Leggi il valore ADC
            int adc_value = gpio_handler_read_adc_channel0(); // Usa il canale ADC corretto

            

            // Determina se scatta l'allarme
            int allarme = adc_value > soglia ? 1 : 0;

            // Invia il valore al server
            if (adc_value != adc_value_old)
            {
                if (send_sensor_value(sensor_pk, adc_value, allarme) != ESP_OK) 
                {
                    ESP_LOGE(TAG, "Errore nell'invio del valore.. Riprovo...");
                }
                else
                {
                    ESP_LOGI(TAG, "Valore ADC: %d", adc_value);
                    adc_value_old = adc_value;
                }
            }

            // Delay prima della prossima lettura
            vTaskDelay(pdMS_TO_TICKS(1000)); // Attendi 1 secondo
        }
    }
}


static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x1) { // Testo
                // Copia i dati ricevuti in una stringa terminata da null
                char *received_data = malloc(data->data_len + 1);
                if (received_data) {
                    memcpy(received_data, data->data_ptr, data->data_len);
                    received_data[data->data_len] = '\0';
                    ESP_LOGI(TAG, "Messaggio WebSocket ricevuto: %s", received_data);

                    free(received_data);

                    // Imposta il flag per aggiornare i parametri
                    parameter_update_needed = true;
                } else {
                    ESP_LOGE(TAG, "Allocazione della memoria per i dati ricevuti fallita");
                }
            }
            break;
        default:
            break;
    }
}

void websocket_client_task(void *param) {
    esp_websocket_client_config_t websocket_cfg = {
        .uri = "ws://192.168.1.132:5001/ui_to_process_update", // Sostituisci con l'endpoint corretto
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);

    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);

    if (esp_websocket_client_start(client) != ESP_OK) {
        ESP_LOGE(TAG, "Impossibile avviare il client WebSocket");
        vTaskDelete(NULL);
    }

    while (1) {
        // Esegui eventuali operazioni periodiche
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay per evitare consumo CPU
    }

    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
    vTaskDelete(NULL);
}

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

void wifi_manager_init(void);
esp_err_t wifi_manager_connect(const char *ssid, const char *password);
void wifi_manager_start_ap(void);
void wifi_manager_erase_credentials(void);
int wifi_manager_credentials_exist(void);
void wifi_manager_wait_for_connection(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H
#ifndef TIME_SYNC_SERVICE_H
#define TIME_SYNC_SERVICE_H

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*time_sync_service_sync_callback_t)(void);

esp_err_t time_sync_service_init(void);
void time_sync_service_register_callback(time_sync_service_sync_callback_t cb);
esp_err_t time_sync_service_start_on_network(void);
bool time_sync_service_is_started(void);
bool time_sync_service_has_valid_time(void);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SYNC_SERVICE_H */

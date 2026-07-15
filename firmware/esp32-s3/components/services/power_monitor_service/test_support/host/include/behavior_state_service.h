#ifndef POWER_MONITOR_HOST_TEST_BEHAVIOR_STATE_SERVICE_H
#define POWER_MONITOR_HOST_TEST_BEHAVIOR_STATE_SERVICE_H

#include "esp_err.h"

esp_err_t behavior_state_set(const char *state_id);
const char *behavior_state_get_current(void);

#endif /* POWER_MONITOR_HOST_TEST_BEHAVIOR_STATE_SERVICE_H */

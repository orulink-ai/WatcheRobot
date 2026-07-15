#ifndef POWER_MONITOR_HOST_TEST_SENSECAP_WATCHER_H
#define POWER_MONITOR_HOST_TEST_SENSECAP_WATCHER_H

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define BSP_PWR_VBUS_IN_DET 0

uint32_t bsp_exp_io_get_level(int pin);
bool bsp_system_is_charging(void);
bool bsp_system_is_standby(void);
bool bsp_battery_is_present(void);
uint16_t bsp_battery_get_voltage(void);
uint8_t bsp_battery_get_percent(void);
esp_err_t bsp_system_shutdown(void);

#endif /* POWER_MONITOR_HOST_TEST_SENSECAP_WATCHER_H */

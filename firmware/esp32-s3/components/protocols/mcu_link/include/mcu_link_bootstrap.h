/**
 * @file mcu_link_bootstrap.h
 * @brief App-facing bootstrap helpers for the static MCU link instance.
 */

#ifndef MCU_LINK_BOOTSTRAP_H
#define MCU_LINK_BOOTSTRAP_H

#include "mcu_link.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mcu_link_bootstrap_init(void);
esp_err_t mcu_link_bootstrap_start(void);
void mcu_link_bootstrap_stop(void);
esp_err_t mcu_link_bootstrap_poll(mcu_link_event_t *out_event);
mcu_link_t *mcu_link_bootstrap_get_link(void);
mcu_link_state_t mcu_link_bootstrap_get_state(void);
bool mcu_link_bootstrap_is_link_ready(void);
bool mcu_link_bootstrap_is_ready(void);
bool mcu_link_bootstrap_handshake_timed_out(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* MCU_LINK_BOOTSTRAP_H */

#ifndef PROVISION_STATE_MACHINE_H
#define PROVISION_STATE_MACHINE_H

#include "provision_manager.h"

provision_transition_result_t provision_state_machine_apply(provision_manager_t *manager,
                                                            const provision_event_t *event);

#endif

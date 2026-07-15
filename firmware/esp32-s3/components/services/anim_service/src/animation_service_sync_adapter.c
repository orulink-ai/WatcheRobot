#include "animation_service_sync_adapter.h"

#include <stdint.h>
#include <string.h>

void animation_service_overflow_buffer_init(animation_service_overflow_buffer_t *buffer) {
    if (buffer != NULL) {
        memset(buffer, 0, sizeof(*buffer));
    }
}

bool animation_service_overflow_buffer_record(animation_service_overflow_buffer_t *buffer, animation_ticket_t ticket) {
    size_t index;
    size_t free_index = ANIMATION_CONTROLLER_TICKET_CAPACITY;

    if (buffer == NULL || ticket == ANIMATION_TICKET_INVALID) {
        return false;
    }
    if (buffer->recorded_count != UINT32_MAX) {
        buffer->recorded_count++;
    }
    for (index = 0U; index < ANIMATION_CONTROLLER_TICKET_CAPACITY; ++index) {
        if (buffer->tickets[index] == ticket) {
            return true;
        }
        if (free_index == ANIMATION_CONTROLLER_TICKET_CAPACITY && buffer->tickets[index] == ANIMATION_TICKET_INVALID) {
            free_index = index;
        }
    }
    if (free_index == ANIMATION_CONTROLLER_TICKET_CAPACITY) {
        return false;
    }
    buffer->tickets[free_index] = ticket;
    return true;
}

bool animation_service_overflow_buffer_take(animation_service_overflow_buffer_t *buffer, animation_ticket_t *ticket) {
    size_t index;

    if (buffer == NULL || ticket == NULL) {
        return false;
    }
    for (index = 0U; index < ANIMATION_CONTROLLER_TICKET_CAPACITY; ++index) {
        if (buffer->tickets[index] != ANIMATION_TICKET_INVALID) {
            *ticket = buffer->tickets[index];
            buffer->tickets[index] = ANIMATION_TICKET_INVALID;
            return true;
        }
    }
    return false;
}

bool animation_service_overflow_buffer_has_pending(const animation_service_overflow_buffer_t *buffer) {
    size_t index;
    if (buffer == NULL) {
        return false;
    }
    for (index = 0U; index < ANIMATION_CONTROLLER_TICKET_CAPACITY; ++index) {
        if (buffer->tickets[index] != ANIMATION_TICKET_INVALID) {
            return true;
        }
    }
    return false;
}

static animation_service_result_t adapter_guard(const animation_service_sync_adapter_t *adapter) {
    if (adapter == NULL) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    if (adapter->dispatching_sink) {
        return ANIMATION_SERVICE_REENTRANT;
    }
    return adapter->stopped ? ANIMATION_SERVICE_STOPPED : ANIMATION_SERVICE_OK;
}

static void dispatch_events(animation_service_sync_adapter_t *adapter, const animation_event_t *events,
                            size_t event_count) {
    size_t index;

    for (index = 0U; index < event_count; ++index) {
        if (adapter->observer != NULL) {
            adapter->observer(&events[index], adapter->observer_context);
        }
        if (adapter->sink != NULL) {
            adapter->dispatching_sink = true;
            adapter->sink(&events[index], adapter->sink_context);
            adapter->dispatching_sink = false;
        }
    }
}

void animation_service_sync_dispatch_output(animation_service_sync_adapter_t *adapter,
                                            const animation_controller_output_t *output) {
    animation_failure_t play_failure = ANIMATION_FAILURE_NONE;
    animation_controller_output_t failure_output;

    dispatch_events(adapter, output->events, output->event_count);
    if (output->player_command.type == ANIMATION_PLAYER_COMMAND_STOP) {
        if (adapter->player.stop != NULL) {
            adapter->player.stop(adapter->player.context);
        }
        return;
    }
    if (output->player_command.type != ANIMATION_PLAYER_COMMAND_PLAY) {
        return;
    }
    if (adapter->player.play == NULL) {
        play_failure = ANIMATION_FAILURE_SERVICE_STOPPED;
    } else {
        play_failure = adapter->player.play(adapter->player.context, output->player_command.ticket,
                                            &output->player_command.request);
    }
    if (play_failure != ANIMATION_FAILURE_NONE &&
        animation_controller_handle_player_event(&adapter->controller, output->player_command.ticket,
                                                 ANIMATION_PLAYER_EVENT_FAILED, play_failure,
                                                 &failure_output) == ANIMATION_SERVICE_OK) {
        animation_service_sync_dispatch_output(adapter, &failure_output);
    }
}

static void terminate_all_live(animation_service_sync_adapter_t *adapter, animation_event_type_t terminal_type,
                               animation_failure_t failure) {
    animation_event_t terminal_events[ANIMATION_CONTROLLER_TICKET_CAPACITY];
    size_t event_count = 0U;
    size_t index;

    for (index = 0U; index < ANIMATION_CONTROLLER_TICKET_CAPACITY; ++index) {
        animation_controller_ticket_slot_t *slot = &adapter->controller.tickets[index];
        if (!slot->in_use) {
            continue;
        }
        terminal_events[event_count].ticket = slot->ticket;
        terminal_events[event_count].type = terminal_type;
        terminal_events[event_count].failure = failure;
        terminal_events[event_count].request = slot->request;
        terminal_events[event_count].completed_cycles = slot->completed_cycles;
        memset(slot, 0, sizeof(*slot));
        ++event_count;
    }
    memset(adapter->controller.queued_tickets, 0, sizeof(adapter->controller.queued_tickets));
    adapter->controller.queued_count = 0U;
    adapter->controller.active_ticket = ANIMATION_TICKET_INVALID;
    adapter->controller.desired_ticket = ANIMATION_TICKET_INVALID;
    for (index = 0U; index < event_count; ++index) {
        animation_controller_record_terminal_metrics(&adapter->controller, terminal_type);
    }
    dispatch_events(adapter, terminal_events, event_count);
}

void animation_service_sync_adapter_init(animation_service_sync_adapter_t *adapter,
                                         const animation_service_player_ops_t *player) {
    if (adapter == NULL) {
        return;
    }
    memset(adapter, 0, sizeof(*adapter));
    animation_controller_init(&adapter->controller);
    if (player != NULL) {
        adapter->player = *player;
    }
}

void animation_service_sync_set_event_observer(animation_service_sync_adapter_t *adapter,
                                               animation_service_event_observer_t observer, void *context) {
    if (adapter == NULL) {
        return;
    }
    adapter->observer = observer;
    adapter->observer_context = context;
}

animation_service_result_t animation_service_sync_submit(animation_service_sync_adapter_t *adapter,
                                                         const animation_request_t *request,
                                                         animation_ticket_t *out_ticket) {
    animation_controller_output_t output;
    animation_service_result_t result = animation_service_sync_prepare_submit(adapter, request, out_ticket, &output);

    if (result == ANIMATION_SERVICE_OK) {
        animation_service_sync_dispatch_output(adapter, &output);
    }
    return result;
}

animation_service_result_t animation_service_sync_prepare_submit(animation_service_sync_adapter_t *adapter,
                                                                 const animation_request_t *request,
                                                                 animation_ticket_t *out_ticket,
                                                                 animation_controller_output_t *output) {
    animation_service_result_t result = adapter_guard(adapter);

    if (out_ticket != NULL) {
        *out_ticket = ANIMATION_TICKET_INVALID;
    }
    if (result != ANIMATION_SERVICE_OK) {
        return result;
    }
    if (!adapter->surface_bound) {
        return ANIMATION_SERVICE_NO_SURFACE;
    }
    if (adapter->suspended) {
        return ANIMATION_SERVICE_STOPPED;
    }
    if (output == NULL) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    return animation_controller_submit(&adapter->controller, request, out_ticket, output);
}

animation_service_result_t animation_service_sync_cancel(animation_service_sync_adapter_t *adapter,
                                                         animation_ticket_t ticket) {
    animation_controller_output_t output;
    animation_service_result_t result = adapter_guard(adapter);

    if (result != ANIMATION_SERVICE_OK) {
        return result;
    }
    result = animation_controller_cancel(&adapter->controller, ticket, &output);
    if (result == ANIMATION_SERVICE_OK) {
        animation_service_sync_dispatch_output(adapter, &output);
    }
    return result;
}

animation_service_result_t animation_service_sync_cancel_owner(animation_service_sync_adapter_t *adapter,
                                                               animation_source_t source, uint32_t owner_epoch) {
    animation_controller_output_t output;
    animation_service_result_t result = adapter_guard(adapter);

    if (result != ANIMATION_SERVICE_OK) {
        return result;
    }
    result = animation_controller_cancel_owner(&adapter->controller, source, owner_epoch, &output);
    if (result == ANIMATION_SERVICE_OK) {
        animation_service_sync_dispatch_output(adapter, &output);
    }
    return result;
}

animation_service_result_t animation_service_sync_player_event(animation_service_sync_adapter_t *adapter,
                                                               animation_ticket_t ticket,
                                                               animation_player_event_type_t type,
                                                               animation_failure_t failure) {
    animation_controller_output_t output;
    animation_service_result_t result = adapter_guard(adapter);

    if (result != ANIMATION_SERVICE_OK) {
        return result;
    }
    result = animation_controller_handle_player_event(&adapter->controller, ticket, type, failure, &output);
    if (result == ANIMATION_SERVICE_OK) {
        animation_service_sync_dispatch_output(adapter, &output);
    }
    return result;
}

animation_service_result_t animation_service_sync_get_snapshot(animation_service_sync_adapter_t *adapter,
                                                               animation_snapshot_t *snapshot) {
    animation_service_result_t result = adapter_guard(adapter);

    if (snapshot == NULL) {
        return ANIMATION_SERVICE_INVALID_ARGUMENT;
    }
    if (result != ANIMATION_SERVICE_OK) {
        return result;
    }
    animation_controller_get_snapshot(&adapter->controller, snapshot);
    return ANIMATION_SERVICE_OK;
}

animation_service_result_t animation_service_sync_set_sink(animation_service_sync_adapter_t *adapter,
                                                           animation_event_sink_t sink, void *context) {
    animation_service_result_t result = adapter_guard(adapter);

    if (result != ANIMATION_SERVICE_OK) {
        return result;
    }
    adapter->sink = sink;
    adapter->sink_context = context;
    return ANIMATION_SERVICE_OK;
}

animation_service_result_t animation_service_sync_set_surface(animation_service_sync_adapter_t *adapter,
                                                              bool surface_bound) {
    animation_service_result_t result = adapter_guard(adapter);

    if (result != ANIMATION_SERVICE_OK) {
        return result;
    }
    if (adapter->surface_bound && !surface_bound) {
        if (adapter->player.stop != NULL) {
            adapter->player.stop(adapter->player.context);
        }
        terminate_all_live(adapter, ANIMATION_EVENT_FAILED, ANIMATION_FAILURE_SERVICE_STOPPED);
    }
    adapter->surface_bound = surface_bound;
    return ANIMATION_SERVICE_OK;
}

animation_service_result_t animation_service_sync_set_suspended(animation_service_sync_adapter_t *adapter,
                                                                bool suspended) {
    animation_service_result_t result = adapter_guard(adapter);

    if (result != ANIMATION_SERVICE_OK) {
        return result;
    }
    if (!adapter->suspended && suspended) {
        if (adapter->player.stop != NULL) {
            adapter->player.stop(adapter->player.context);
        }
        terminate_all_live(adapter, ANIMATION_EVENT_CANCELLED, ANIMATION_FAILURE_NONE);
    }
    adapter->suspended = suspended;
    return ANIMATION_SERVICE_OK;
}

animation_service_result_t animation_service_sync_stop(animation_service_sync_adapter_t *adapter) {
    animation_service_result_t result = adapter_guard(adapter);

    if (result != ANIMATION_SERVICE_OK) {
        return result;
    }
    if (adapter->player.stop != NULL) {
        adapter->player.stop(adapter->player.context);
    }
    terminate_all_live(adapter, ANIMATION_EVENT_FAILED, ANIMATION_FAILURE_SERVICE_STOPPED);
    adapter->stopped = true;
    return ANIMATION_SERVICE_OK;
}

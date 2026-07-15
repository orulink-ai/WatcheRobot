#ifndef ANIMATION_SERVICE_SYNC_ADAPTER_H
#define ANIMATION_SERVICE_SYNC_ADAPTER_H

#include "animation_controller_core.h"

typedef struct {
    void *context;
    animation_failure_t (*play)(void *context, animation_ticket_t ticket, const animation_request_t *request);
    void (*stop)(void *context);
} animation_service_player_ops_t;

typedef void (*animation_service_event_observer_t)(const animation_event_t *event, void *context);

typedef struct {
    animation_controller_t controller;
    animation_service_player_ops_t player;
    animation_event_sink_t sink;
    void *sink_context;
    animation_service_event_observer_t observer;
    void *observer_context;
    bool surface_bound;
    bool suspended;
    bool stopped;
    bool dispatching_sink;
} animation_service_sync_adapter_t;

typedef struct {
    animation_ticket_t tickets[ANIMATION_CONTROLLER_TICKET_CAPACITY];
    uint32_t recorded_count;
} animation_service_overflow_buffer_t;

void animation_service_overflow_buffer_init(animation_service_overflow_buffer_t *buffer);
bool animation_service_overflow_buffer_record(animation_service_overflow_buffer_t *buffer, animation_ticket_t ticket);
bool animation_service_overflow_buffer_take(animation_service_overflow_buffer_t *buffer, animation_ticket_t *ticket);
bool animation_service_overflow_buffer_has_pending(const animation_service_overflow_buffer_t *buffer);

void animation_service_sync_adapter_init(animation_service_sync_adapter_t *adapter,
                                         const animation_service_player_ops_t *player);
void animation_service_sync_set_event_observer(animation_service_sync_adapter_t *adapter,
                                               animation_service_event_observer_t observer, void *context);
animation_service_result_t animation_service_sync_submit(animation_service_sync_adapter_t *adapter,
                                                         const animation_request_t *request,
                                                         animation_ticket_t *out_ticket);
animation_service_result_t animation_service_sync_prepare_submit(animation_service_sync_adapter_t *adapter,
                                                                 const animation_request_t *request,
                                                                 animation_ticket_t *out_ticket,
                                                                 animation_controller_output_t *output);
void animation_service_sync_dispatch_output(animation_service_sync_adapter_t *adapter,
                                            const animation_controller_output_t *output);
animation_service_result_t animation_service_sync_cancel(animation_service_sync_adapter_t *adapter,
                                                         animation_ticket_t ticket);
animation_service_result_t animation_service_sync_cancel_owner(animation_service_sync_adapter_t *adapter,
                                                               animation_source_t source, uint32_t owner_epoch);
animation_service_result_t animation_service_sync_player_event(animation_service_sync_adapter_t *adapter,
                                                               animation_ticket_t ticket,
                                                               animation_player_event_type_t type,
                                                               animation_failure_t failure);
animation_service_result_t animation_service_sync_get_snapshot(animation_service_sync_adapter_t *adapter,
                                                               animation_snapshot_t *snapshot);
animation_service_result_t animation_service_sync_set_sink(animation_service_sync_adapter_t *adapter,
                                                           animation_event_sink_t sink, void *context);
animation_service_result_t animation_service_sync_set_surface(animation_service_sync_adapter_t *adapter,
                                                              bool surface_bound);
animation_service_result_t animation_service_sync_set_suspended(animation_service_sync_adapter_t *adapter,
                                                                bool suspended);
animation_service_result_t animation_service_sync_stop(animation_service_sync_adapter_t *adapter);

#endif /* ANIMATION_SERVICE_SYNC_ADAPTER_H */

/**
 * @file animation_controller_core.h
 * @brief Allocation-free animation arbitration state machine.
 */

#ifndef ANIMATION_CONTROLLER_CORE_H
#define ANIMATION_CONTROLLER_CORE_H

#include "animation_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANIMATION_CONTROLLER_QUEUE_CAPACITY 8U
#define ANIMATION_CONTROLLER_TICKET_CAPACITY 16U
#define ANIMATION_CONTROLLER_OUTPUT_EVENT_CAPACITY (ANIMATION_CONTROLLER_TICKET_CAPACITY + 2U)

typedef enum {
    ANIMATION_PLAYER_COMMAND_NONE = 0,
    ANIMATION_PLAYER_COMMAND_PLAY,
    ANIMATION_PLAYER_COMMAND_STOP,
} animation_player_command_type_t;

typedef enum {
    ANIMATION_PLAYER_EVENT_COMMITTED = 0,
    ANIMATION_PLAYER_EVENT_CYCLE_COMPLETED,
    ANIMATION_PLAYER_EVENT_COMPLETED,
    ANIMATION_PLAYER_EVENT_FAILED,
} animation_player_event_type_t;

typedef struct {
    animation_player_command_type_t type;
    animation_ticket_t ticket;
    animation_request_t request;
} animation_player_command_t;

typedef struct {
    animation_event_t events[ANIMATION_CONTROLLER_OUTPUT_EVENT_CAPACITY];
    size_t event_count;
    animation_player_command_t player_command;
} animation_controller_output_t;

typedef enum {
    ANIMATION_TICKET_PHASE_ACCEPTED = 0,
    ANIMATION_TICKET_PHASE_PREPARING,
    ANIMATION_TICKET_PHASE_COMMITTED,
} animation_ticket_phase_t;

typedef struct {
    bool in_use;
    animation_ticket_t ticket;
    animation_request_t request;
    uint32_t sequence;
    uint16_t completed_cycles;
    animation_ticket_phase_t phase;
} animation_controller_ticket_slot_t;

typedef struct {
    animation_controller_ticket_slot_t tickets[ANIMATION_CONTROLLER_TICKET_CAPACITY];
    animation_ticket_t queued_tickets[ANIMATION_CONTROLLER_QUEUE_CAPACITY];
    size_t queued_count;
    animation_ticket_t active_ticket;
    animation_ticket_t desired_ticket;
    animation_ticket_t visible_ticket;
    emoji_anim_type_t visible_type;
    animation_ticket_t next_ticket;
    uint32_t next_sequence;
    uint32_t emitted_terminal_count;
    uint32_t accepted_count;
    uint32_t completed_count;
    uint32_t preempted_count;
    uint32_t cancelled_count;
    uint32_t failed_count;
    uint32_t orphan_player_event_count;
    uint32_t wrong_transition_count;
} animation_controller_t;

void animation_controller_record_terminal_metrics(animation_controller_t *controller,
                                                  animation_event_type_t terminal_type);

void animation_controller_init(animation_controller_t *controller);

animation_service_result_t animation_controller_submit(animation_controller_t *controller,
                                                       const animation_request_t *request,
                                                       animation_ticket_t *out_ticket,
                                                       animation_controller_output_t *output);

animation_service_result_t animation_controller_cancel(animation_controller_t *controller, animation_ticket_t ticket,
                                                       animation_controller_output_t *output);

animation_service_result_t animation_controller_cancel_owner(animation_controller_t *controller,
                                                             animation_source_t source, uint32_t owner_epoch,
                                                             animation_controller_output_t *output);

animation_service_result_t animation_controller_handle_player_event(animation_controller_t *controller,
                                                                    animation_ticket_t ticket,
                                                                    animation_player_event_type_t event,
                                                                    animation_failure_t failure,
                                                                    animation_controller_output_t *output);

void animation_controller_get_snapshot(const animation_controller_t *controller, animation_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* ANIMATION_CONTROLLER_CORE_H */

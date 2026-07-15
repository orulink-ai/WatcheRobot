#include "sfx_schedule.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_take_due_waits_until_due_time(void) {
    sfx_schedule_t schedule = {0};
    char sound_id[SFX_SCHEDULE_ID_LEN];
    int64_t late_ms = -1;

    assert(sfx_schedule_enqueue(&schedule, "happy", 1500, NULL, 0));
    assert(sfx_schedule_has_active(&schedule));
    assert(!sfx_schedule_has_due(&schedule, 1499));
    assert(!sfx_schedule_take_due(&schedule, 1499, sound_id, sizeof(sound_id), &late_ms));

    assert(sfx_schedule_has_due(&schedule, 1500));
    assert(sfx_schedule_take_due(&schedule, 1500, sound_id, sizeof(sound_id), &late_ms));
    assert(strcmp(sound_id, "happy") == 0);
    assert(late_ms == 0);
    assert(!sfx_schedule_has_active(&schedule));
}

static void test_take_due_orders_by_time_then_insert_sequence(void) {
    sfx_schedule_t schedule = {0};
    char sound_id[SFX_SCHEDULE_ID_LEN];
    int64_t late_ms = -1;

    assert(sfx_schedule_enqueue(&schedule, "third", 300, NULL, 0));
    assert(sfx_schedule_enqueue(&schedule, "first", 100, NULL, 0));
    assert(sfx_schedule_enqueue(&schedule, "second", 100, NULL, 0));

    assert(sfx_schedule_take_due(&schedule, 300, sound_id, sizeof(sound_id), &late_ms));
    assert(strcmp(sound_id, "first") == 0);
    assert(late_ms == 200);

    assert(sfx_schedule_take_due(&schedule, 300, sound_id, sizeof(sound_id), &late_ms));
    assert(strcmp(sound_id, "second") == 0);
    assert(late_ms == 200);

    assert(sfx_schedule_take_due(&schedule, 300, sound_id, sizeof(sound_id), &late_ms));
    assert(strcmp(sound_id, "third") == 0);
    assert(late_ms == 0);
}

static void test_full_queue_replaces_latest_request(void) {
    sfx_schedule_t schedule = {0};
    char sound_id[SFX_SCHEDULE_ID_LEN];
    char replaced[SFX_SCHEDULE_ID_LEN];
    int64_t late_ms = -1;
    int i;

    for (i = 0; i < SFX_SCHEDULE_CAPACITY; ++i) {
        char id[SFX_SCHEDULE_ID_LEN];
        snprintf(id, sizeof(id), "sound-%02d", i);
        assert(sfx_schedule_enqueue(&schedule, id, (int64_t)i * 100, NULL, 0));
    }

    assert(sfx_schedule_enqueue(&schedule, "urgent", 50, replaced, sizeof(replaced)));
    assert(strcmp(replaced, "sound-15") == 0);

    assert(sfx_schedule_take_due(&schedule, 50, sound_id, sizeof(sound_id), &late_ms));
    assert(strcmp(sound_id, "sound-00") == 0);
    assert(sfx_schedule_take_due(&schedule, 50, sound_id, sizeof(sound_id), &late_ms));
    assert(strcmp(sound_id, "urgent") == 0);
}

static void test_clear_discards_pending_events(void) {
    sfx_schedule_t schedule = {0};
    char sound_id[SFX_SCHEDULE_ID_LEN];

    assert(sfx_schedule_enqueue(&schedule, "happy", 0, NULL, 0));
    sfx_schedule_clear(&schedule);

    assert(!sfx_schedule_has_active(&schedule));
    assert(!sfx_schedule_has_due(&schedule, 1000));
    assert(!sfx_schedule_take_due(&schedule, 1000, sound_id, sizeof(sound_id), NULL));
}

int main(void) {
    test_take_due_waits_until_due_time();
    test_take_due_orders_by_time_then_insert_sequence();
    test_full_queue_replaces_latest_request();
    test_clear_discards_pending_events();

    puts("sfx schedule host tests passed");
    return 0;
}

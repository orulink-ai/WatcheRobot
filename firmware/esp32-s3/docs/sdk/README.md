# WatcheRobot SDK v1

WatcheRobot v1 exposes the same robot abilities through two front doors:

- The embedded C SDK is used by offline firmware Applications.
- The desktop Python SDK uses `sdk.control.app` and the WebSocket protocol.

Both paths share the device services for Behavior, animation, servo motion, SFX, LED, microphone, and camera. The C
SDK owns session/Job state; App Runtime remains responsible for foreground ownership.

## Embedded lifecycle

An Application opens one `watcher_sdk_context_t` during `on_open`, calls `watcher_sdk_tick`/`watcher_sdk_poll_event`
from `on_tick`, and closes the context during `on_close`. C calls are non-blocking and no SDK API waits for hardware.

```c
static watcher_sdk_context_t *s_sdk;

static void my_app_on_open(void) {
    const watcher_sdk_config_t config = {.app_id = "example.offline.app"};
    if (watcher_sdk_open(&config, &s_sdk) == WATCHER_SDK_RESULT_OK) {
        watcher_sdk_job_id_t job_id;
        (void)watcher_behavior_play(s_sdk, "greeting", 1, &job_id);
    }
}

static void my_app_on_tick(void) {
    watcher_sdk_event_t event;
    watcher_sdk_tick(s_sdk);
    while (watcher_sdk_poll_event(s_sdk, &event)) {
        /* Route STARTING/RUNNING/terminal events to the Application. */
    }
}

static void my_app_on_close(void) {
    watcher_sdk_close(s_sdk);
    s_sdk = NULL;
}
```

The complete compilable-shaped example is in
[`components/sdk/watcher_sdk/examples/offline_app_example.c`](../../components/sdk/watcher_sdk/examples/offline_app_example.c).

## Conflict rules

- One Behavior may run at a time.
- A direct command cancels the whole current Behavior, then replaces the old operation in its own domain.
- Direct operations in different domains may overlap.
- `motion.set_target`, light color, and light off are real-time operations without a public Job.
- `motion.move_to` completes from the matching STM32 execution event, with a bounded timeout fallback; it does not
  claim physical position-feedback convergence.
- Closing the context cancels all Jobs, stops motion/audio/lights, closes media, and releases camera resources.

See [behavior-schema-v1.md](behavior-schema-v1.md) and [protocol-v1.md](protocol-v1.md).

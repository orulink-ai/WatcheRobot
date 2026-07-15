# Behavior Schema v1

A Behavior is a device-side timeline containing motion, animation, audio, and light tracks. It describes **what to
play together**; the Application still decides **when and why** to play it. v1 has no conditions, nested Behaviors,
arbitrary state graph, or Python inline timeline.

```json
{
  "schema_version": "1.0",
  "default_behavior": "greeting",
  "behaviors": {
    "greeting": {
      "loop": false,
      "motion": [
        {
          "at_ms": 0,
          "pan_deg": 110,
          "tilt_deg": 120,
          "duration_ms": 300,
          "profile": "ease_in_out"
        }
      ],
      "animation": [
        {"at_ms": 0, "anim": "happy", "playback_mode": "once"}
      ],
      "audio": [
        {"at_ms": 20, "sound_id": "hello"}
      ],
      "light": [
        {
          "at_ms": 0,
          "effect": "breathing",
          "red": 77,
          "green": 163,
          "blue": 255,
          "brightness": 70,
          "period_ms": 800,
          "repeat": 1,
          "zone": "all"
        }
      ]
    }
  }
}
```

`at_ms` is relative to Behavior start. The scheduler runs at 10 ms and dispatches all due tracks in one task.

## Compatibility aliases

The parser keeps existing installed resources working while exposing clearer public names:

| Public v1 | Existing compatibility name |
| --- | --- |
| `schema_version` | `version` |
| `default_behavior` | `default_state` |
| `behaviors` | `states` |
| `animation` | `expression` |
| `audio` | `sound` |
| `pan_deg` / `tilt_deg` | `x_deg` / `y_deg` |
| `profile` | `motion_profile` |
| `repeat` | `repeat_count` |

Legacy automatic transition fields remain internal compatibility behavior. New v1 Behavior definitions should not
use them.

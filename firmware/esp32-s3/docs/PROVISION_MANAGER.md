# Provision Manager Phase 1

## Scope

Phase 1 separates provisioning lifecycle state from `provision.app` without
changing product boot routing. The existing app remains the user interface and
still requests the explicit `wifi+ble+provisioning` resource set.

The manager deliberately does not:

- own Wi-Fi or BLE drivers;
- create or mutate LVGL objects;
- persist snapshots to NVS;
- automatically route an unprovisioned boot into the Provision UI;
- stop work when the Provision UI closes.

## State model

The pure state machine supports:

```text
UNPROVISIONED -> WAIT_BLE -> WIFI_CONFIGURING -> WIFI_VALIDATING -> READY
                                      |                  |
                                      +-> FAILED_RETRYABLE

Any state -> REPAIR_REQUIRED
Any state -> FACTORY_RESET_PENDING -> UNPROVISIONED
```

Every attempt has a non-zero `generation`. A retry, a new credential update
from `READY`, or a completed factory reset advances the generation. Events from
an earlier generation return `PROVISION_TRANSITION_STALE` without changing live
state.

`resume` and `suspend` only attach or detach the UI. Suspending does not reset
state, clear failure information, or cancel the active attempt.

## Threading boundary

BLE and Wi-Fi callbacks do not call the manager directly. They publish small
pending facts, and the main loop consumes those facts in
`provision_manager_platform_tick()`. State transitions therefore remain
serialized on the application main task.

This is an interim adapter. A later phase should replace the pending flags with
a bounded event queue carrying the generation captured by each asynchronous
operation.

## Host regression

The component host test covers:

- first-time entry into `WAIT_BLE`;
- successful BLE credential and Wi-Fi validation flow;
- retry generation advancement and stale callback rejection;
- illegal transition rejection;
- UI detach without lifecycle cancellation;
- repair-required and factory-reset recovery paths.

`main/test_support/host` also contains a static adapter contract that verifies
the manager is initialized at platform startup, ticked from the main loop, and
resumed/suspended by `provision.app`.

Both tests are included in `tools/run-resource-host-tests.ps1`.

## Next phase

Before enabling automatic boot routing, add:

1. versioned and checksummed NVS snapshots;
2. an explicit bounded event queue with operation-time generation capture;
3. retry deadlines and failure classification from Wi-Fi callbacks;
4. factory-reset coordination that atomically clears credentials and snapshot;
5. COM hardware tests for interrupted provisioning, wrong credentials, reboot
   recovery, and ten repeated Provision round trips.

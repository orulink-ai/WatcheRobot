<div align="center">

<p><strong>English</strong> | <a href="COMMUNICATION_PROTOCOLS_zh.md">简体中文</a></p>

</div>

# Communication Protocols

This is the public firmware protocol overview for WatcheRobot. It is intended as the first protocol document to read before opening the lower-level ESP32-S3 and STM32F103 implementation notes.

Code review date: 2026-06-22.

Release boundary for this version:

- The body of this document only describes behavior confirmed from the current source tree.
- Items that are implemented on only one side, planned, insufficiently tested, or still semantically ambiguous are not part of the stable public protocol contract for this release.
- Those items are collected under [Protocol TODOs](#5-protocol-todos) so the release can ship without hiding known protocol work.

## 1. Link Overview

| Link | Purpose | Current code entry |
| --- | --- | --- |
| ESP32-S3 <-> STM32F103 | Board-internal co-processor link for servo, touch, sensor, power, link status, and reserved LED traffic | `esp32-s3/components/protocols/mcu_link/`, `stm32-f103/User/Protocol/`, `stm32-f103/User/Platform/stm32/platform_coproc_uart.c` |
| ESP32-S3 <-> Server | Wi-Fi provisioning, UDP Discovery, WebSocket JSON control, and WSPK binary media frames | `esp32-s3/components/utils/wifi_manager/`, `esp32-s3/components/protocols/discovery/`, `esp32-s3/components/protocols/ws_client/` |
| ESP32-S3 <-> App | BLE GATT local control and Wi-Fi provisioning | `esp32-s3/components/protocols/ble_service/` |

## 2. ESP32-S3 <-> STM32F103

This is the board-internal UART protocol. The ESP32-S3 side uses UART2 by default, TX on GPIO19, RX on GPIO20, and `921600` baud. The STM32F103 side uses `USART2@921600 8N1`, with `RX DMA + IDLE` as the receive path.

One wire packet is:

```text
COBS(raw_frame) + 0x00 delimiter
```

`raw_frame` is encoded as follows. Multi-byte integers are little-endian.

| Field | Size | Current value or meaning |
| --- | --- | --- |
| `magic0` | 1 | `0xA5` |
| `magic1` | 1 | `0x5A` |
| `proto_version` | 1 | `0x01` |
| `msg_class` | 1 | Message class |
| `msg_id` | 1 | Message ID inside the class |
| `flags` | 1 | `ACK_REQ=bit0`, `RESPONSE=bit1`, `FINAL=bit2` |
| `seq` | 4 | Sender-side incrementing sequence |
| `payload_len` | 2 | Maximum `128` bytes |
| `payload` | N | Business payload |
| `crc16` | 2 | CRC16-CCITT-FALSE, init `0xFFFF`, over header and payload |

Message classes:

| Class | Meaning |
| --- | --- |
| `0x01` | SYS: handshake, ACK, NACK, FAULT |
| `0x02` | MOTION: servo and motion |
| `0x03` | LED: class reserved in the current protocol space, command payload alignment is tracked in TODOs |
| `0x04` | SENSOR: touch, IMU, magnetometer |
| `0x05` | POWER: 5V power control |

Current handshake:

1. ESP32-S3 sends `SYS / HELLO_REQ(0x01)` with empty payload and `ACK_REQ`.
2. STM32F103 validates the frame and returns `SYS / ACK(0x04)`.
3. STM32F103 returns `SYS / HELLO_RSP(0x02)` with a `9` byte payload.

Confirmed stable message surface in the current source:

| Direction | Class / message | Payload length | Notes |
| --- | --- | --- | --- |
| ESP32-S3 -> STM32F103 | `SYS / HELLO_REQ(0x01)` | `0` | Starts MCU Link handshake, requires `ACK_REQ` |
| STM32F103 -> ESP32-S3 | `SYS / ACK(0x04)` | `6` | Response payload includes referenced sequence and status |
| STM32F103 -> ESP32-S3 | `SYS / NACK(0x05)` | `8` | Response payload includes referenced sequence, status, and reason |
| STM32F103 -> ESP32-S3 | `SYS / FAULT(0x06)` | `9` | Reports link/runtime faults |
| STM32F103 -> ESP32-S3 | `SYS / HELLO_RSP(0x02)` | `9` | Reports role, firmware/hardware version, capability bitmap, sensor bitmap, boot reason, default stream profile |
| ESP32-S3 -> STM32F103 | `MOTION / SERVO_MOVE(0x01)` | `9` | Axis mask, X/Y target in deg x10, duration, profile, source |
| ESP32-S3 -> STM32F103 | `MOTION / SERVO_STOP(0x02)` | `2` | Stop scope and source |
| ESP32-S3 -> STM32F103 | `MOTION / SERVO_JOG(0x05)` | `12` | Axis mask, X/Y velocity in deg x10/s, timeout, source, axis limits |
| STM32F103 -> ESP32-S3 | `MOTION / MOTION_DONE(0x03)` | `11` | Referenced sequence, result, final X/Y in deg x10, execution time |
| STM32F103 -> ESP32-S3 | `SENSOR / TOUCH_EVENT(0x01)` | `6` | Touch ID, event code, timestamp |
| STM32F103 -> ESP32-S3 | `SENSOR / MAG_STATE(0x02)` | `6` | Heading, field norm, quality, status bits |
| STM32F103 -> ESP32-S3 | `SENSOR / IMU_STATE(0x04)` | `11` | Roll, pitch, yaw, acceleration norm, gyro norm, motion flags |
| ESP32-S3 -> STM32F103 | `POWER / 5V_ENABLE(0x01)` | `1` | Source tag |
| ESP32-S3 -> STM32F103 | `POWER / 5V_DISABLE(0x02)` | `1` | Source tag |

The shared core format and constants match on both sides: magic, protocol version, maximum payload, header length, CRC length, COBS delimiter, and class values.

ESP32-S3 declares additional forward-looking IDs, such as heartbeat, snapshot, motion sequence, LED state, and additional sensor events. STM32F103 does not currently implement all of them. They are intentionally not documented as stable STM32 capabilities in this release; see [Protocol TODOs](#5-protocol-todos).

Code references:

- ESP32-S3 frame definition: [mcu_frame.h](esp32-s3/components/protocols/mcu_link/include/mcu_frame.h)
- ESP32-S3 default UART config: [Kconfig](esp32-s3/components/protocols/mcu_link/Kconfig)
- STM32F103 frame definition: [coproc_protocol_types.h](stm32-f103/User/Protocol/coproc_protocol_types.h)
- STM32F103 codec: [coproc_frame_codec.c](stm32-f103/User/Protocol/coproc_frame_codec.c)
- STM32F103 UART integration: [platform_coproc_uart.c](stm32-f103/User/Platform/stm32/platform_coproc_uart.c)

## 3. ESP32-S3 <-> Server

The server link has three steps:

```text
Wi-Fi STA connection -> UDP Discovery -> WebSocket business traffic
```

Current Wi-Fi behavior:

- Mode: STA.
- Credential storage: ESP-IDF Wi-Fi flash storage.
- Default connection wait: `10000 ms`.
- While BLE is connected, firmware pauses the background Wi-Fi / WebSocket link. After BLE disconnects, it resumes background connectivity if Wi-Fi credentials are available.

Current UDP Discovery behavior:

- Port: `37020`.
- Default total timeout: `30000 ms`.
- Up to `3` sends per round, with about `5000 ms` between rounds.
- The device sends `DISCOVER` with `device_id` and `mac`.
- After receiving `ANNOUNCE`, firmware requires `ip`, uses `port` when present, and defaults the WebSocket port to `8765` when `port` is missing.
- Firmware parses optional `version`, `protocol_version`, and `server` fields for reporting.

Current WebSocket behavior:

1. After successful Discovery, firmware builds `ws://<ip>:<port>`.
2. After WebSocket connects, ESP32-S3 first sends `sys.client.hello`.
3. Firmware treats the session as ready only after receiving `sys.ack` for `sys.client.hello`.
4. Text messages use a JSON envelope: `{"type":"...", "code":0, "data":{}}`.

Downlink types currently routed in source:

```text
sys.ack, sys.nack, sys.ping, sys.pong, sys.session.resume
ctrl.servo.angle
ctrl.motion.jog, ctrl.motion.stop
ctrl.microphone.open, ctrl.microphone.close
ctrl.robot.state.set
ctrl.camera.video_config, ctrl.camera.capture_image
ctrl.camera.start_video, ctrl.camera.stop_video
evt.asr.result, evt.ai.status, evt.ai.thinking, evt.ai.reply
xfer.ota.handshake, xfer.ota.checksum
```

Binary media frames use the `WSPK` header in the current source. The current header length is `14` bytes:

| Offset | Size | Field | Meaning |
| --- | --- | --- | --- |
| `0` | 4 | `magic` | ASCII `"WSPK"` |
| `4` | 1 | `frame_type` | `1=audio`, `2=video`, `3=image`, `4=ota` |
| `5` | 1 | `flags` | `FIRST=bit0`, `LAST=bit1`, `KEYFRAME=bit2`, `FRAGMENT=bit3` |
| `6` | 4 | `seq` | Incremented independently by frame type |
| `10` | 4 | `payload_len` | Payload byte length |

Note: the current WSPK header has no `stream_id` field.

Code references:

- Discovery: [discovery_client.h](esp32-s3/components/protocols/discovery/include/discovery_client.h)
- WebSocket interface: [ws_client.h](esp32-s3/components/protocols/ws_client/include/ws_client.h)
- WebSocket router: [ws_router.c](esp32-s3/components/protocols/ws_client/src/ws_router.c)

## 4. ESP32-S3 <-> App / BLE

BLE uses one service and one characteristic. It supports both legacy text commands and JSON commands.

| Item | Current value |
| --- | --- |
| Service UUID | `0x00FF` |
| Characteristic UUID | `0xFF01` |
| Characteristic property | `READ`, `WRITE`, `NOTIFY` |
| Maximum characteristic value | `256` bytes |

JSON mode detection: after trimming leading whitespace, a payload whose first character is `{` is parsed as JSON. Otherwise it is handled as a legacy text command.

JSON mode currently supports:

```text
ctrl.servo.angle
ctrl.motion.jog
ctrl.motion.stop
evt.ai.status
ctrl.robot.state.set
cfg.wifi.set
cfg.wifi.get
cfg.wifi.clear
sys.ping
```

JSON responses:

- Success: `sys.ack`
- Failure: `sys.nack`
- Ping: `sys.pong`
- Wi-Fi status notification: `evt.wifi.status`

Legacy text commands currently support:

```text
X:<angle>
Y:<angle>
SET_SERVO:<...>
SERVO_MOVE:<...>
WIFI_CONFIG:<ssid>,<password>
WIFI_STATUS
WIFI_CLEAR
PING
```

Code references:

- BLE interface: [ble_service.h](esp32-s3/components/protocols/ble_service/include/ble_service.h)
- BLE implementation: [ble_service.c](esp32-s3/components/protocols/ble_service/src/ble_service.c)

## 5. Protocol TODOs

The following items are intentionally not promoted into the stable release protocol body yet. They need tests, cross-end confirmation, or implementation alignment before becoming public contract. The main tracking issue is [#5](https://github.com/orulink-ai/WatcheRobot/issues/5).

- Build a MCU Link cross-end contract matrix for class, message ID, payload length, direction, ESP32 support, STM32 support, and expected behavior.
- Add cross-end golden vector tests so ESP32-packed frames decode on STM32 and STM32-packed frames decode on ESP32.
- Decide and verify LED command payload alignment. ESP32 currently sends `SET_STATIC` and `SET_EFFECT` payloads, while STM32 currently implements `SET_RGB`, `BREATHE`, and `OFF` payloads.
- Keep ESP32 motion sequence messages as TODO until STM32 runtime implements and tests `SERVO_SEQUENCE`, `SERVO_SEQUENCE_BEGIN`, `SERVO_SEQUENCE_CHUNK`, and `SERVO_SEQUENCE_END`, or until unsupported NACK behavior is explicitly tested.
- Keep heartbeat, snapshot request/response, LED state, additional sensor events, and sensor health as forward-looking MCU Link IDs until both sides and tests confirm behavior.
- Add BLE and WebSocket shared command tests for `ctrl.servo.angle`, `ctrl.motion.jog`, `ctrl.motion.stop`, `ctrl.robot.state.set`, and `sys.ping`, including success, NACK, bad JSON, missing fields, and range validation.
- Add Discovery edge tests for malformed JSON, non-`ANNOUNCE` responses, missing `ip`, missing `port`, protocol version reporting, bind failure behavior, and URL generation.
- Add WSPK protocol tests for invalid magic, invalid type, payload length mismatch, zero-payload `LAST`, video `FIRST | KEYFRAME`, image `FIRST | LAST | KEYFRAME`, audio `LAST`, and fragmented frames.
- Document and test the SSCMA co-processor protocol before treating it as part of the public stable protocol: AT command formatting, `\r{...}\n` response framing, response/event/log dispatch, request matching, buffer limits, and SPI transport read/write/available behavior.
- Keep currently unrouted WebSocket names out of the stable body until source routes exist, including `ctrl.servo.pwm.unlock`, `ctrl.servo.pwm.lock`, `ctrl.servo.trajectory.play`, `ctrl.light.set`, and `ctrl.sound.play`.
- Capture hardware-in-the-loop smoke evidence for UART2 `921600 8N1` handshake, split/merged frames, CRC error recovery, queue overflow recovery, STM32 reset, and ESP32 reconnect.

## 6. Detailed Notes and Historical Records

The following files are kept as detailed notes or historical records. Use this document and the current code as the primary reference when they disagree:

- [S3 communication freeze baseline](esp32-s3/docs/COMM_PROTOCOL_FREEZE.md)
- [Camera media protocol](esp32-s3/docs/SERVER_GATEWAY_CAMERA_MEDIA_PROTOCOL.md)
- [BLE GATT protocol](esp32-s3/docs/BLE_GATT_PROTOCOL_BRIDGE.md)
- [STM32 v2 document entry](stm32-f103/Documents/STM32_v2文档入口.md)
- [Upstream protocol baseline](stm32-f103/Documents/上游协议基线.md)

<div align="center">

<p><strong>English</strong> | <a href="COMMUNICATION_PROTOCOLS_zh.md">简体中文</a></p>

</div>

# Communication Protocols

This is the public firmware protocol overview for WatcheRobot. It is intended as the first protocol document to read before opening the lower-level ESP32-S3 and STM32F103 implementation notes.

Code review date: 2026-06-22.

## 1. Link Overview

| Link | Purpose | Current code entry |
| --- | --- | --- |
| ESP32-S3 <-> STM32F103 | Board-internal co-processor link for servo, LED, touch, sensor, power control, and status | `esp32-s3/components/protocols/mcu_link/`, `stm32-f103/User/Protocol/`, `stm32-f103/User/Platform/stm32/platform_coproc_uart.c` |
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
| `0x03` | LED |
| `0x04` | SENSOR: touch, IMU, magnetometer |
| `0x05` | POWER: 5V power control |

Current handshake:

1. ESP32-S3 sends `SYS / HELLO_REQ(0x01)` with empty payload and `ACK_REQ`.
2. STM32F103 validates the frame and returns `SYS / ACK(0x04)`.
3. STM32F103 returns `SYS / HELLO_RSP(0x02)` with a `9` byte payload.

The shared core format and constants match on both sides: magic, protocol version, maximum payload, header length, CRC length, COBS delimiter, and class values. ESP32-S3 declares some forward-looking IDs, such as heartbeat, snapshot, and additional sensor events. STM32F103 does not currently implement all of them, so those ESP32-side IDs should not be treated as supported STM32 capabilities.

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
- After receiving `ANNOUNCE`, firmware parses `ip`, `port`, `version`, `protocol_version`, and `server`.

Current WebSocket behavior:

1. After successful Discovery, firmware builds `ws://<ip>:<port>`.
2. After WebSocket connects, ESP32-S3 first sends `sys.client.hello`.
3. Firmware treats the session as ready only after receiving `sys.ack` for `sys.client.hello`.
4. Text messages use a JSON envelope: `{"type":"...", "code":0, "data":{}}`.

Main downlink types currently routed:

```text
sys.ack, sys.nack, sys.ping, sys.pong, sys.session.resume
ctrl.servo.angle, ctrl.servo.pwm.unlock, ctrl.servo.pwm.lock
ctrl.servo.trajectory.play, ctrl.light.set
ctrl.motion.jog, ctrl.motion.stop
ctrl.microphone.open, ctrl.microphone.close
ctrl.robot.state.set, ctrl.sound.play
ctrl.camera.video_config, ctrl.camera.capture_image
ctrl.camera.start_video, ctrl.camera.stop_video
evt.asr.result, evt.ai.status, evt.ai.thinking, evt.ai.reply
xfer.ota.handshake, xfer.ota.checksum
```

Binary media frames use the `WSPK` header. The current header length is `14` bytes:

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
| Maximum characteristic value | `512` bytes |

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

## 5. Detailed Notes and Historical Records

The following files are kept as detailed notes or historical records. Use this document and the current code as the primary reference when they disagree:

- [S3 communication freeze baseline](esp32-s3/docs/COMM_PROTOCOL_FREEZE.md)
- [Camera media protocol](esp32-s3/docs/SERVER_GATEWAY_CAMERA_MEDIA_PROTOCOL.md)
- [BLE GATT protocol](esp32-s3/docs/BLE_GATT_PROTOCOL_BRIDGE.md)
- [STM32 v2 document entry](stm32-f103/Documents/STM32_v2文档入口.md)
- [Upstream protocol baseline](stm32-f103/Documents/上游协议基线.md)

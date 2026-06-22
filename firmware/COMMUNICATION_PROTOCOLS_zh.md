<div align="center">

<p><a href="COMMUNICATION_PROTOCOLS.md">English</a> | <strong>简体中文</strong></p>

</div>

# 通信协议总览

本文是 WatcheRobot 固件通信协议的公开总览。阅读 ESP32-S3 和 STM32F103 的底层实现说明前，建议先从这里理解主要链路和代码入口。

代码核对日期：2026-06-22。

## 1. 链路总览

| 链路 | 用途 | 当前代码入口 |
| --- | --- | --- |
| ESP32-S3 <-> STM32F103 | 板内协处理器链路，承载舵机、灯带、触摸、传感器、电源等控制与状态 | `esp32-s3/components/protocols/mcu_link/`、`stm32-f103/User/Protocol/`、`stm32-f103/User/Platform/stm32/platform_coproc_uart.c` |
| ESP32-S3 <-> Server | Wi-Fi 入网、UDP Discovery、WebSocket JSON 控制和 WSPK 二进制媒体帧 | `esp32-s3/components/utils/wifi_manager/`、`esp32-s3/components/protocols/discovery/`、`esp32-s3/components/protocols/ws_client/` |
| ESP32-S3 <-> App | BLE GATT 本地控制和 Wi-Fi 配网 | `esp32-s3/components/protocols/ble_service/` |

## 2. ESP32-S3 <-> STM32F103

这条链路是板内 UART 协议。ESP32-S3 侧默认使用 UART2，TX 为 GPIO19，RX 为 GPIO20，波特率为 `921600`。STM32F103 侧使用 `USART2@921600 8N1`，接收路径为 `RX DMA + IDLE`。

线上的一包数据是：

```text
COBS(raw_frame) + 0x00 delimiter
```

`raw_frame` 格式如下，整数均为 little-endian：

| 字段 | 长度 | 当前值或含义 |
| --- | --- | --- |
| `magic0` | 1 | `0xA5` |
| `magic1` | 1 | `0x5A` |
| `proto_version` | 1 | `0x01` |
| `msg_class` | 1 | 消息分类 |
| `msg_id` | 1 | 分类内消息号 |
| `flags` | 1 | `ACK_REQ=bit0`、`RESPONSE=bit1`、`FINAL=bit2` |
| `seq` | 4 | 发送端递增序号 |
| `payload_len` | 2 | 最大 `128` 字节 |
| `payload` | N | 业务负载 |
| `crc16` | 2 | CRC16-CCITT-FALSE，初值 `0xFFFF`，覆盖 header 和 payload |

消息分类：

| Class | 含义 |
| --- | --- |
| `0x01` | SYS，握手、ACK、NACK、FAULT |
| `0x02` | MOTION，舵机和运动 |
| `0x03` | LED，灯带 |
| `0x04` | SENSOR，触摸、IMU、磁力计 |
| `0x05` | POWER，5V 电源控制 |

当前握手流程：

1. ESP32-S3 发送 `SYS / HELLO_REQ(0x01)`，payload 为空，带 `ACK_REQ`。
2. STM32F103 校验后返回 `SYS / ACK(0x04)`。
3. STM32F103 返回 `SYS / HELLO_RSP(0x02)`，payload 长度为 `9`。

当前两端共同使用的核心格式和常量是一致的：magic、协议版本、最大 payload、header 长度、CRC 长度、COBS delimiter 和 class 值保持一致。ESP32-S3 侧声明了 heartbeat、snapshot、部分 sensor event 等扩展 ID，STM32F103 当前并未全部实现，不能把这些 ESP32-S3 侧预留 ID 当作 STM32 已支持能力。

详细代码：

- ESP32-S3 帧定义：[mcu_frame.h](esp32-s3/components/protocols/mcu_link/include/mcu_frame.h)
- ESP32-S3 UART 默认配置：[Kconfig](esp32-s3/components/protocols/mcu_link/Kconfig)
- STM32F103 帧定义：[coproc_protocol_types.h](stm32-f103/User/Protocol/coproc_protocol_types.h)
- STM32F103 编解码：[coproc_frame_codec.c](stm32-f103/User/Protocol/coproc_frame_codec.c)
- STM32F103 UART 接入：[platform_coproc_uart.c](stm32-f103/User/Platform/stm32/platform_coproc_uart.c)

## 3. ESP32-S3 <-> Server

Server 链路分三步：

```text
Wi-Fi STA 连接 -> UDP Discovery 找服务端 -> WebSocket 业务通信
```

Wi-Fi 当前行为：

- 模式：STA。
- 凭据保存：ESP-IDF Wi-Fi flash storage。
- 默认连接等待：`10000 ms`。
- BLE 已连接时，固件会暂停后台 Wi-Fi / WebSocket 链路；BLE 断开后，如果已有 Wi-Fi 凭据，会恢复后台连接。

UDP Discovery 当前行为：

- 端口：`37020`。
- 默认总超时：`30000 ms`。
- 每轮最多发送 `3` 次，轮间间隔约 `5000 ms`。
- 设备发送包含 `device_id` 和 `mac` 的 `DISCOVER`。
- 收到 `ANNOUNCE` 后解析 `ip`、`port`、`version`、`protocol_version`、`server`。

WebSocket 当前行为：

1. Discovery 成功后生成 `ws://<ip>:<port>`。
2. WebSocket 连接成功后，ESP32-S3 先发送 `sys.client.hello`。
3. 收到服务端对 `sys.client.hello` 的 `sys.ack` 后，固件才认为 session ready。
4. 文本消息统一使用 JSON envelope：`{"type":"...", "code":0, "data":{}}`。

当前路由支持的主要下行类型包括：

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

二进制媒体帧使用 `WSPK` 头，当前头长度是 `14` 字节：

| 偏移 | 长度 | 字段 | 含义 |
| --- | --- | --- | --- |
| `0` | 4 | `magic` | ASCII `"WSPK"` |
| `4` | 1 | `frame_type` | `1=audio`、`2=video`、`3=image`、`4=ota` |
| `5` | 1 | `flags` | `FIRST=bit0`、`LAST=bit1`、`KEYFRAME=bit2`、`FRAGMENT=bit3` |
| `6` | 4 | `seq` | 按 frame type 单独递增 |
| `10` | 4 | `payload_len` | payload 字节数 |

注意：当前 WSPK 头没有 `stream_id` 字段。

详细代码：

- Discovery：[discovery_client.h](esp32-s3/components/protocols/discovery/include/discovery_client.h)
- WebSocket 接口：[ws_client.h](esp32-s3/components/protocols/ws_client/include/ws_client.h)
- WebSocket 路由：[ws_router.c](esp32-s3/components/protocols/ws_client/src/ws_router.c)

## 4. ESP32-S3 <-> App / BLE

BLE 使用单服务、单特征值模型，兼容旧文本命令，也支持 JSON 命令。

| 项目 | 当前值 |
| --- | --- |
| Service UUID | `0x00FF` |
| Characteristic UUID | `0xFF01` |
| Characteristic property | `READ`、`WRITE`、`NOTIFY` |
| 最大 characteristic value | `512` 字节 |

JSON 模式判断规则：写入内容去掉前导空白后，如果第一个字符是 `{`，按 JSON 处理；否则按 legacy 文本命令处理。

JSON 模式当前支持：

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

JSON 响应：

- 成功：`sys.ack`
- 失败：`sys.nack`
- ping：`sys.pong`
- Wi-Fi 状态通知：`evt.wifi.status`

Legacy 文本命令当前支持：

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

详细代码：

- BLE 接口：[ble_service.h](esp32-s3/components/protocols/ble_service/include/ble_service.h)
- BLE 实现：[ble_service.c](esp32-s3/components/protocols/ble_service/src/ble_service.c)

## 5. 详细说明与历史记录

以下文档保留为详细说明或历史记录，阅读时以本文和当前代码为准：

- [S3 通讯协议冻结基线](esp32-s3/docs/COMM_PROTOCOL_FREEZE.md)
- [图片流与视频流通讯协议](esp32-s3/docs/SERVER_GATEWAY_CAMERA_MEDIA_PROTOCOL.md)
- [BLE GATT 协议文档](esp32-s3/docs/BLE_GATT_PROTOCOL_BRIDGE.md)
- [STM32 v2 文档入口](stm32-f103/Documents/STM32_v2文档入口.md)
- [上游协议基线](stm32-f103/Documents/上游协议基线.md)

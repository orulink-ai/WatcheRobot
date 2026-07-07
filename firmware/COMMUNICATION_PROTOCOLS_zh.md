<div align="center">

<p><a href="COMMUNICATION_PROTOCOLS.md">English</a> | <strong>简体中文</strong></p>

</div>

# 通信协议总览

本文是 WatcheRobot 固件通信协议的公开总览。阅读 ESP32-S3 和 STM32F103 的底层实现说明前，建议先从这里理解主要链路和代码入口。

代码核对日期：2026-06-22。

本版本发版边界：

- 本文正文只描述当前源码中已经能确认的行为。
- 仅在单侧实现、计划中、测试覆盖不足或语义仍不清晰的内容，不作为本次发版的稳定公开协议契约。
- 这些内容统一收敛到 [Protocol 通讯协议文档中的待办项](#5-protocol-通讯协议文档中的待办项)，确保先发版时不隐藏已知协议工作。

## 1. 链路总览

| 链路 | 用途 | 当前代码入口 |
| --- | --- | --- |
| ESP32-S3 <-> STM32F103 | 板内协处理器链路，承载舵机、触摸、传感器、电源、链路状态和预留 LED 通信 | `esp32-s3/components/protocols/mcu_link/`、`stm32-f103/User/Protocol/`、`stm32-f103/User/Platform/stm32/platform_coproc_uart.c` |
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
| `0x03` | LED，当前协议空间中预留该分类，命令 payload 对齐问题见待办项 |
| `0x04` | SENSOR，触摸、IMU、磁力计 |
| `0x05` | POWER，5V 电源控制 |

当前握手流程：

1. ESP32-S3 发送 `SYS / HELLO_REQ(0x01)`，payload 为空，带 `ACK_REQ`。
2. STM32F103 校验后返回 `SYS / ACK(0x04)`。
3. STM32F103 返回 `SYS / HELLO_RSP(0x02)`，payload 长度为 `9`。

当前源码中确认稳定的消息面：

| 方向 | Class / message | Payload 长度 | 说明 |
| --- | --- | --- | --- |
| ESP32-S3 -> STM32F103 | `SYS / HELLO_REQ(0x01)` | `0` | 发起 MCU Link 握手，要求 `ACK_REQ` |
| STM32F103 -> ESP32-S3 | `SYS / ACK(0x04)` | `6` | payload 包含被引用序号和状态 |
| STM32F103 -> ESP32-S3 | `SYS / NACK(0x05)` | `8` | payload 包含被引用序号、状态和原因 |
| STM32F103 -> ESP32-S3 | `SYS / FAULT(0x06)` | `9` | 上报链路或 runtime 故障 |
| STM32F103 -> ESP32-S3 | `SYS / HELLO_RSP(0x02)` | `9` | 上报角色、固件/硬件版本、能力位图、传感器位图、启动原因和默认 stream profile |
| ESP32-S3 -> STM32F103 | `MOTION / SERVO_MOVE(0x01)` | `9` | axis mask、X/Y 目标角度 deg x10、duration、profile、source |
| ESP32-S3 -> STM32F103 | `MOTION / SERVO_STOP(0x02)` | `2` | stop scope 和 source |
| ESP32-S3 -> STM32F103 | `MOTION / SERVO_JOG(0x05)` | `12` | axis mask、X/Y 速度 deg x10/s、timeout、source、轴限位 |
| STM32F103 -> ESP32-S3 | `MOTION / MOTION_DONE(0x03)` | `11` | 被引用序号、结果、最终 X/Y 角度 deg x10、执行耗时 |
| STM32F103 -> ESP32-S3 | `SENSOR / TOUCH_EVENT(0x01)` | `6` | touch ID、事件码、时间戳 |
| STM32F103 -> ESP32-S3 | `SENSOR / MAG_STATE(0x02)` | `6` | 航向、磁场强度、质量、状态位 |
| STM32F103 -> ESP32-S3 | `SENSOR / IMU_STATE(0x04)` | `11` | roll、pitch、yaw、加速度模长、陀螺仪模长、运动标志 |
| ESP32-S3 -> STM32F103 | `POWER / 5V_ENABLE(0x01)` | `1` | source tag |
| ESP32-S3 -> STM32F103 | `POWER / 5V_DISABLE(0x02)` | `1` | source tag |

当前两端共同使用的核心格式和常量是一致的：magic、协议版本、最大 payload、header 长度、CRC 长度、COBS delimiter 和 class 值保持一致。

ESP32-S3 侧声明了 heartbeat、snapshot、motion sequence、LED state、部分 sensor event 等扩展 ID。STM32F103 当前并未全部实现。它们本次不写入稳定 STM32 能力正文，统一放到 [Protocol 通讯协议文档中的待办项](#5-protocol-通讯协议文档中的待办项)。

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
- 收到 `ANNOUNCE` 后，固件要求存在 `ip`，若存在 `port` 则使用该端口，缺少 `port` 时 WebSocket 端口默认 `8765`。
- 固件解析可选的 `version`、`protocol_version`、`server` 字段用于报告。

WebSocket 当前行为：

1. Discovery 成功后生成 `ws://<ip>:<port>`。
2. WebSocket 连接成功后，ESP32-S3 先发送 `sys.client.hello`。
3. 收到服务端对 `sys.client.hello` 的 `sys.ack` 后，固件才认为 session ready。
4. 文本消息统一使用 JSON envelope：`{"type":"...", "code":0, "data":{}}`。

当前源码中已路由的下行类型：

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

二进制媒体帧在当前源码中使用 `WSPK` 头，当前头长度是 `14` 字节：

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
| 最大 characteristic value | `256` 字节 |

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

## 5. Protocol 通讯协议文档中的待办项

以下内容本次不提升为稳定发版协议正文。它们需要补测试、跨端确认或实现对齐后，再进入公开协议契约。主跟踪 issue 为 [#5](https://github.com/orulink-ai/WatcheRobot/issues/5)。

- 建立 MCU Link 跨端协议矩阵，覆盖 class、message ID、payload 长度、方向、ESP32 支持状态、STM32 支持状态和当前预期行为。
- 增加跨端 golden vector 测试，确保 ESP32 打包的帧能被 STM32 解码，STM32 打包的帧能被 ESP32 解码。
- 明确并验证 LED 命令 payload 对齐。ESP32 当前发送 `SET_STATIC` 和 `SET_EFFECT` payload，STM32 当前实现 `SET_RGB`、`BREATHE`、`OFF` payload。
- 在 STM32 runtime 实现并测试 `SERVO_SEQUENCE`、`SERVO_SEQUENCE_BEGIN`、`SERVO_SEQUENCE_CHUNK`、`SERVO_SEQUENCE_END` 前，motion sequence 消息保持待办；如果暂不实现，也需要测试其 unsupported NACK 行为。
- heartbeat、snapshot request/response、LED state、额外 sensor event、sensor health 等 MCU Link ID 保持前向预留，等待两端实现和测试确认。
- 增加 BLE 与 WebSocket 共享命令测试：`ctrl.servo.angle`、`ctrl.motion.jog`、`ctrl.motion.stop`、`ctrl.robot.state.set`、`sys.ping`，覆盖成功、NACK、坏 JSON、缺字段、范围校验。
- 增加 Discovery 边界测试：坏 JSON、非 `ANNOUNCE`、缺少 `ip`、缺少 `port`、protocol version 报告、bind 失败行为、URL 生成。
- 增加 WSPK 协议测试：invalid magic、invalid type、payload length mismatch、零 payload `LAST`、video `FIRST | KEYFRAME`、image `FIRST | LAST | KEYFRAME`、audio `LAST`、分片帧。
- 在 SSCMA 协处理器协议进入稳定公开协议前，补齐文档和测试：AT 命令格式、`\r{...}\n` 响应 framing、response/event/log 分发、request 匹配、buffer limit、SPI transport read/write/available 行为。
- 以下当前源码未路由的 WebSocket 名称不进入稳定正文，直到源码路由存在并有测试：`ctrl.servo.pwm.unlock`、`ctrl.servo.pwm.lock`、`ctrl.servo.trajectory.play`、`ctrl.light.set`、`ctrl.sound.play`。
- 补硬件台架 smoke 记录：UART2 `921600 8N1` 握手、拆包/粘包、CRC 错误恢复、队列溢出恢复、STM32 reset、ESP32 reconnect。

## 6. 详细说明与历史记录

以下文档保留为详细说明或历史记录，阅读时以本文和当前代码为准：

- [S3 通讯协议冻结基线](esp32-s3/docs/COMM_PROTOCOL_FREEZE.md)
- [图片流与视频流通讯协议](esp32-s3/docs/SERVER_GATEWAY_CAMERA_MEDIA_PROTOCOL.md)
- [BLE GATT 协议文档](esp32-s3/docs/BLE_GATT_PROTOCOL_BRIDGE.md)
- [STM32 v2 文档入口](stm32-f103/Documents/STM32_v2文档入口.md)
- [上游协议基线](stm32-f103/Documents/上游协议基线.md)

# TestHost

该目录现在承载 STM32 协处理器协议核心的 native host-side tests。

当前入口：

- `cmake --preset HostDebug`
- `cmake --build --preset HostDebug`
- `ctest --preset HostDebug --output-on-failure`

测试覆盖：

- `coproc_ring_buffer`
- `coproc_framing`
- `coproc_cobs`
- `coproc_crc16`
- `coproc_frame_codec`
- `coproc_tx_builder`
- `HELLO_REQ -> ACK -> HELLO_RSP` 最小闭环

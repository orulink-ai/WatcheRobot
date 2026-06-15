# WatcheRobot S3 Development Workflow

本文记录 `firmware/esp32-s3` 的通用开发和 PR 合并前检查规则。协议、动画、BLE、相机等专题设计仍以各自文档为准；本文只定义跨模块工程流程。

## PR Merge 前质量门禁

每个固件 PR 在 merge 前都需要完成一次鲁棒性 review。该 review 关注正常编译和格式检查不容易覆盖的问题，包括：

- 内存生命周期、越界、泄漏、PSRAM/DMA/cache 约束
- FreeRTOS task、ISR、queue、timer、event loop、锁和共享状态
- MCU link、BLE、WebSocket、camera gateway 等协议边界和异常输入
- HAL/driver 的 GPIO、I2C、SPI、UART、PWM、I2S、DMA 等硬件资源冲突
- service 层状态机、缓存语义、事件顺序、初始化顺序和降级恢复
- OTA、NVS、SPIFFS、SD、生成资产和版本兼容
- watchdog、错误恢复、日志可诊断性和现场定位能力

推荐使用 Codex skill `embedded-pr-robustness-review` 执行这一步。

唤醒词、WebSocket TTS、行为状态和音频/显示内存相关改动，还应同步检查
[`WAKE_WORD_TTS_MEMORY_NOTES.md`](WAKE_WORD_TTS_MEMORY_NOTES.md) 中的内存护栏和回归清单。

### Skill 触发条件

当用户请求对嵌入式固件 PR 做 merge 前代码质量或鲁棒性 review 时，应使用 `embedded-pr-robustness-review` skill。

典型触发场景包括：

- PR merge 前 review
- 嵌入式代码质量 review
- 鲁棒性、稳定性、并发、内存生命周期、硬件资源 review
- ESP-IDF / FreeRTOS / C / C++ 固件变更评审
- MCU link、wire protocol、协议解析、状态机变更评审
- HAL、driver、service 层变更评审
- WatcheRobot S3 固件相关 review，包括：
  - `components/protocols/mcu_link`
  - `components/protocols/*`
  - `components/services/*`
  - `components/hal/*`
  - `components/drivers/*`
  - `components/services/anim_service`
  - `components/hal/hal_display`
  - `main/app_main.c`
  - OTA、存储、LVGL、camera、audio、BLE、Wi-Fi、MCU 通信相关代码

该 skill 默认只做 review 和风险识别，不会修改代码、不会 flash 硬件、不会运行硬件脚本，除非用户明确要求执行。

### Review 输出要求

PR 鲁棒性 review 必须包含：

- `Blockers`：merge 前必须修复的问题
- `High Risk`：建议修复或明确接受风险的问题
- `Medium/Low`：可后续处理但需要记录的问题
- `Missing Verification`：当前 PR 应补充的构建、host test、硬件测试或日志证据
- `No Finding Areas`：已检查但未发现问题的关键风险域

每条 finding 必须说明文件/行号、问题、鲁棒性影响、建议修复和建议验证。

### 最低验证基线

- 通用固件改动：至少提供 `idf.py build` 结果。
- 无本地唤醒词变体改动：使用 `build-no-wake` 和
  `sdkconfig.defaults;sdkconfig.no-wake.defaults` 构建，并确认
  `srmodels.bin` 为 4 字节占位模型。
- `components/protocols/mcu_link` 改动：优先补充 host CMake/CTest 验证，并提供固件 build。
- HAL、pin/bus、display、camera、audio、SD、MCU link 硬件路径改动：除 build 外，还需要 flash/monitor boot log 或对应硬件 smoke 证据。
- camera/WebSocket gateway 改动：需要评估是否运行 `tools/ws_camera_gateway_test.py`。
- animation asset/runtime 改动：需要覆盖资产生成、SD/PSRAM 日志和 on-device display smoke。
- WebSocket/desktop expression 的 SFX 改动：需要验证 `evt.ai.status.sound_file`
  非空时能触发对应本地音效，同时未提供 `sound_file` 的云端状态仍不会抢占 TTS 音频链路。

缺少对应验证时，应在 review 的 `Missing Verification` 中明确列出，不能默认视为通过。

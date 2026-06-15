# IP5306 驱动 README

> **日期**: 2026-04-16
> **更新日期**: 2026-04-28
> **项目**: watcheRobot_STM32
> **硬件**: STM32F103C8T6 + IP5306 电源管理芯片

---

## 0. 当前实现说明

- 本文描述的是当前主线代码中 `IP5306_KEY` 的接线、控制逻辑与快速测试方法。
- 当前实现已经将 `PB12` 配置为 `IP5306_KEY` 控制脚，但该引脚当前不再直接连接 `KEY`。
- 当前硬件拓扑改为：`PB12 -> 外部 NMOS Gate -> NMOS 导通后把 KEY 拉到 GND`。
- `PA1` 已配置为 `IP5306_IRQ` 输入脚，用于读取芯片状态变化。
- 当前板级电源边界为：ESP32 由 USB-C 5V 独立供电；IP5306 控制的是 STM32 侧给舵机和 WS2812 LED 使用的 5V 外设电源，不控制 ESP32 自身供电。
- 当前 Debug 固件在 `POWER_5V_ENABLE` / `ip5306_on` 后会延时触发一次本地舵机观测动作，方便肉眼确认舵机 5V rail 上电后的行为；该动作是调试验证探针，不应混同为正式 App 固件的默认业务动作。
- 当前业务入口位于本目录下的 `ip5306_key.c`、`ip5306_irq.c` 与 `User/App/cli.c`，不在 `main.c` 中直接编写测试流程。

---

## 1. 硬件连接

### 1.1 当前接线

| STM32 引脚 | 连接对象 | 功能 | 备注 |
|------------|----------|------|------|
| PB12 | 外部 NMOS Gate | 控制 KEY 下拉开关 | 高电平导通 |
| PA1 | IP5306 IRQ | 状态输入 | 当前为上升沿中断配置 |

### 1.2 当前电源边界

当前调试板上 ESP32 由 USB-C 5V 直接供电，因此执行 `ip5306_off`、`ip5306_double` 或协议侧 `POWER_5V_DISABLE` 后，ESP32 日志串口持续输出是正常现象，不应作为失败判据。

IP5306 这一路只控制 STM32 侧外设 5V rail，当前目标负载包括：

- 舵机 5V
- WS2812 LED 5V

因此验证开关机或 POWER 协议时，应测量 IP5306 输出端、舵机 5V 或 LED 5V，而不是观察 ESP32 是否掉电。

### 1.3 KEY 引脚逻辑

- IP5306 的 `KEY` 正常由机械按键触发。
- 机械按键的一端接 `KEY`，另一端接 `GND`。
- 按键被按下时，本质上就是把 `KEY` 短暂拉低到地。
- 当前 PCB 上通过外部 `NMOS` 来完成这个动作：
  - `PB12 = LOW` 时，NMOS 截止，`KEY` 断开，等价“释放”
  - `PB12 = HIGH` 时，NMOS 导通，`KEY` 被拉到地，等价“按下”

### 1.4 IRQ 引脚逻辑

- `IP5306_IRQ` 用于对外提示芯片状态变化。
- 当前 CubeMX 中已将 `PA1` 配置为 `GPIO_MODE_IT_RISING`，并带内部下拉。
- 当前软件已经接入 `EXTI1` 中断处理链路，并在 IRQ 模块内记录：
  - 待处理事件标志
  - 累计上升沿次数
  - 最近一次触发时间戳

### 1.5 当前 CubeMX 配置

| 参数 | 当前配置 | 说明 |
|------|----------|------|
| KEY GPIO 引脚 | `PB12` | 外部 NMOS Gate 控制 |
| KEY 模式 | `GPIO_MODE_OUTPUT_PP` | 推挽输出，直接驱动 MOS 栅极 |
| KEY 初始电平 | `GPIO_PIN_RESET` | 默认让 NMOS 截止，避免上电误按键 |
| IRQ GPIO 引脚 | `PA1` | `IP5306_IRQ` 输入 |
| IRQ 模式 | `GPIO_MODE_IT_RISING` | 上升沿事件 |
| IRQ 上拉/下拉 | `GPIO_PULLDOWN` | 默认稳定在低电平 |

当前这组配置是合理的，原因如下：

- `PB12` 现在控制的是 MOS 栅极，而不是直接接 `KEY`，因此推挽输出更合适。
- 默认低电平可以让 MOS 截止，避免 STM32 上电时误触发 `KEY`。
- `PA1` 使用上升沿中断输入，便于后续把 IP5306 的状态变化接入中断逻辑。

---

## 2. IP5306 开关机时序

根据 IP5306 datasheet 的按键控制说明，`KEY` 的典型控制逻辑可以按下面理解：

| 操作 | KEY 动作 | 当前测试实现 | 预期效果 |
|------|----------|--------------|----------|
| 短按 | KEY 拉低 `30ms ~ 2s` | `PB12` 拉高 `100ms` | 开机、唤醒或重新打开输出 |
| 长按 | KEY 拉低 `> 2s` | `PB12` 拉高 `2500ms` | 开启或关闭照明 LED |
| 双击短按 | 1 秒内连续两次短按 | `PB12` 拉高 `100ms` 两次，中间间隔 `200ms` | 关闭升压输出、电量显示和照明 LED |
| 长时间保持 | KEY 持续拉低 | `PB12` 拉高 `5000ms` | 仅用于万用表验证 GPIO 与 NMOS 通路 |

当前代码采用：

- `100ms` 作为短按测试值
- `2500ms` 作为长按照明控制测试值
- `200ms` 作为双击短按之间的间隔测试值
- `5000ms` 作为万用表验证用保持时间

这样做的原因是：

- `100ms` 明显落在短按时序范围内，既容易触发，又不会误判成长按。
- `2500ms` 明显超过 `2s`，便于稳定触发长按动作。
- `200ms` 明显落在 `1s` 双击窗口内，便于稳定触发关闭升压输出。
- `5000ms` 不用于正常控制，只用于让万用表能稳定观察到低电平。

---

## 3. 软件实现

### 3.1 关键文件

| 文件 | 作用 |
|------|------|
| `Core/Src/gpio.c` | `PB12/PA1` 的 GPIO 初始化 |
| `Core/Src/stm32f1xx_it.c` | `EXTI1_IRQHandler()` 中断入口 |
| `Core/Inc/main.h` | `IP5306_KEY/IP5306_IRQ` 引脚宏定义 |
| `User/Board/board_pins.h` | 板级 `IP5306_KEY/IP5306_IRQ` 引脚映射 |
| `User/Config/app_config.h` | 短按、长按、测试保持时间常量 |
| `User/Device/ip5306/ip5306_irq.c` | IRQ 引脚电平读取与上升沿事件记录 |
| `User/Device/ip5306/ip5306_key.c` | KEY 按键模拟逻辑 |
| `User/App/cli.c` | 串口测试命令入口 |

### 3.2 当前接口

| 接口 | 作用 |
|------|------|
| `IP5306_Irq_Init()` | 清空上电后的 IRQ 软件状态 |
| `IP5306_Irq_Read()` | 读取 `PA1` 当前电平 |
| `IP5306_Irq_OnRisingEdge()` | 在 EXTI 回调中记录一次 IRQ 上升沿 |
| `IP5306_Irq_HasPendingEvent()` | 查询是否有未清除的 IRQ 标志 |
| `IP5306_Irq_GetEventCount()` | 查询累计上升沿次数 |
| `IP5306_Irq_GetLastEventTickMs()` | 查询最近一次触发时间 |
| `IP5306_Irq_ClearPendingEvent()` | 清除软件侧 IRQ 待处理标志 |
| `IP5306_Key_Init()` | 初始化为“未按下”状态 |
| `IP5306_Key_PressMs(ms)` | 拉高栅极指定时长后释放 |
| `IP5306_Key_ShortPress()` | 模拟短按 |
| `IP5306_Key_LongPress()` | 模拟长按 |
| `IP5306_Key_DoubleShortPress()` | 模拟双击短按 |
| `IP5306_Key_TestHold()` | 长时间保持导通，方便万用表观察 |
| `App_SetPower5V(enabled, sourceTag)` | 协议侧 POWER 5V 入口，enable 映射短按，disable 映射双击短按 |

### 3.3 Debug 固件上电舵机观测动作

当前 Debug 固件为了便于肉眼确认 IP5306 打开的是 STM32 侧舵机 / WS2812 LED 的 5V rail，在 POWER enable 后增加了一个本地观测 probe：

1. `POWER_5V_ENABLE` 或 `ip5306_on` 进入 `App_SetPower5V(1, ...)`
2. STM32 先执行一次 `IP5306_Key_ShortPress()`
3. 延时 `APP_POWER5V_SERVO_PROBE_DELAY_MS`，当前为 `600ms`
4. STM32 本地设置：
   - `Servo1 -> APP_POWER5V_SERVO1_PROBE_ANGLE`，当前为 `70 deg`
   - `Servo2 -> APP_POWER5V_SERVO2_PROBE_ANGLE`，当前为 `110 deg`
5. `USART1` 打印 `POWER5V SERVO PROBE` 结果

示例日志：

```text
========== POWER5V SERVO PROBE ==========
  Delay  : 600 ms after POWER enable
  Servo1 : OK angle=70 deg pulse=1277 us
  Servo2 : OK angle=110 deg pulse=1722 us
=========================================
```

注意：

- 这个 probe 只用于当前 Debug 固件的板级验证，目的是让测试人员能看到上电后舵机确实有一次本地动作。
- 它不改变 IP5306 的按键时序本身，也不表示正式 App 固件必须在每次 POWER enable 后自动动舵机。
- 如果后续要切正式 App 固件，需要关闭 `APP_POWER5V_SERVO_PROBE_ENABLE` 或移除此观测动作。

### 3.4 当前初始化流程

```c
// main() 先初始化 GPIO
MX_GPIO_Init();

// 进入应用层初始化
App_Init();

// App_Init() 中先清空 IRQ 软件状态
IP5306_Irq_Init();

// App_Init() 中让 NMOS 保持截止，引脚处于未按下状态
IP5306_Key_Init();
```

### 3.5 IRQ 中断链路

```c
PA1 rising edge
    -> EXTI1_IRQHandler()
    -> HAL_GPIO_EXTI_IRQHandler(IP5306_IRQ_Pin)
    -> HAL_GPIO_EXTI_Callback(GPIO_Pin)
    -> IP5306_Irq_OnRisingEdge()
```

当前回调会记录：

- `Pending Event`
- `Event Count`
- `Last Tick`

---

## 4. 串口测试命令

### 4.1 命令列表

| 命令 | 动作 | 用途 |
|------|------|------|
| `ip5306_irq` | 读取 IRQ 电平与事件状态 | 查看 IRQ 是否真正触发过 |
| `ip5306_irq_clear` | 清除待处理 IRQ 标志 | 为下一次测试重新归零 |
| `ip5306_on` | 拉高 PB12 100ms，并在 Debug 固件中执行一次舵机 probe | 通过 NMOS 模拟短按开机/唤醒，并肉眼观察上电后负载行为 |
| `ip5306_off` | 拉高 PB12 100ms 两次 | `ip5306_double` 的别名，方便直接做关断测试 |
| `ip5306_double` | 拉高 PB12 100ms 两次 | 通过 NMOS 模拟双击短按关闭升压输出 |
| `ip5306_long` | 拉高 PB12 2500ms | 通过 NMOS 模拟长按 |
| `ip5306_hold` | 拉高 PB12 5000ms | 便于万用表验证栅极与 KEY 通路 |

### 4.2 使用方法

1. 使用 `USART1` 连接串口工具
2. 波特率设置为 `115200, 8N1`
3. 输入命令后按回车执行

### 4.3 推荐测试顺序

1. 先执行 `ip5306_hold`
2. 用万用表测 `PB12` 栅极或 `KEY` 网络对地电压
3. 确认 MCU 确实可以让 NMOS 导通，并把 `KEY` 拉到接近 `0V`
4. 再执行 `ip5306_on`
5. 观察 IP5306 输出端是否被打开
6. 最后执行 `ip5306_off`
7. 观察 IP5306 输出端是否被关闭
8. 如有需要，执行 `ip5306_irq` 观察 `PA1` 当前状态
9. 执行 `ip5306_irq_clear` 清空标志后，再做下一轮测试

注意：当前 ESP32 不由 IP5306 输出供电，所以 `ip5306_off` 后 ESP32 串口继续打印日志是符合预期的。该测试的外部判据是舵机/LED 5V rail 是否变化。

---

## 5. 万用表测试建议

### 5.1 测 PB12 栅极 / KEY 网络

目的：

- 验证 CubeMX 的 GPIO 模式和当前代码是否真的在“驱动 NMOS 并拉低 KEY”

方法：

1. 黑表笔接系统地
2. 红表笔接 `PB12` 或直接接 `IP5306 KEY`
3. 上电空闲时记录电压
4. 执行 `ip5306_hold`
5. 观察 `PB12` 是否持续升到接近 `3.3V`
6. 如测 `KEY` 网络，则观察其是否被拉到接近 `0V`

说明：

- `PB12` 当前是推挽输出控制 MOS 栅极，因此空闲时应接近 `0V`。
- 执行 `ip5306_hold` 时，`PB12` 应稳定升高到高电平。
- 如果测的是 `KEY` 网络，则应看到 MOS 导通后 `KEY` 被拉向地。

### 5.2 测 IP5306 输出端

目的：

- 验证 `KEY` 的控制是否真的触发了 IP5306 的开关机动作

方法：

1. 黑表笔接地
2. 红表笔接 IP5306 的输出端
3. 执行 `ip5306_on`
4. 观察输出是否上升
5. 执行 `ip5306_off`
6. 观察输出是否下降

### 5.3 测 IRQ 引脚

目的：

- 验证 `PA1` 与 `IP5306_IRQ` 之间的连线和电平变化是否正常

方法：

1. 黑表笔接地，红表笔接 `PA1`
2. 记录空闲时电压
3. 先执行 `ip5306_irq_clear`
4. 结合 `ip5306_on`、`ip5306_off` 或芯片状态变化，执行 `ip5306_irq`
5. 观察串口返回的：
   - `PA1 State`
   - `Pending Event`
   - `Event Count`
   - `Last Tick`

说明：

- 当前代码已接入 EXTI 回调，并能记录 IRQ 上升沿事件。
- 这一轮仍以“验证引脚、电平和触发方向”为主，尚未把 IRQ 进一步接成完整业务状态机。

---

## 6. 注意事项

### 6.1 万用表不适合测短脉冲本身

- `100ms` 的短按脉冲对万用表来说通常太快，不容易直接看到波形变化。
- 如果你的目标是验证“GPIO 能不能让 MOS 导通并拉低 KEY”，优先使用 `ip5306_hold`。

### 6.2 充电输入会影响“关机”判断

- 如果 IP5306 同时接着充电输入，输出行为可能不会完全等同于“纯电池供电”场景。
- 这时你可能会看到长按后输出没有像预期那样完全掉电。
- 因此做关机验证时，建议尽量明确当前是否接入外部充电电源。

### 6.3 ESP32 USB-C 供电不会被 IP5306 关闭

- 当前 ESP32 的 5V 来自 USB-C，不经过 IP5306 控制的外设 5V rail。
- 因此 POWER 协议和 `ip5306_*` CLI 命令不会关闭 ESP32 本体。
- 双端日志测试时，ESP32 在 `ip5306_off` 后继续输出 `MCU_OBS`、WiFi 或内存日志，说明 ESP32 自身供电仍在，不代表 IP5306 控制失败。
- 若要验证 IP5306 控制效果，请测舵机/LED 5V rail 或对应负载状态。

### 6.4 这份实现优先验证“时序正确”，不是完整电源状态机

- 当前实现的目标是快速验证硬件引脚、电平逻辑和基本开关机能力。
- 这一轮没有引入更复杂的“电源状态机”或自动重试逻辑。
- 当前已经把 `IP5306_IRQ` 的 EXTI 回调接入了软件记录链路，但还没有继续扩展成完整业务逻辑。
- 如果后续要把 IP5306 接入正式业务流程，再考虑补：
  - 状态检测
  - 防重复触发
  - IRQ 回调与事件分发
  - 更完整的上电/关机策略

---

## 7. 当前结论

- `PB12` 当前改为推挽输出、默认低电平，是“GPIO 控 NMOS 栅极”方案下比较合适的配置。
- `KEY` 控制逻辑本质仍然是“把 KEY 拉低模拟按键”，只是这一步现在由外部 NMOS 完成。
- `PA1` 现在不仅具备 `IP5306_IRQ` 的引脚配置，也已经完成了 EXTI1 到软件事件记录的链路。
- 当前最实用的验证流程是：
  - `ip5306_hold` 验证 GPIO 高电平导通能力
  - `ip5306_on` 验证短按开机
  - `ip5306_off` / `ip5306_double` 验证双击短按关闭升压输出
  - `ip5306_long` 验证长按动作
  - `ip5306_irq` / `ip5306_irq_clear` 验证 IRQ 触发链路

---

## 8. 参考资料

- [IP5306 Datasheet（镜像 PDF）](https://done.land/assets/files/ip5306_datasheet.pdf)
- [IP5306 资料整理（done.land）](https://done.land/components/power/powersupplies/battery/chargers/charge-discharge/ip5306/)

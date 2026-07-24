# WatcheRobot 内置 App 开发指南

## 1. 目的与边界

本文面向需要为 WatcheRobot ESP32-S3 固件开发**内置 App**的协作者。内置 App 是随 Launcher 固件一起构建和发布的 C 模块，例如 `Desktop Link`、`Phone Control`、`Agent`。

它适合需要 LVGL UI、BLE、Wi-Fi、WebSocket、灯光、舵机、音频或复杂业务逻辑的功能。

本文不覆盖 App.Center 的 JSON `manifest-app`。它只能驱动现有的文案、状态和 SD 动画，不能运行新的 C 业务代码。独立 OTA 固件应用的交付协议见 [app_center_app_pack_contract.md](app_center_app_pack_contract.md)。

## 2. 框架概览

```text
Launcher 图标 / 事件
        |
        v
watcher_app_open("example.app")
        |
        +-- 关闭当前 App: on_close()
        +-- 释放当前 App 资源
        +-- 按 resource_mode 配置 BLE / Wi-Fi / 配网
        +-- 首次创建: on_create()
        +-- 进入页面: on_open()
        |
主循环 ----> on_tick()
按键/触摸 --> on_button() / on_touch()
退出 ------> on_close()
```

App Runtime 位于 `main/watcher_app_runtime.c`，负责 App 注册、切换、生命周期调用和资源切换。App 不应自行接管全局 BLE/Wi-Fi 的开关流程；应通过 `resource_mode` 声明它需要的基础通信资源。

## 3. 一个 App 的组成

每个 App 都是一个 `watcher_app_t`：

```c
static const watcher_app_t s_example_app = {
    .id = "example.app",             // 稳定且全局唯一
    .name = "Example",
    .icon = "example",
    .theme_color = 0x2D7FF9,
    .resource_mode = WATCHER_APP_RESOURCE_WIFI_ONLY,
    .on_create = example_app_on_create,
    .on_open = example_app_on_open,
    .on_tick = example_app_on_tick,
    .on_close = example_app_on_close,
    .on_button = example_app_on_button,
    .on_touch = example_app_on_touch,
};
```

回调职责如下：

| 回调 | 何时调用 | 应做什么 |
| --- | --- | --- |
| `on_create` | 第一次打开 | 创建可复用的静态 UI 或初始化不会反复创建的状态。 |
| `on_open` | 每次进入 | 刷新 UI、订阅事件、启动本 App 会话。 |
| `on_tick` | 主循环 | 仅执行轻量状态刷新；不能阻塞、轮询网络或进行长耗时 IO。 |
| `on_close` | 切换/退出 | 取消订阅、停止 App 专属任务、释放 UI 与临时资源。 |
| `on_button` | 实体按键 | 处理返回、确认等按键。 |
| `on_touch` | 触摸事件 | 将坐标转换为 App 内操作。 |

资源模式：

| 值 | 使用场景 |
| --- | --- |
| `WATCHER_APP_RESOURCE_OFF` | 纯本地 UI、动画展示。 |
| `WATCHER_APP_RESOURCE_BLE_ONLY` | 手机蓝牙控制。 |
| `WATCHER_APP_RESOURCE_WIFI_ONLY` | Desktop Link、云端、WebSocket、HTTP。 |
| `WATCHER_APP_RESOURCE_PROVISIONING` | Wi-Fi 配网。 |

## 4. 推荐代码组织

当前仓库没有统一的 `apps/` 目录；`Desktop Link` 使用 `main/client_app.c`，部分本地 App 仍在 `main/app_main.c`。新功能建议采用下列目录，避免继续扩大 `app_main.c`：

```text
firmware/s3/main/apps/example_app/
  example_app.h          # 对外只暴露 configure/get_app 等最小接口
  example_app.c          # 生命周期、状态机、业务编排
  example_app_ui.c       # LVGL 页面和触摸控件
  example_app_ui.h
  example_app_test.c     # 可脱离硬件的状态机/协议单测（如适用）
```

`app_main.c` 只负责：

1. 配置依赖回调或服务句柄。
2. 调用 `watcher_app_register(example_app_get_app())`。
3. 把 App 添加到 Launcher 的图标入口（`s_launcher_entries` 及对应首页 UI 数据）。

新源文件需要加入 `main/CMakeLists.txt` 的 `SRCS`。

## 5. 开发步骤

1. **定义 App 合约**：写明 App ID、入口图标、资源模式、用户流程、要使用的硬件、需要的协议事件和退出行为。
2. **确认容量**：当前 App Runtime 固定最多注册 12 个 App；默认 Launcher 构建已经注册约 11 个。增加 App 前必须先核对数量；计划增加多个 App 时，应先改造注册表容量与 Launcher 分页能力。
3. **实现状态与 UI**：业务状态放在 App 模块，LVGL 对象放在 UI 模块。避免在按键回调、触摸回调或 `on_tick` 中执行阻塞操作。
4. **接入服务**：优先调用已有的动画、灯光、音频、MCU Link、BLE 或 WebSocket 服务。不要复制驱动代码，也不要让 UI 直接维护底层连接状态。
5. **接入 Launcher**：注册 App 后，补充 Launcher 图标、焦点图和点击路由；只注册不添加入口，用户无法从首页打开。
6. **处理退出**：`on_close` 必须停止 App 自己创建的任务、timer、订阅和会话，并清除临时 UI。重复进入、退出、再进入不能残留旧状态。

## 6. Codex 协作方式

可以让协作者使用 Codex 开发，但需要先给 Codex 一份完整的 App 需求，而不是只说“做一个 App”。建议每个 App 使用单独分支和独立 PR。

可直接使用下面的任务描述：

```text
在 WatcheRobot_esp32 的 dev 分支开发一个内置 App。

App 名称：<名称>
App ID：<name.app>
入口：Launcher 首页第 <位置> 个图标，图标资源为 <资源名或待补资源>
资源模式：<OFF / BLE_ONLY / WIFI_ONLY / PROVISIONING>
用户流程：<进入后看到什么、如何操作、如何退出>
硬件能力：<灯光 / 舵机 / SD 动画 / 音频 / 无>
通信：<无 / BLE 命令 / WebSocket 消息，附协议字段>
失败表现：<未连接、资源缺失、超时等提示与返回路径>

要求：
1. 将业务、UI、底层服务调用解耦；不要把新业务堆入 app_main.c。
2. 复用现有服务和 watcher_app_runtime，不直接重写全局 BLE/Wi-Fi 生命周期。
3. 为可脱离硬件的状态机、解析或协议逻辑补充测试。
4. 执行固件构建；若改动硬件、通信或 UI，提供烧录/串口或实机验证结果。
5. 提交前说明改动文件、风险、验证结果和未覆盖项。
```

Codex 可以完成代码定位、实现、构建、烧录、读取串口、提交分支和创建 PR；协作者仍需对产品交互、硬件接线、安全边界和真实设备验收负责。

## 7. 最低验收清单

- App 能从 Launcher 打开，并显示正确入口和焦点状态。
- 首次打开、退出、重复打开三次以上均正常。
- 切换到其他 App 后，BLE/Wi-Fi/配网资源状态符合 `resource_mode`。
- 无网络、BLE 断开、SD 资源缺失等失败路径不会卡死或重启循环。
- `on_tick` 不造成 UI 卡顿、看门狗风险或重复创建资源。
- 涉及协议时，异常输入、重复命令和断线重连有可预期结果。
- 至少通过 `idf.py build`；涉及硬件、显示或通信时，还应提供实机验证日志。

## 8. 参考实现

- `main/client_app.c`：Wi-Fi / Desktop Link App 的依赖注入、打开、轮询、关闭结构。
- `main/watcher_app_runtime.h`：App 接口和资源模式定义。
- `main/watcher_app_runtime.c`：注册、切换和生命周期顺序。
- `main/app_main.c`：App 注册和 Launcher 入口。

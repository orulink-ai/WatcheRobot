<div align="center">

<p><a href="README.md">English</a> | <strong>简体中文</strong></p>

<img src="docs/images/watcher-robot-render.png" alt="WatcheRobot 渲染图" width="720">

<p>WatcheRobot 桌面机器人开源资料包，包含嵌入式固件、硬件生产文件、机械模型、SD 卡行为资源和发布工具。</p>

<p>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPL--3.0-blue.svg" alt="License: GPL-3.0"></a>
  <img src="https://img.shields.io/badge/ESP--IDF-v5.2.1-green" alt="ESP-IDF v5.2.1">
  <img src="https://img.shields.io/badge/Hardware-Gerber%20%7C%20BOM%20%7C%20CPL-orange" alt="Hardware: Gerber, BOM, CPL">
</p>

</div>

---

## 项目概览

WatcheRobot 是一个桌面机器人项目，当前公开资料围绕 ESP32-S3 主控、STM32F103 协处理器、自研 PCB 模块、SD 卡行为资源和整机机械装配资料组织。

这个仓库面向活动参赛者和开源开发者，目标是让没有历史上下文的人可以 Clone/Fork 后快速理解目录、准备工具链、刷写固件或生成行为资源、验证第一个动作，并通过 GitHub 提交改动。

产品 App、服务端运行包、桌面端安装包在可用时通过 GitHub Releases 分发，源码不包含在本仓库中。

## 10 分钟快速开始

参赛者最短路径：

1. Clone 或 Fork 仓库。
2. 安装当前平台所需的基础工具。
3. 优先下载 Release 资产；若尚未发布，则使用本地构建作为 fallback。
4. 刷写 ESP32-S3，并准备 SD 卡行为资源。
5. 上电，按 smoke test 验证第一个动作。
6. 如需提交改动，创建规范分支并打开 PR。

### 1. Clone

```bash
git clone https://github.com/orulink-ai/WatcheRobot.git
cd WatcheRobot
```

如果你准备提交 PR，请先 Fork。分支命名和 PR 规则见 [CONTRIBUTING_zh.md](CONTRIBUTING_zh.md)。

### 2. 安装工具

| 平台 | 最小工具 |
| --- | --- |
| Windows | Git、Python 3.11+、开发板对应 USB 串口驱动、本地构建时需要 ESP-IDF v5.2.1 |
| macOS | Git、Python 3.11+、必要时安装 USB 串口驱动、本地构建时需要 ESP-IDF v5.2.1 |
| Linux | Git、Python 3.11+、`dialout` 或等价串口权限、本地构建时需要 ESP-IDF v5.2.1 |

串口驱动、端口识别、ESP32/STM32 刷写和平台差异见 [docs/flashing.md](docs/flashing.md)。

### 3. 获取固件和行为资源

活动现场优先使用 GitHub Release 资产。当前首个公开活动 Release 尚未发布，因此文档保留本地构建和资源生成路径。

ESP32-S3 本地构建 fallback：

```bash
cd firmware/esp32-s3
idf.py set-target esp32s3
idf.py build
```

STM32F103 主机侧测试 fallback：

```bash
cd firmware/stm32-f103
cmake --preset HostDebug
cmake --build --preset HostDebug
ctest --preset HostDebug
```

### 4. 刷写并准备 SD 卡

当 ESP32 release ZIP 可用时，Windows 可使用仓库内刷写工具：

```bash
python -m pip install -r tools/win_flasher/requirements.txt
python -m tools.win_flasher flash --zip .\WatcheRobot-S3-V2.3.0-esp32s3.zip --port COM7
```

macOS/Linux 或本地 ESP-IDF 构建场景，可在 `firmware/esp32-s3` 下使用 `idf.py flash monitor`。

SD 卡行为资源流程见 [docs/sd-card-assets.md](docs/sd-card-assets.md)。活动现场可直接使用 [docs/behavior-flash-skill.md](docs/behavior-flash-skill.md) 作为操作 checklist。

### 5. 验证第一个动作

按 [docs/action-test.md](docs/action-test.md) 检查：

- ESP32 启动日志和屏幕启动
- SD 卡 `anim/` 行为资源
- BLE 或 WebSocket 命令入口
- 一个舵机动作
- 一个灯效动作
- 硬件可用时验证一次触摸事件

## 目录结构

```text
firmware/
  README.md                       固件构建和目录入口
  COMMUNICATION_PROTOCOLS.md      公开固件协议总览
  esp32-s3/                       ESP32-S3 固件源码、资源和 ESP-IDF 工程
  stm32-f103/                     STM32F103 协处理器固件、协议代码和主机侧测试

hardware/
  README.md       硬件资料地图和 BOM 说明
  pcb/            PCB 源文件、原理图、Layout、Gerber、BOM、CPL 和备用件模板
  3d-models/      机械模型导出文件
  assembly/       后续装配图片或装配文档

docs/
  flashing.md             固件刷写和工具链说明
  sd-card-assets.md       SD 卡行为资源说明
  behavior-flash-skill.md 活动现场行为资源操作 checklist
  action-test.md          第一个动作 smoke test
  sdk.md                  公开 SDK 和协议边界
  versions.md             版本来源和追踪规则
  downloads.md            Release 资产说明
  compatibility.md        版本兼容矩阵
  release-process.md      Release 流程和资产规则
  governance.md           仓库边界说明

tools/
  esp32-flasher/  ESP32 Release 刷写命令参考
  win_flasher/    Windows ESP32 Release ZIP 刷写工具
```

## 参赛者文档入口

- [刷写说明](docs/flashing.md)
- [SD 卡行为资源](docs/sd-card-assets.md)
- [行为资源现场 checklist](docs/behavior-flash-skill.md)
- [第一个动作 smoke test](docs/action-test.md)
- [SDK 和协议边界](docs/sdk.md)
- [版本追踪](docs/versions.md)
- [贡献指南](CONTRIBUTING_zh.md)
- [安全策略](SECURITY.md)

## 开源范围

本仓库公开：

- 嵌入式固件源码
- PCB 和机械结构公开资料
- 公开烧录与发布工具
- 公开 Release 文档

以下内容在可用时作为 Release 资产分发，但源码不在本仓库开源：

- Android 应用安装包
- 服务端运行包
- 桌面端安装包
- 预编译固件

Release 资产类型和当前发布状态见 [docs/downloads.md](docs/downloads.md)。

## 当前文档缺口

- GitHub Releases 尚未发布，因此部分 Quick Start 路径使用本地构建 fallback。
- 跨端协议契约测试缺口见 [GitHub issue #5](https://github.com/orulink-ai/WatcheRobot/issues/5)。
- 当前 BOM 已包含型号、规格、供应商、供应商料号和可替代料字段；可购买链接和活动现场备用件数量见 [hardware/pcb/spares.md](hardware/pcb/spares.md) 继续补齐。

## 技术栈

| 领域 | 主要技术 |
| --- | --- |
| ESP32-S3 固件 | ESP-IDF v5.2.1, FreeRTOS, LVGL |
| STM32F103 固件 | STM32 HAL, CMake, 主机侧 C 测试 |
| 硬件 | EasyEDA Pro, Gerber, BOM, CPL, STEP |
| 发布工具 | Python, Windows 烧录工具 |

## 许可证

本仓库使用 [GPL-3.0](LICENSE) 许可证。若子项目或第三方组件包含独立许可证声明，以其自身声明为准。

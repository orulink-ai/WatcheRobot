<div align="center">

<p><a href="README.md">English</a> | <strong>简体中文</strong></p>

<img src="docs/images/watcher-robot-render.png" alt="WatcheRobot 渲染图" width="720">

<p>WatcheRobot 桌面机器人开源资料包，包含嵌入式固件、硬件生产文件、机械模型和发布工具。</p>

<p>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPL--3.0-blue.svg" alt="License: GPL-3.0"></a>
  <img src="https://img.shields.io/badge/ESP--IDF-v5.2.1-green" alt="ESP-IDF v5.2.1">
  <img src="https://img.shields.io/badge/Hardware-Gerber%20%7C%20BOM%20%7C%20CPL-orange" alt="Hardware: Gerber, BOM, CPL">
</p>

</div>

---

## 项目概览

WatcheRobot 是一个桌面机器人项目，当前公开资料围绕 ESP32-S3 主控、STM32F103 协处理器、自研 PCB 模块和整机机械装配资料组织。

这个仓库的定位是 WatcheRobot 的公开复现资料包。它提供硬件查看与打样、嵌入式固件构建、Release 资产准备和板级验证所需的公开材料。产品 App、服务端运行包、桌面端安装包在可用时通过 GitHub Releases 分发，源码不包含在本仓库中。

当前仓库重点包含：

- ESP32-S3 固件源码和资源文件
- STM32F103 协处理器固件源码和主机侧测试
- PCB 生产资料：原理图 PDF、Layout PDF、Gerber、BOM、CPL 和 EasyEDA Pro 源工程
- 整机 STEP 装配模型
- Release 协作文档和烧录工具

## 目录结构

```text
firmware/
  esp32-s3/       ESP32-S3 固件源码、资源和 ESP-IDF 工程
  stm32-f103/     STM32F103 协处理器固件、协议代码和主机侧测试

hardware/
  pcb/            PCB 源文件、原理图、Layout、Gerber、BOM 和 CPL
  3d-models/      机械模型导出文件
  assembly/       后续装配图片或装配文档

docs/
  downloads.md        Release 资产说明
  compatibility.md    版本兼容矩阵
  release-process.md  Release 流程和资产规则
  governance.md       仓库边界说明

tools/
  esp32-flasher/  ESP32 Release 烧录说明
  win_flasher/    Windows ESP32 Release ZIP 烧录工具
```

## 快速开始

### ESP32-S3 固件

使用 ESP-IDF v5.2.1：

```bash
cd firmware/esp32-s3
idf.py set-target esp32s3
idf.py build
```

### STM32F103 主机侧测试

```bash
cd firmware/stm32-f103
cmake --preset HostDebug
cmake --build --preset HostDebug
ctest --preset HostDebug
```

### 硬件资料

PCB 生产资料位于 `hardware/pcb/`：

- `schematic/`：原理图 PDF
- `layout/`：PCB Layout PDF
- `gerber/`：已解压的 Gerber 和钻孔文件
- `bom/`：每块板的 BOM 文件和中文 BOM 模板
- `cpl/`：贴片坐标 / Pick-and-place 文件
- `pcb-source/`：可编辑 EasyEDA Pro 源工程

整机机械装配 STEP 位于 `hardware/3d-models/exports/`。

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

Release 资产类型见 [docs/downloads.md](docs/downloads.md)。

## 技术栈

| 领域 | 主要技术 |
| --- | --- |
| ESP32-S3 固件 | ESP-IDF, FreeRTOS, LVGL |
| STM32F103 固件 | STM32 HAL, CMake, 主机侧 C 测试 |
| 硬件 | EasyEDA Pro, Gerber, BOM, CPL, STEP |
| 发布工具 | Python, Windows 烧录工具 |

## 许可证

本仓库使用 [GPL-3.0](LICENSE) 许可证。若子项目或第三方组件包含独立许可证声明，以其自身声明为准。

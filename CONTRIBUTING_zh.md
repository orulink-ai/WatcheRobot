# 贡献指南

感谢你一起完善 WatcheRobot。这个仓库是 Public，活动参赛者可以直接 Fork、测试改动并提交 Pull Request，不需要额外申请仓库权限。

## Fork 和分支

1. Fork `orulink-ai/WatcheRobot`。
2. Clone 你自己的 fork。
3. 创建一个聚焦的工作分支。

推荐分支命名：

| 工作类型 | 分支格式 |
| --- | --- |
| 活动队伍开发 | `hackathon-2026/<team-name>` |
| 文档 | `docs/<topic>` |
| 固件 | `firmware/<topic>` |
| 硬件资料 | `hardware/<topic>` |
| Release 文档或清单 | `release/<topic>` |

一个 PR 尽量只解决一个明确问题。

## PR 标题

标题使用简短前缀：

- `docs: ...`
- `firmware: ...`
- `hardware: ...`
- `release: ...`
- `tools: ...`

示例：

- `docs: add SD-card behavior asset checklist`
- `hardware: document spare parts for wireless charging base`
- `firmware: update ESP32 behavior state notes`

## 不要提交的内容

不要提交：

- Release 产物：`.bin`、`.zip`、`.exe`、`.msi`、`.dmg`、`.apk`、`.aab`
- 本地构建输出或生成的固件镜像
- Wi-Fi 凭据、API key、token、私钥或 `.env` 文件
- 本地机器路径、私人串口记录、只属于现场台架的 COM 口日志
- 闭源 App、Server、Desktop 包的源码

二进制发布物应该上传到 GitHub Releases，不进入 Git 历史。

## 文档更新要求

如果你的改动影响安装、刷写、行为资源、硬件资料或公开协议，请同步更新对应文档：

- 快速开始：`README_zh.md`
- 刷写：`docs/flashing.md`
- SD 卡行为资源：`docs/sd-card-assets.md`
- 第一个动作 smoke test：`docs/action-test.md`
- SDK 和协议边界：`docs/sdk.md`
- 版本与兼容：`docs/versions.md`、`docs/compatibility.md`
- 硬件与 BOM：`hardware/README.md`、`hardware/pcb/spares.md`

## 提交 PR 前的验证

按改动范围运行检查：

- 仅文档改动：检查链接；可行时运行仓库 policy 扫描。
- ESP32 固件改动：使用 ESP-IDF v5.2.1 构建。
- STM32 固件改动：运行 `cmake --preset HostDebug`、`cmake --build --preset HostDebug`、`ctest --preset HostDebug`。
- 刷写或行为资源改动：完成 `docs/action-test.md` 中的 checklist。

如果你的环境缺少某个工具，请在 PR 的 Test Plan 中明确说明。

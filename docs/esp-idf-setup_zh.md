# ESP-IDF v5.2.1 安装指引

本文面向活动参赛者，目标是在 Windows 或 macOS 上安装并激活 ESP-IDF v5.2.1，用于构建 WatcheRobot ESP32-S3 固件。

官方参考：

- [ESP-IDF v5.2.1 Windows Setup](https://docs.espressif.com/projects/esp-idf/en/v5.2.1/esp32s3/get-started/windows-setup.html)
- [ESP-IDF v5.2.1 Linux/macOS Setup](https://docs.espressif.com/projects/esp-idf/en/v5.2.1/esp32s3/get-started/linux-macos-setup.html)

本仓库固定使用：

| 项目 | 要求 |
| --- | --- |
| ESP-IDF | `v5.2.1` |
| 目标芯片 | `esp32s3` |
| Python | `3.11+` |
| 构建目录 | `firmware/esp32-s3` |
| 支持路径 | ESP-IDF 命令行；Arduino IDE 不是本仓库支持的构建路径 |

## Windows 安装

推荐使用 Espressif 官方 ESP-IDF Tools Installer。不要手动混装多个 Python、CMake、Ninja 或 GCC 工具链，除非你已经熟悉 ESP-IDF 环境隔离方式。

### 1. 准备基础环境

1. 安装 Git for Windows。
2. 安装 Python 3.11 或更新版本。
3. 安装开发板或 USB-UART 转接器对应的 USB 串口驱动。
4. 准备一个 ASCII 路径作为 ESP-IDF 安装目录，例如：

```text
C:\Espressif
```

避免安装到中文路径、带空格的路径、OneDrive 同步目录或需要管理员权限才能写入的目录。

### 2. 安装 ESP-IDF v5.2.1

1. 打开官方 Windows 安装说明中的 ESP-IDF Tools Installer 下载入口。
2. 启动安装器，选择 ESP-IDF `v5.2.1`。
3. 目标芯片选择 `ESP32-S3` 或确认安装器包含 `esp32s3` 工具链。
4. 安装目录使用 `C:\Espressif` 或同类 ASCII 路径。
5. 等待安装器下载 Python 虚拟环境、CMake、Ninja、OpenOCD 和 Xtensa 工具链。

安装完成后，开始菜单通常会出现 `ESP-IDF 5.2 CMD` 或类似入口。

### 3. 激活环境并验证

每次构建前，都从 `ESP-IDF 5.2 CMD` 打开终端，或使用已经激活 ESP-IDF 环境的 shell。

在该终端中执行：

```bat
idf.py --version
python --version
where idf.py
```

期望结果：

- `idf.py --version` 显示 ESP-IDF `v5.2.1`。
- `python --version` 显示 Python `3.11` 或更新版本。
- `where idf.py` 指向 `C:\Espressif` 下的 ESP-IDF 环境，而不是其他旧版本安装目录。

### 4. 构建 WatcheRobot ESP32-S3 固件

在 `ESP-IDF 5.2 CMD` 中进入仓库：

```bat
cd C:\path\to\WatcheRobot\firmware\esp32-s3
idf.py set-target esp32s3
idf.py build
```

如果需要刷写，先在设备管理器中确认串口号，例如 `COM7`，然后执行：

```bat
idf.py -p COM7 flash monitor
```

退出串口监视器通常使用 `Ctrl+]`。

## macOS 安装

Intel Mac 和 Apple Silicon Mac 都使用命令行安装路径。下面示例使用 Homebrew；如果你使用 MacPorts，请参考官方 ESP-IDF v5.2.1 macOS 文档替换依赖安装命令。

### 1. 安装 Command Line Tools

```bash
xcode-select --install
```

如果系统提示已经安装，可以继续下一步。

### 2. 安装 Homebrew 依赖

```bash
brew install cmake ninja dfu-util ccache python@3.11 git
```

确认 Python 和 Git 可用：

```bash
python3 --version
git --version
```

### 3. 下载 ESP-IDF v5.2.1

```bash
mkdir -p ~/esp
git clone -b v5.2.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.2.1
cd ~/esp/esp-idf-v5.2.1
```

如果网络中断导致子模块不完整，可在该目录下重新执行：

```bash
git submodule update --init --recursive
```

### 4. 安装 ESP32-S3 工具链

```bash
./install.sh esp32s3
```

该命令会安装 ESP32-S3 所需的 Python 依赖、CMake/Ninja 集成和 Xtensa 工具链。

### 5. 激活环境并验证

每打开一个新的终端，都需要先激活 ESP-IDF 环境：

```bash
cd ~/esp/esp-idf-v5.2.1
. ./export.sh
```

验证当前 shell：

```bash
idf.py --version
python --version
which idf.py
```

期望结果：

- `idf.py --version` 显示 ESP-IDF `v5.2.1`。
- `python --version` 显示 Python `3.11` 或 ESP-IDF 虚拟环境中的兼容版本。
- `which idf.py` 指向 `~/esp/esp-idf-v5.2.1`。

### 6. 构建 WatcheRobot ESP32-S3 固件

先激活 ESP-IDF 环境，再进入仓库：

```bash
cd ~/esp/esp-idf-v5.2.1
. ./export.sh

cd /path/to/WatcheRobot/firmware/esp32-s3
idf.py set-target esp32s3
idf.py build
```

查看串口：

```bash
ls /dev/cu.*
```

常见端口名包括 `/dev/cu.usbserial-*` 和 `/dev/cu.usbmodem*`。刷写示例：

```bash
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

退出串口监视器通常使用 `Ctrl+]`。

## 常见问题

| 现象 | 处理方式 |
| --- | --- |
| `idf.py` 找不到 | 当前终端没有激活 ESP-IDF 环境。Windows 使用 `ESP-IDF 5.2 CMD`；macOS 先执行 `. ./export.sh`。 |
| `idf.py --version` 不是 `v5.2.1` | PATH 指向了其他 ESP-IDF 安装目录。关闭终端，重新进入 v5.2.1 环境。 |
| 提示缺少 `esp32s3` 工具链 | Windows 安装器没有包含 ESP32-S3，或 macOS 没有执行 `./install.sh esp32s3`。 |
| 串口找不到 | 换一根支持数据传输的 USB 线；Windows 查看设备管理器；macOS 执行 `ls /dev/cu.*`。 |
| 串口权限或占用错误 | 关闭其他串口工具；确认没有另一个 `idf.py monitor`、串口助手或 IDE 正在占用端口。 |
| 构建目录错误 | 确认当前目录是 `firmware/esp32-s3`，再执行 `idf.py set-target esp32s3` 和 `idf.py build`。 |
| 构建缓存异常 | 在 `firmware/esp32-s3` 下执行 `idf.py fullclean` 后重新 `idf.py set-target esp32s3` 和 `idf.py build`。 |
| 想用 Arduino IDE | 本仓库不支持 Arduino IDE 构建 WatcheRobot ESP32-S3 固件，请使用 ESP-IDF v5.2.1。 |

## 下一步

ESP-IDF 安装和本地构建通过后，继续按 [固件刷写说明](flashing.md) 刷写固件，并按 [SD 卡行为资源说明](sd-card-assets.md) 准备行为资源。

# 快速开始指南

**最后更新**: 2026-05-05

---

## 环境准备

### 1. 安装 ESP-IDF

本项目基于 ESP-IDF v5.4+，请按照官方文档安装：

**Windows**:

```bash
# 下载 ESP-IDF 安装器
https://dl.espressif.com/dl/esp-idf/

# 或使用命令行安装
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.4
./install.bat esp32,esp32s3,esp32c3
```

**Linux/macOS**:

```bash
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.4
./install.sh esp32,esp32s3,esp32c3
```

### 2. 设置环境变量

**Windows**:

```bash
%USERPROFILE%\esp\esp-idf\export.bat
```

**Linux/macOS**:

```bash
. $HOME/esp/esp-idf/export.sh
```

---

## 编译和烧录

### 方法一：使用 idf.py（推荐新手）

#### 1. 克隆项目

```bash
git clone https://github.com/your-repo/xiaozhi-esp32.git
cd xiaozhi-esp32
```

#### 2. 设置目标芯片

```bash
# 对于 ESP32-S3
idf.py set-target esp32s3

# 对于 ESP32-C3
idf.py set-target esp32c3

# 对于 ESP32
idf.py set-target esp32
```

#### 3. 配置开发板

```bash
idf.py menuconfig
```

导航到：`Xiaozhi Assistant` → `Board Type`，选择你的开发板。

#### 4. 编译

```bash
idf.py build
```

#### 5. 烧录

```bash
# 烧录固件
idf.py flash

# 烧录并监控日志
idf.py flash monitor
```

### 方法二：使用 release.py 脚本（推荐）

如果你的开发板目录下有 `config.json` 文件，可以使用此脚本：

```bash
python scripts/release.py bread-compact-wifi-s3cam
```

此脚本会自动：

- 设置目标芯片
- 应用编译选项
- 完成编译并打包固件

---

## 首次配置

### 1. WiFi 配置

首次启动时，设备会进入 WiFi 配置模式：

1. 设备创建热点：`xiaozhi-XXXXXX`
2. 使用手机连接该热点
3. 浏览器打开 `http://192.168.4.1`
4. 输入 WiFi 名称和密码
5. 点击"连接"

### 2. 设备激活

WiFi 连接成功后，设备会自动连接到服务器进行激活。

---

## 常用命令

### 编译相关

```bash
# 完整编译
idf.py build

# 清理编译
idf.py clean

# 完全清理（包括配置）
idf.py fullclean

# 仅编译指定组件
idf.py build --component=main
```

### 烧录相关

```bash
# 烧录固件
idf.py flash

# 烧录并监控
idf.py flash monitor

# 仅监控日志
idf.py monitor

# 擦除 Flash
idf.py erase-flash
```

### 监控相关

```bash
# 监控日志
idf.py monitor

# 监控日志并过滤
idf.py monitor | grep "TAG"

# 退出监控
Ctrl + ]
```

### 配置相关

```bash
# 打开配置菜单
idf.py menuconfig

# 保存配置
idf.py save-defconfig

# 查看配置差异
idf.py diffconfig
```

---

## 目录结构

```
xiaozhi-esp32/
├── main/                      # 主应用源码
│   ├── main.cc               # 入口点
│   ├── application.cc/h      # 应用主控
│   ├── audio/                # 音频子系统
│   ├── display/              # 显示子系统
│   ├── protocols/            # 通信协议
│   ├── video/                # 视频流服务
│   ├── monitor/              # 监控模式
│   ├── lock_control/         # 锁控模块
│   ├── local_preview/        # 本地预览
│   └── boards/               # 开发板硬件抽象
├── managed_components/       # ESP-IDF 组件（自动管理）
├── partitions/               # 分区表
├── docs/                     # 文档
└── scripts/                  # 构建工具脚本
```

---

## 开发板选择

### 当前支持的开发板

项目支持 70+ 种 ESP32 系列开发板，主要包括：

- **M5Stack 系列**: M5Stack CoreS3, M5Stack AtomS3, etc.
- **ESP-BOX 系列**: ESP32-S3-BOX, ESP32-S3-BOX-3, etc.
- **立创开发板**: lichuang-s3, etc.
- **自定义开发板**: bread-compact-wifi-s3cam (当前使用)

### 查看所有支持的开发板

```bash
ls main/boards/
```

### 添加新开发板

参考 [自定义开发板指南](custom-board.md)

---

## 调试技巧

### 1. 查看日志级别

```bash
# 在 menuconfig 中设置
Component config → Log output → Default log verbosity
```

可选级别：

- None (0)
- Error (1)
- Warning (2)
- Info (3)
- Debug (4)
- Verbose (5)

### 2. 过滤日志

```bash
# 只显示特定 TAG 的日志
idf.py monitor | grep "Application"

# 排除特定 TAG 的日志
idf.py monitor | grep -v "wifi"
```

### 3. 查看内存使用

```cpp
// 在代码中添加
ESP_LOGI(TAG, "Free heap: %d", esp_get_free_heap_size());
ESP_LOGI(TAG, "Free PSRAM: %d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

### 4. 查看任务统计

```bash
# 在监控模式下按 Ctrl+T, H
idf.py monitor
# 然后按 Ctrl+T
# 再按 H
```

### 5. 使用 GDB 调试

```bash
# 启动 OpenOCD
openocd -f board/esp32s3-builtin.cfg

# 在另一个终端启动 GDB
xtensa-esp32s3-elf-gdb build/xiaozhi-esp32.elf
(gdb) target remote :3333
(gdb) monitor reset halt
(gdb) break app_main
(gdb) continue
```

---

## 常见问题

### 1. 编译错误：找不到 ESP-IDF

**解决方案**:

```bash
# 确保已设置环境变量
. $HOME/esp/esp-idf/export.sh  # Linux/macOS
%USERPROFILE%\esp\esp-idf\export.bat  # Windows
```

### 2. 烧录失败：无法连接到设备

**解决方案**:

- 检查 USB 线缆是否正常
- 检查串口驱动是否安装
- 尝试按住 BOOT 按钮再烧录
- 检查串口号是否正确：`idf.py -p COM3 flash`

### 3. WiFi 连接失败

**解决方案**:

- 检查 WiFi 名称和密码是否正确
- 确保 WiFi 是 2.4GHz（不支持 5GHz）
- 检查路由器是否开启了 MAC 地址过滤
- 尝试重启设备

### 4. 设备无法激活

**解决方案**:

- 检查网络连接是否正常
- 检查服务器地址是否正确
- 查看日志确认错误信息
- 联系管理员检查服务器状态

### 5. 摄像头无法工作

**解决方案**:

- 检查摄像头连接是否正常
- 检查引脚配置是否正确
- 查看日志确认错误信息
- 尝试降低分辨率或帧率

---

## 下一步

- 阅读 [系统架构文档](../architecture/system-overview.md)
- 查看 [通信协议](../protocols/esp32-server-v5.2.md)
- 了解 [功能模块](../features/)
- 参考 [快速参考](../reference/quick-reference.md)

---

## 获取帮助

- 查看 [快速参考](../reference/quick-reference.md) 的故障排查部分
- 查看 [变更日志](../reference/CHANGELOG.md) 确认最新状态
- 查看代码注释和文档

---

**最后更新**: 2026-05-05

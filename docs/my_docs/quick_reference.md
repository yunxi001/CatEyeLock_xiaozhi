# 智能猫眼门禁系统 - 快速参考

## 编译和烧录

```bash
idf.py set-target esp32s3   # 配置目标芯片
idf.py build                # 编译
idf.py flash                # 烧录
idf.py monitor              # 监控日志
idf.py fullclean && idf.py build  # 清理重编译
```

---

## 服务器命令

### 监控模式

```json
// 启动监控模式
{"type": "system", "command": "start_monitor"}

// 停止监控模式
{"type": "system", "command": "stop_monitor"}
```

### 锁控命令

```json
// 远程开锁
{"type": "lock_control", "command": "unlock"}

// 设置临时密码
{"type": "lock_control", "command": "temp_code", "code": "123456"}

// 开启警报
{"type": "lock_control", "command": "alarm_on", "level": 1}

// 关闭警报
{"type": "lock_control", "command": "alarm_off"}
```

### 人脸识别结果

```json
// 识别成功且有权限
{"type": "face_recognition", "result": "known", "access": {"granted": true}}

// 识别成功但无权限
{"type": "face_recognition", "result": "known", "access": {"granted": false}}

// 未识别（陌生人）
{"type": "face_recognition", "result": "unknown", "access": {"granted": false}}

// 无人脸
{"type": "face_recognition", "result": "no_face"}
```

---

## UART 协议速查

### 协议格式（7字节）

```
[0xAA][CAT][TYPE][DATA0][DATA1][DATA2][CHECKSUM]
校验和 = (CAT + TYPE + DATA0 + DATA1 + DATA2) & 0xFF
```

### 消息类别

| CAT | 名称 | 方向 |
|-----|------|------|
| 0x01 | EVENT | STM32 → ESP32 |
| 0x02 | CONTROL | ESP32 → STM32 |
| 0x0F | ACK | 双向 |

### 常用事件（CAT=0x01）

| TYPE | 事件 | 处理 |
|------|------|------|
| 0x01 | 门铃按下 | 触发人脸识别 |
| 0x02 | 人体检测 | 触发人脸识别 |
| 0x03 | 暴力破坏 | 警报+上报 |

### 常用命令（CAT=0x02）

| TYPE | 命令 | 数据 |
|------|------|------|
| 0x01 | 开锁 | 无 |
| 0x02 | 开警报 | DATA0=级别 |
| 0x03 | 关警报 | 无 |
| 0x04 | 设密码 | BCD编码 |

### UART 配置

```
波特率: 9600, 数据位: 8, 停止位: 1, 校验: 无
TX: GPIO_NUM_3, RX: GPIO_NUM_14
```

---

## API 速查

### Application

```cpp
bool StartMonitorMode();      // 启动监控模式
void StopMonitorMode();       // 停止监控模式
bool IsMonitorMode() const;   // 检查监控模式
```

### LockControlService

```cpp
bool SendUnlock();                              // 开锁
bool SendAlarm(uint8_t level);                  // 开警报
bool SendAlarmOff();                            // 关警报
bool SendTempCode(const char* password);        // 设临时密码
void SetEventCallback(EventCallback callback);  // 设事件回调
```

### Esp32Camera

```cpp
bool CaptureJpeg(uint8_t** data, size_t* size, int quality = 80);
bool IsAvailable() const;
uint16_t GetFrameWidth() const;
uint16_t GetFrameHeight() const;
```

---

## BinaryProtocol2 协议

### 协议结构

```c
struct BinaryProtocol2 {
    uint16_t version;      // = 2
    uint16_t type;         // 消息类型
    uint32_t reserved;     // 扩展字段
    uint32_t timestamp;    // 毫秒
    uint32_t payload_size; // 字节
    uint8_t payload[];
};
```

### 消息类型区分

| type | reserved | 数据类型 |
|------|----------|----------|
| 0 | 0 | 音频（OPUS） |
| 0 | 非0 | 监控视频流（JPEG） |
| 2 | 非0 | 人脸识别图像（JPEG） |

**视频 reserved 编码：** `(width << 16) | height`

---

## 性能指标

| 指标 | 目标值 |
|------|--------|
| 视频分辨率 | 640x480 |
| 视频帧率 | 10 fps |
| JPEG 质量 | 60-80 |
| 视频带宽 | ~1.2 Mbps |
| 端到端延迟 | <500ms |

---

## 故障排查

### 编译失败

```bash
idf.py fullclean
idf.py build
```

### 摄像头问题

```bash
idf.py monitor | grep "Esp32Camera"
```

### 视频流问题

```bash
idf.py monitor | grep "VideoStreamService"
```

### UART 通信问题

```bash
idf.py monitor | grep "LockControl"
```

---

## 日志标签

| 标签 | 模块 |
|------|------|
| `Application` | 应用主控 |
| `LockControl` | 锁控服务 |
| `LockProtocol` | UART 协议 |
| `VideoStreamService` | 视频流 |
| `MonitorService` | 监控服务 |
| `Esp32Camera` | 摄像头 |

---

## 文档索引

| 文档 | 说明 |
|------|------|
| `face_recognition_handover.md` | 主交接文档 |
| `server_protocol.md` | 服务器端协议规范 |
| `.kiro/specs/face-recognition/` | 规范文档（权威） |

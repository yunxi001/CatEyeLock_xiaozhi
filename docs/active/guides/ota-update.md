# OTA 升级指南

**最后更新**: 2026-05-05

---

## 概述

OTA (Over-The-Air) 升级允许通过网络远程更新设备固件，无需物理连接。

---

## OTA 流程

```
Device                    Server
  │                         │
  │ 1. 检查更新              │
  │────────────────────────>│
  │                         │
  │ 2. 返回固件信息          │
  │<────────────────────────│
  │                         │
  │ 3. 下载固件              │
  │────────────────────────>│
  │                         │
  │ 4. 验证固件              │
  │                         │
  │ 5. 写入 OTA 分区         │
  │                         │
  │ 6. 设置启动分区          │
  │                         │
  │ 7. 重启设备              │
  │                         │
  │ 8. 从新分区启动          │
```

---

## 触发 OTA 升级

### 1. 自动检查更新

设备启动时自动检查更新：

```cpp
// 在 Application::Initialize() 中
if (CheckForUpdate()) {
    StartOtaUpdate();
}
```

### 2. 手动触发更新

通过服务器命令触发：

```json
{
  "type": "system",
  "command": "check_update"
}
```

---

## 固件版本管理

### 版本号格式

```
v<major>.<minor>.<patch>
例如：v1.2.3
```

### 版本比较

```cpp
bool IsNewerVersion(const char* current, const char* remote) {
    // 比较版本号
    // 返回 true 如果 remote 版本更新
}
```

---

## 分区表

OTA 升级需要特殊的分区表配置：

```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000
otadata,  data, ota,     0xf000,  0x2000
phy_init, data, phy,     0x11000, 0x1000
ota_0,    app,  ota_0,   0x20000, 0x3C0000
ota_1,    app,  ota_1,   0x3E0000,0x3C0000
storage,  data, fat,     0x7A0000,0x860000
```

---

## 安全性

### 1. 固件签名验证

```cpp
// 验证固件签名
if (!VerifyFirmwareSignature(firmware_data, signature)) {
    ESP_LOGE(TAG, "固件签名验证失败");
    return false;
}
```

### 2. HTTPS 下载

```cpp
// 使用 HTTPS 下载固件
esp_http_client_config_t config = {
    .url = firmware_url,
    .cert_pem = server_cert_pem_start,
};
```

---

## 错误处理

| 错误     | 处理方式     |
| -------- | ------------ |
| 下载失败 | 重试 3 次    |
| 验证失败 | 中止升级     |
| 写入失败 | 回滚到旧版本 |
| 启动失败 | 自动回滚     |

---

## 回滚机制

如果新固件启动失败，系统会自动回滚到旧版本：

```cpp
// 在 app_main() 中
esp_ota_img_states_t ota_state;
if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        // 标记为有效
        esp_ota_mark_app_valid_cancel_rollback();
    }
}
```

---

## 调试技巧

### 1. 查看 OTA 日志

```bash
idf.py monitor | grep "OTA"
```

### 2. 查看分区信息

```bash
idf.py partition-table
```

### 3. 手动触发 OTA

```cpp
// 在代码中添加
if (button_pressed) {
    StartOtaUpdate();
}
```

---

## 相关文档

- [ESP32固件更新指南](../../deprecated/analysis/ESP32固件更新指南-霍尔传感器移除.md)

---

**最后更新**: 2026-05-05

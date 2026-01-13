# ESP32 锁控事件处理修改需求

> 版本：v1.0  
> 创建日期：2026-01-13  
> 基于：`docs/my_docs/我的修改建议.md`

---

## 1. 修改背景

### 1.1 设计原则

- **ESP32 屏幕**：仅用于显示摄像头画面，不显示其他状态信息
- **ESP32 扬声器**：用于语音播报，不播放警报音效
- **STM32 端**：负责处理蜂鸣器警报

### 1.2 影响范围

| 文件 | 修改内容 |
|------|----------|
| `main/application.cc` | 修改 `HandleLockReportMessage()` 中的事件处理逻辑 |
| `main/protocols/websocket_protocol.cc` | 新增 `SendPasswordReport()` 方法 |
| `main/protocols/websocket_protocol.h` | 新增方法声明 |
| `docs/my_docs/智能猫眼门锁系统-服务器与ESP32通信协议规范-v5.0.md` | 新增密码上报消息定义 |

---

## 2. 详细修改需求

### 2.1 EVT_TAMPER (0x03) - 撬锁报警

**当前行为**：
```cpp
case static_cast<uint8_t>(xiaozhi::EventId::EVT_TAMPER):
    event_name = "tamper";
    ESP_LOGI(TAG, "撬锁报警 (级别 %d)", param);
    HandleTamperAlert(param);  // 蜂鸣器报警 + 显示警告
    break;
```

**修改后行为**：
```cpp
case static_cast<uint8_t>(xiaozhi::EventId::EVT_TAMPER):
    event_name = "tamper";
    ESP_LOGI(TAG, "撬锁报警 (级别 %d)", param);
    // 不调用 HandleTamperAlert()，仅上报服务器
    // STM32 端负责蜂鸣器警报
    break;
```

**修改说明**：
- ❌ 移除：`HandleTamperAlert()` 调用（蜂鸣器报警 + 显示警告）
- ✅ 保留：上报服务器 `event_report: tamper`
- 📝 原因：警报由 STM32 端处理，ESP32 屏幕仅显示摄像头画面


---

### 2.2 EVT_DOOR_OPEN (0x04) - 门未关超时

**当前行为**：
```cpp
case static_cast<uint8_t>(xiaozhi::EventId::EVT_DOOR_OPEN):
    event_name = "door_open";
    ESP_LOGI(TAG, "门未关超时 (%d 分钟)", param);
    HandleDoorNotClosed();  // 显示提示
    break;
```

**修改后行为**：
```cpp
case static_cast<uint8_t>(xiaozhi::EventId::EVT_DOOR_OPEN):
    event_name = "door_open";
    ESP_LOGI(TAG, "门未关超时 (%d 分钟)", param);
    // 播放语音提示（不显示警告）
    PlaySound(Lang::Sounds::OGG_DOOR_NOT_CLOSED);  // 需要添加此音效
    break;
```

**修改说明**：
- ❌ 移除：`HandleDoorNotClosed()` 调用（显示提示）
- ✅ 保留：上报服务器 `event_report: door_open`
- ✅ 新增：语音播报功能

**新增音效资源**：

| 常量名 | 文件名 | 内容 | 语言 |
|--------|--------|------|------|
| `Lang::Sounds::OGG_DOOR_NOT_CLOSED` | `door_not_closed.ogg` | "门未关好，请检查" | zh-CN |

**音效制作要求**：
```bash
# 格式要求
- 格式：OGG 容器 + Opus 编码
- 采样率：16kHz
- 声道：单声道
- 时长：< 3 秒

# 转换命令
ffmpeg -i door_not_closed.wav -c:a libopus -b:a 32k -ar 16000 -ac 1 door_not_closed.ogg

# 放置路径
main/assets/locales/zh-CN/door_not_closed.ogg
```

---

### 2.3 EVT_LOW_BATTERY (0x05) - 低电量警告

**当前行为**：
```cpp
case static_cast<uint8_t>(xiaozhi::EventId::EVT_LOW_BATTERY):
    event_name = "low_battery";
    ESP_LOGW(TAG, "低电量警告: %d%%", param);
    Alert("", "电量低", "", "");  // 显示警告
    break;
```

**修改后行为**：
```cpp
case static_cast<uint8_t>(xiaozhi::EventId::EVT_LOW_BATTERY):
    event_name = "low_battery";
    ESP_LOGW(TAG, "低电量警告: %d%%", param);
    // 不显示警告，仅上报服务器
    break;
```

**修改说明**：
- ❌ 移除：`Alert()` 调用（显示警告）
- ✅ 保留：上报服务器 `event_report: low_battery`
- 📝 原因：ESP32 屏幕仅显示摄像头画面

---

### 2.4 RPT_PWD (0xC0) - 密码查询结果

**当前行为**：
```cpp
case static_cast<uint8_t>(xiaozhi::RptType::RPT_PWD): {
    uint32_t pwd = xiaozhi::LockProtocol::DecodePasswordHex(msg.data);
    ESP_LOGI(TAG, "当前密码: %06lu", (unsigned long)pwd);
    // 不上报服务器
    break;
}
```

**修改后行为**：
```cpp
case static_cast<uint8_t>(xiaozhi::RptType::RPT_PWD): {
    uint32_t pwd = xiaozhi::LockProtocol::DecodePasswordHex(msg.data);
    ESP_LOGI(TAG, "当前密码: %06lu", (unsigned long)pwd);
    
    // 新增：上报密码到服务器
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->SendPasswordReport(pwd);
    }
    break;
}
```

**修改说明**：
- ✅ 新增：上报密码到服务器
- 📝 用途：服务器端密码同步、App 显示当前密码

---

## 3. 新增功能

### 3.1 密码上报消息 (password_report)

**ESP32 → Server JSON 格式**：
```json
{
    "type": "password_report",
    "ts": 1702234567890,
    "data": {
        "password": "123456"
    }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| type | string | 固定为 `"password_report"` |
| ts | int64 | 时间戳（毫秒） |
| data.password | string | 6位数字密码 |

**WebsocketProtocol 新增方法**：

```cpp
// websocket_protocol.h
class WebsocketProtocol : public Protocol {
public:
    // ... 现有方法 ...
    
    /**
     * @brief 上报密码查询结果到服务器
     * @param password 6位数字密码
     */
    void SendPasswordReport(uint32_t password);
};

// websocket_protocol.cc
void WebsocketProtocol::SendPasswordReport(uint32_t password) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "password_report");
    cJSON_AddNumberToObject(root, "ts", esp_timer_get_time() / 1000);
    
    cJSON* data = cJSON_CreateObject();
    char pwd_str[8];
    snprintf(pwd_str, sizeof(pwd_str), "%06lu", (unsigned long)password);
    cJSON_AddStringToObject(data, "password", pwd_str);
    cJSON_AddItemToObject(root, "data", data);
    
    char* json_str = cJSON_PrintUnformatted(root);
    SendText(json_str);
    free(json_str);
    cJSON_Delete(root);
}
```


---

## 4. 代码修改清单

### 4.1 main/application.cc

**修改函数**：`HandleLockReportMessage()`

```cpp
void Application::HandleLockReportMessage(const xiaozhi::LockMessage& msg) {
    switch (msg.type) {
        case static_cast<uint8_t>(xiaozhi::RptType::RPT_EVENT): {
            uint8_t event_id = msg.data[0];
            uint8_t param = msg.data[1];
            
            std::string event_name;
            switch (event_id) {
                case static_cast<uint8_t>(xiaozhi::EventId::EVT_DOORBELL):
                    event_name = "bell";
                    ESP_LOGI(TAG, "门铃按下 - 触发人脸识别");
                    TriggerFaceRecognition();
                    break;
                    
                case static_cast<uint8_t>(xiaozhi::EventId::EVT_PIR):
                    event_name = "pir_trigger";
                    ESP_LOGI(TAG, "PIR 检测到人体 (持续 %d 秒) - 触发人脸识别", param);
                    TriggerFaceRecognition();
                    break;
                    
                // ========== 修改点 1: EVT_TAMPER ==========
                case static_cast<uint8_t>(xiaozhi::EventId::EVT_TAMPER):
                    event_name = "tamper";
                    ESP_LOGI(TAG, "撬锁报警 (级别 %d) - 仅上报服务器", param);
                    // 移除: HandleTamperAlert(param);
                    // STM32 端负责蜂鸣器警报
                    break;
                    
                // ========== 修改点 2: EVT_DOOR_OPEN ==========
                case static_cast<uint8_t>(xiaozhi::EventId::EVT_DOOR_OPEN):
                    event_name = "door_open";
                    ESP_LOGI(TAG, "门未关超时 (%d 分钟) - 语音播报", param);
                    // 移除: HandleDoorNotClosed();
                    // 新增: 语音播报
                    PlaySound(Lang::Sounds::OGG_DOOR_NOT_CLOSED);
                    break;
                    
                // ========== 修改点 3: EVT_LOW_BATTERY ==========
                case static_cast<uint8_t>(xiaozhi::EventId::EVT_LOW_BATTERY):
                    event_name = "low_battery";
                    ESP_LOGW(TAG, "低电量警告: %d%% - 仅上报服务器", param);
                    // 移除: Alert("", "电量低", "", "");
                    break;
                    
                default:
                    ESP_LOGW(TAG, "未知事件 ID: 0x%02X", event_id);
                    break;
            }
            
            // 上报事件到服务器（保持不变）
            if (!event_name.empty() && protocol_ && protocol_->IsAudioChannelOpened()) {
                protocol_->SendEventReport(event_name, param);
            }
            break;
        }
        
        // ... RPT_UNLOCK, RPT_ENV, RPT_STATE 保持不变 ...
        
        // ========== 修改点 4: RPT_PWD ==========
        case static_cast<uint8_t>(xiaozhi::RptType::RPT_PWD): {
            uint32_t pwd = xiaozhi::LockProtocol::DecodePasswordHex(msg.data);
            ESP_LOGI(TAG, "当前密码: %06lu - 上报服务器", (unsigned long)pwd);
            
            // 新增: 上报密码到服务器
            if (protocol_ && protocol_->IsAudioChannelOpened()) {
                protocol_->SendPasswordReport(pwd);
            }
            break;
        }
        
        default:
            ESP_LOGW(TAG, "未知上报类型: 0x%02X", msg.type);
            break;
    }
}
```

### 4.2 main/protocols/websocket_protocol.h

**新增方法声明**：

```cpp
class WebsocketProtocol : public Protocol {
public:
    // ... 现有方法 ...
    
    // 新增：密码上报
    void SendPasswordReport(uint32_t password);
};
```

### 4.3 main/protocols/websocket_protocol.cc

**新增方法实现**：

```cpp
void WebsocketProtocol::SendPasswordReport(uint32_t password) {
    if (!IsAudioChannelOpened()) {
        ESP_LOGW(TAG, "音频通道未打开，无法上报密码");
        return;
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "password_report");
    cJSON_AddNumberToObject(root, "ts", esp_timer_get_time() / 1000);
    
    cJSON* data = cJSON_CreateObject();
    char pwd_str[8];
    snprintf(pwd_str, sizeof(pwd_str), "%06lu", (unsigned long)password);
    cJSON_AddStringToObject(data, "password", pwd_str);
    cJSON_AddItemToObject(root, "data", data);
    
    char* json_str = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "上报密码: %s", json_str);
    SendText(json_str);
    free(json_str);
    cJSON_Delete(root);
}
```

### 4.4 新增音效文件

**文件路径**：`main/assets/locales/zh-CN/door_not_closed.ogg`

**制作步骤**：
1. 录制或合成语音："门未关好，请检查"
2. 转换格式：
   ```bash
   ffmpeg -i door_not_closed.wav -c:a libopus -b:a 32k -ar 16000 -ac 1 door_not_closed.ogg
   ```
3. 放置到 `main/assets/locales/zh-CN/` 目录
4. 重新编译固件

---

## 5. 服务器协议更新

### 5.1 新增消息类型

在 `智能猫眼门锁系统-服务器与ESP32通信协议规范-v5.0.md` 中新增：

**第 4 节新增 4.5 小节**：

```markdown
### 4.5 密码上报 (password_report)

**触发条件：** ESP32 收到 STM32 的密码查询结果 (RPT_PWD) 时

```json
{
    "type": "password_report",
    "ts": 1702234567890,
    "data": {
        "password": "123456"
    }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| password | string | 6位数字密码 |

> **Server 处理**：存储到数据库，并转发给所有关联的 App
> **安全说明**：密码仅在用户主动查询时上报，不会定期上报
```

### 5.2 消息类型汇总更新

**第 11.1 节 ESP32 上报消息表新增**：

| type | 说明 | Server 处理 |
|------|------|-------------|
| `password_report` | 密码查询结果 | 存储 + 转发给 App |

---

## 6. 测试用例

### 6.1 EVT_TAMPER 测试

| 步骤 | 操作 | 预期结果 |
|------|------|----------|
| 1 | STM32 发送撬锁报警 | ESP32 日志显示 "撬锁报警 - 仅上报服务器" |
| 2 | 检查 ESP32 屏幕 | 无警告显示，仅显示摄像头画面 |
| 3 | 检查 ESP32 扬声器 | 无蜂鸣器声音 |
| 4 | 检查服务器 | 收到 `event_report: tamper` |

### 6.2 EVT_DOOR_OPEN 测试

| 步骤 | 操作 | 预期结果 |
|------|------|----------|
| 1 | STM32 发送门未关超时 | ESP32 日志显示 "门未关超时 - 语音播报" |
| 2 | 检查 ESP32 屏幕 | 无警告显示 |
| 3 | 检查 ESP32 扬声器 | 播放 "门未关好，请检查" 语音 |
| 4 | 检查服务器 | 收到 `event_report: door_open` |

### 6.3 EVT_LOW_BATTERY 测试

| 步骤 | 操作 | 预期结果 |
|------|------|----------|
| 1 | STM32 发送低电量警告 | ESP32 日志显示 "低电量警告 - 仅上报服务器" |
| 2 | 检查 ESP32 屏幕 | 无警告显示 |
| 3 | 检查服务器 | 收到 `event_report: low_battery` |

### 6.4 RPT_PWD 测试

| 步骤 | 操作 | 预期结果 |
|------|------|----------|
| 1 | 服务器发送密码查询命令 | ESP32 转发到 STM32 |
| 2 | STM32 返回密码 | ESP32 日志显示 "当前密码 - 上报服务器" |
| 3 | 检查服务器 | 收到 `password_report` |

---

## 7. 修改优先级

| 优先级 | 修改项 | 工作量 |
|--------|--------|--------|
| P0 | EVT_TAMPER 移除本地处理 | 低 |
| P0 | EVT_LOW_BATTERY 移除显示 | 低 |
| P1 | EVT_DOOR_OPEN 语音播报 | 中（需制作音效） |
| P1 | RPT_PWD 上报服务器 | 中（需新增协议方法） |

---

## 8. 版本历史

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| v1.0 | 2026-01-13 | 初始版本 |

---

**文档维护者**：毕业设计项目组  
**最后更新**：2026-01-13


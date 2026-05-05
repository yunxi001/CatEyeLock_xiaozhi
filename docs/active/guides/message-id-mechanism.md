# 消息ID机制与工作流程

**最后更新**: 2026-01-16

---

## 概述

消息ID（msg_id / seq_id）机制用于实现消息追溯和防重放攻击。

---

## 核心概念

### 1. 两级确认机制

ESP32 收到带 `seq_id` 的指令后，实现两级确认：

**第一级确认：esp32_ack**

- ESP32 收到命令后**立即**发送
- 表示"命令已收到，开始处理"

**第二级确认：ack**

- STM32 执行完成后发送
- 表示"命令执行完成"
- 携带执行结果

### 2. 防重放机制

- ESP32 维护最近 100 条 seq_id 缓存（FIFO 淘汰）
- 重复的 seq_id 消息直接丢弃
- 防止指令被重复执行

---

## 消息流程

```
Server                    ESP32                    STM32
  │                         │                        │
  │ {"seq_id": "xxx",       │                        │
  │  "command": "unlock"}   │                        │
  │────────────────────────>│                        │
  │                         │                        │
  │                         │ esp32_ack              │
  │<────────────────────────│                        │
  │                         │                        │
  │                         │ UART: CMD_LOCK         │
  │                         │───────────────────────>│
  │                         │                        │
  │                         │ UART: ACK_OK           │
  │                         │<───────────────────────│
  │                         │                        │
  │ ack (success)           │                        │
  │<────────────────────────│                        │
```

---

## 实现细节

### 1. seq_id 检查

**代码位置**: `main/protocols/websocket_protocol.cc`

```cpp
// v5.0 协议：msg_id 防重放检查
auto msg_id = cJSON_GetObjectItem(root, "msg_id");
if (cJSON_IsString(msg_id)) {
    std::string msg_id_str = msg_id->valuestring;
    if (IsDuplicateMsgId(msg_id_str)) {
        ESP_LOGW(TAG, "重复的 msg_id，忽略消息: %s", msg_id_str.c_str());
        cJSON_Delete(root);
        return;
    }
    // 添加到缓存
    AddMsgIdToCache(msg_id_str);
}
```

### 2. 缓存管理

```cpp
bool WebsocketProtocol::IsDuplicateMsgId(const std::string& msg_id) {
    return msg_id_set_.find(msg_id) != msg_id_set_.end();
}

void WebsocketProtocol::AddMsgIdToCache(const std::string& msg_id) {
    // FIFO 淘汰
    if (msg_id_queue_.size() >= MSG_ID_CACHE_SIZE) {
        std::string old_id = msg_id_queue_.front();
        msg_id_queue_.pop();
        msg_id_set_.erase(old_id);
    }

    // 添加新 ID
    msg_id_queue_.push(msg_id);
    msg_id_set_.insert(msg_id);
}
```

---

## 相关文档

- [ESP32与服务器通信协议 v5.2](../protocols/esp32-server-v5.2.md)
- [完整的消息ID机制文档](../../my_docs/消息ID机制与工作流程.md)

---

**最后更新**: 2026-01-16

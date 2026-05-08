# 通信协议文档

## 文件结构

```
/refactor_guide/
├── 00-总体通信协议规范.md        (6.3KB)  系统架构、设计原则、消息路由、统一错误码
├── 01-ESP32-STM32-UART协议规范.md (13.4KB) 7字节UART帧、3类消息、无ACK、帧校验示例
├── 02-ESP32-Server-WebSocket协议规范.md (15.6KB) 门锁JSON消息4入6出、BinaryProtocol2保留
├── 03-Server-App-WebSocket协议规范.md (13.4KB) App认证、命令代理、推送透传、HTTP API
└── 04-协议实施指南.md           (16.9KB) 各端具体修改内容、验证清单、回滚方案
```

## 文档定位

| 文档组合 | 交付对象 |
|---------|---------|
| 00 + 01 | STM32开发者（不需要改，但需确认协议一致） |
| 00 + 01 + 02 + 04 | ESP32开发者 |
| 00 + 02 + 03 + 04 | 服务器开发者 |
| 00 + 03 + 04 | App开发者 |

## 核心简化点

- **ESP32↔STM32**：CAT 0x00 整类删除（ACK_OK/ACK_ERR/PING/PONG），STM32不再回复ACK
- **ESP32↔Server**：去掉 esp32_ack、seq_id、防重放、pending_commands_ 队列、heartbeat
- **Server↔App**：去掉 server_ack、WebSocket query、media_download、face_management（改为HTTP API）
- **错误码**：从 0-10 体系简化为 `{code: 0或1, msg: "描述"}`

## 说明

实施指南（文档04）里每个需要修改的文件都写了具体的伪代码逻辑。

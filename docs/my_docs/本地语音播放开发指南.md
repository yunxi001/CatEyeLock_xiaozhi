# 本地语音播放开发指南

## 概述

本文档介绍在智能猫眼门锁系统中如何使用本地语音播放功能，包括 API 调用方法、音效资源管理和开发注意事项。

## 功能特性

- **内置音效库**：支持多语言本地化音效
- **OGG 格式**：使用 Opus 编码的 OGG 容器格式
- **零延迟播放**：音效直接嵌入固件，无需文件系统访问
- **自动功率管理**：播放时自动启用音频输出，节省功耗

## API 接口

### 基本播放方法

```cpp
// 通过 Application 单例播放音效
Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);

// 通过 AudioService 直接播放
auto& audio_service = Application::GetInstance().GetAudioService();
audio_service.PlaySound(Lang::Sounds::OGG_EXCLAMATION);
```

### 函数签名

```cpp
// AudioService 类中的播放方法
void PlaySound(const std::string_view& sound);

// Application 类中的便捷方法
void PlaySound(const std::string_view& sound);
```

## 内置音效资源

### 公共音效（所有语言通用）

| 常量名 | 文件名 | 用途 |
|--------|--------|------|
| `Lang::Sounds::OGG_SUCCESS` | `success.ogg` | 操作成功提示 |
| `Lang::Sounds::OGG_EXCLAMATION` | `exclamation.ogg` | 警告/注意提示 |
| `Lang::Sounds::OGG_POPUP` | `popup.ogg` | 弹窗提示音 |
| `Lang::Sounds::OGG_LOW_BATTERY` | `low_battery.ogg` | 低电量警告 |
| `Lang::Sounds::OGG_VIBRATION` | `vibration.ogg` | 振动反馈音 |

### 语言特定音效

根据编译时选择的语言（`CONFIG_LANG`），系统会加载对应语言包中的音效文件：
- 路径：`main/assets/locales/{语言代码}/*.ogg`
- 支持 37 种语言的本地化音效
- 缺失文件自动回退到 `en-US` 版本

## 使用示例

### 1. 系统状态提示

```cpp
// 设备启动成功
void Application::OnSystemReady() {
    display->ShowNotification("系统已就绪");
    PlaySound(Lang::Sounds::OGG_SUCCESS);
}

// 错误提示
void Application::OnError(const std::string& error_msg) {
    Alert("错误", error_msg.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
}
```

### 2. 门锁操作反馈

```cpp
// 开锁成功
void LockControl::OnUnlockSuccess() {
    ESP_LOGI(TAG, "门锁已打开");
    Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);
}

// 门铃检测
void LockControl::OnDoorbellDetected() {
    ESP_LOGI(TAG, "检测到门铃");
    Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
}
```

### 3. 用户交互反馈

```cpp
// 触摸屏操作确认
void OnButtonPressed() {
    // 播放按键音效
    Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
    
    // 执行具体操作
    ProcessButtonAction();
}
```

## 添加自定义音效

### 1. 准备音频文件

```bash
# 音频格式要求
- 格式：OGG 容器 + Opus 编码
- 采样率：16kHz（推荐）
- 声道：单声道
- 时长：建议 < 3 秒
```

### 2. 放置文件

```bash
# 公共音效（所有语言通用）
main/assets/common/my_sound.ogg

# 语言特定音效
main/assets/locales/zh-CN/my_sound.ogg
main/assets/locales/en-US/my_sound.ogg
```

### 3. 重新编译

```bash
# 音效文件会自动嵌入固件
idf.py build
```

### 4. 代码中使用

```cpp
// 编译后自动生成的常量
Application::GetInstance().PlaySound(Lang::Sounds::OGG_MY_SOUND);
```

## 技术原理

### 1. 嵌入机制

```cmake
# CMakeLists.txt 中的配置
file(GLOB COMMON_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/common/*.ogg)
file(GLOB LANG_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/locales/${LANG_DIR}/*.ogg)

idf_component_register(
    EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS}
    ...
)
```

### 2. 内存映射

```cpp
// 生成的符号引用
extern const char ogg_success_start[] asm("_binary_success_ogg_start");
extern const char ogg_success_end[] asm("_binary_success_ogg_end");

// 封装为 string_view
static const std::string_view OGG_SUCCESS {
    static_cast<const char*>(ogg_success_start),
    static_cast<size_t>(ogg_success_end - ogg_success_start)
};
```

### 3. 播放流程

```cpp
void AudioService::PlaySound(const std::string_view& ogg) {
    // 1. 启用音频输出
    if (!codec_->output_enabled()) {
        codec_->EnableOutput(true);
    }
    
    // 2. 解析 OGG 页面结构
    // 3. 提取 Opus 音频包
    // 4. 解码并播放到扬声器
}
```

## 开发注意事项

### 1. 内存使用

- **固件大小**：每个音效文件会增加固件大小
- **运行时内存**：播放时需要临时解码缓冲区
- **建议**：音效文件保持简短（< 3 秒），避免过多自定义音效

### 2. 音频格式

```bash
# 推荐的转换命令（使用 FFmpeg）
ffmpeg -i input.wav -c:a libopus -b:a 32k -ar 16000 -ac 1 output.ogg
```

### 3. 播放时机

- **非阻塞**：`PlaySound()` 是异步调用，不会阻塞当前任务
- **并发**：可以在播放过程中调用其他音效（会中断当前播放）
- **功耗**：播放完成后音频输出会自动关闭以节省功耗

### 4. 错误处理

```cpp
// 播放方法不返回错误状态，但会在日志中记录问题
void PlaySoundSafely(const std::string_view& sound) {
    try {
        Application::GetInstance().PlaySound(sound);
    } catch (...) {
        ESP_LOGW(TAG, "音效播放失败，继续执行");
    }
}
```

### 5. 调试技巧

```cpp
// 启用音频调试日志
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include <esp_log.h>

// 检查音效是否正确嵌入
ESP_LOGI(TAG, "音效大小: %d 字节", Lang::Sounds::OGG_SUCCESS.size());
```

## 最佳实践

### 1. 音效设计原则

- **简洁明了**：每个音效应有明确的语义
- **音量适中**：避免过响或过轻
- **风格统一**：保持整体音效风格一致

### 2. 使用场景

| 场景 | 推荐音效 | 说明 |
|------|----------|------|
| 操作成功 | `OGG_SUCCESS` | 开锁、设置保存等 |
| 错误警告 | `OGG_EXCLAMATION` | 识别失败、网络错误等 |
| 用户交互 | `OGG_POPUP` | 按键确认、菜单切换等 |
| 系统状态 | `OGG_LOW_BATTERY` | 电量不足、设备异常等 |

### 3. 性能优化

```cpp
// 避免频繁播放相同音效
class SoundThrottle {
    std::chrono::steady_clock::time_point last_play_time_;
    static constexpr auto MIN_INTERVAL = std::chrono::milliseconds(500);
    
public:
    bool ShouldPlay() {
        auto now = std::chrono::steady_clock::now();
        if (now - last_play_time_ > MIN_INTERVAL) {
            last_play_time_ = now;
            return true;
        }
        return false;
    }
};
```

## 故障排除

### 常见问题

1. **音效无声音**
   - 检查音频输出是否启用
   - 确认音效文件格式正确
   - 查看串口日志中的错误信息

2. **编译失败**
   - 确认音效文件路径正确
   - 检查文件名是否包含特殊字符
   - 验证 OGG 文件格式有效性

3. **播放卡顿**
   - 检查系统内存使用情况
   - 避免在中断处理函数中播放音效
   - 确认音频编解码器工作正常

### 调试命令

```bash
# 检查嵌入的音效文件
objdump -t build/main/libmain.a | grep ogg

# 查看固件大小变化
idf.py size-components
```

## 总结

本地语音播放功能为智能门锁系统提供了丰富的用户反馈机制。通过合理使用内置音效和适当添加自定义音效，可以显著提升用户体验。开发时需要注意内存使用、音频格式和播放时机，确保系统稳定运行。
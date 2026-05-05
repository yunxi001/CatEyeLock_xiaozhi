# 语音资源开发完整指南

本文档整合了本地语音播放开发指南和语音资源清单，提供完整的语音资源生成与集成操作步骤。

---

## 一、当前实现状态

### 1.1 代码实现情况

| 模块 | 状态 | 说明 |
|------|------|------|
| `PlayAuthFailVoice()` | ✅ 已实现 | 播放认证失败语音（拼接方式），但内部播放调用已注释 |
| `PlayLockedVoice()` | ✅ 已实现 | 播放设备锁定语音（拼接方式），但内部播放调用已注释 |
| `PlayNumberVoice()` | ✅ 已实现 | 播放数字语音（0-99） |
| 事件处理调用 | ✅ 已实现 | `HandleLockReportMessage()` 中已调用上述方法 |
| 语音常量定义 | ❌ 未定义 | `lang_config.h` 中缺少 15 个新增语音常量 |
| 语音文件 | ❌ 未添加 | `main/assets/locales/zh-CN/` 中缺少 15 个 OGG 文件 |

### 1.2 现有语音资源

**公共音效**（`main/assets/common/`）：
- `success.ogg` - 操作成功
- `exclamation.ogg` - 警告提示
- `popup.ogg` - 弹窗提示
- `low_battery.ogg` - 低电量
- `vibration.ogg` - 振动反馈

**语言特定音效**（`main/assets/locales/zh-CN/`）：
- `0.ogg` ~ `9.ogg` - 数字语音（已存在，可复用）
- `activation.ogg` - 激活提示
- `welcome.ogg` - 欢迎语音
- `upgrade.ogg` - 升级提示
- `wificonfig.ogg` - 配网提示
- `err_pin.ogg` / `err_reg.ogg` - 错误提示

---

## 二、需要新增的语音资源

### 2.1 完整清单（15 个文件）

| 序号 | 文件名 | 语音内容 | 分类 |
|------|--------|----------|------|
| 1 | `tamper_alert.ogg` | "检测到异常，请注意安全" | 安全告警 |
| 2 | `door_not_closed.ogg` | "门未关闭，请注意关门" | 安全告警 |
| 3 | `auth_fail_prefix.ogg` | "认证失败，还剩" | 认证失败 |
| 4 | `auth_fail_suffix.ogg` | "次机会" | 认证失败 |
| 5 | `locked_prefix.ogg` | "设备已锁定，请" | 设备锁定 |
| 6 | `locked_suffix.ogg` | "分钟后再试" | 设备锁定 |
| 7 | `fp_press.ogg` | "请按压手指" | 指纹录入 |
| 8 | `fp_lift.ogg` | "请抬起手指" | 指纹录入 |
| 9 | `fp_press_again.ogg` | "请再次按压" | 指纹录入 |
| 10 | `nfc_tap.ogg` | "请刷卡" | NFC 录入 |
| 11 | `nfc_tap_again.ogg` | "请再次刷卡" | NFC 录入 |
| 12 | `enroll_success.ogg` | "录入成功" | 录入结果 |
| 13 | `enroll_fail.ogg` | "录入失败，请重试" | 录入结果 |
| 14 | `already_exists.ogg` | "该特征已存在" | 录入结果 |
| 15 | `id_occupied.ogg` | "指定编号已占用，已自动分配新编号" | 录入结果 |

### 2.2 语音拼接设计

**认证失败语音**：`auth_fail_prefix.ogg` + `{数字}.ogg` + `auth_fail_suffix.ogg`
- 示例："认证失败，还剩" + "4" + "次机会"

**设备锁定语音**：`locked_prefix.ogg` + `{数字}.ogg` + `locked_suffix.ogg`
- 示例："设备已锁定，请" + "3" + "分钟后再试"
- 两位数拼接：`1.ogg` + `0.ogg` = "10"

---

## 三、操作步骤

### 步骤 1：生成语音文件

#### 方法 A：使用在线 TTS 服务（推荐）

1. 访问在线 TTS 服务（如阿里云、讯飞、百度等）
2. 选择中文女声，语速适中
3. 输入语音内容，生成 MP3/WAV 文件
4. 使用 FFmpeg 转换为 OGG 格式

#### 方法 B：使用 Edge TTS（免费）

```bash
# 安装 edge-tts
pip install edge-tts

# 生成语音（使用中文女声 zh-CN-XiaoxiaoNeural）
edge-tts --voice zh-CN-XiaoxiaoNeural --text "检测到异常，请注意安全" --write-media tamper_alert.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "门未关闭，请注意关门" --write-media door_not_closed.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "认证失败，还剩" --write-media auth_fail_prefix.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "次机会" --write-media auth_fail_suffix.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "设备已锁定，请" --write-media locked_prefix.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "分钟后再试" --write-media locked_suffix.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "请按压手指" --write-media fp_press.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "请抬起手指" --write-media fp_lift.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "请再次按压" --write-media fp_press_again.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "请刷卡" --write-media nfc_tap.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "请再次刷卡" --write-media nfc_tap_again.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "录入成功" --write-media enroll_success.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "录入失败，请重试" --write-media enroll_fail.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "该特征已存在" --write-media already_exists.mp3
edge-tts --voice zh-CN-XiaoxiaoNeural --text "指定编号已占用，已自动分配新编号" --write-media id_occupied.mp3
```

#### 方法 C：批量生成脚本

创建 `generate_voices.py`：

```python
import subprocess
import os

# 语音内容列表
voices = [
    ("tamper_alert", "检测到异常，请注意安全"),
    ("door_not_closed", "门未关闭，请注意关门"),
    ("auth_fail_prefix", "认证失败，还剩"),
    ("auth_fail_suffix", "次机会"),
    ("locked_prefix", "设备已锁定，请"),
    ("locked_suffix", "分钟后再试"),
    ("fp_press", "请按压手指"),
    ("fp_lift", "请抬起手指"),
    ("fp_press_again", "请再次按压"),
    ("nfc_tap", "请刷卡"),
    ("nfc_tap_again", "请再次刷卡"),
    ("enroll_success", "录入成功"),
    ("enroll_fail", "录入失败，请重试"),
    ("already_exists", "该特征已存在"),
    ("id_occupied", "指定编号已占用，已自动分配新编号"),
]

output_dir = "voices"
os.makedirs(output_dir, exist_ok=True)

for name, text in voices:
    mp3_file = f"{output_dir}/{name}.mp3"
    ogg_file = f"{output_dir}/{name}.ogg"
    
    # 生成 MP3
    subprocess.run([
        "edge-tts",
        "--voice", "zh-CN-XiaoxiaoNeural",
        "--text", text,
        "--write-media", mp3_file
    ])
    
    # 转换为 OGG (Opus 编码, 16kHz, 单声道)
    subprocess.run([
        "ffmpeg", "-y",
        "-i", mp3_file,
        "-c:a", "libopus",
        "-b:a", "32k",
        "-ar", "16000",
        "-ac", "1",
        ogg_file
    ])
    
    print(f"✓ 生成: {ogg_file}")

print(f"\n完成！共生成 {len(voices)} 个语音文件")
```

运行脚本：
```bash
python generate_voices.py
```

### 步骤 2：转换音频格式

如果已有 MP3/WAV 文件，使用 FFmpeg 转换：

```bash
# 单个文件转换
ffmpeg -i input.mp3 -c:a libopus -b:a 32k -ar 16000 -ac 1 output.ogg

# 批量转换（PowerShell）
Get-ChildItem *.mp3 | ForEach-Object {
    $output = $_.BaseName + ".ogg"
    ffmpeg -i $_.FullName -c:a libopus -b:a 32k -ar 16000 -ac 1 $output
}
```

**音频格式要求**：
- 格式：OGG 容器 + Opus 编码
- 采样率：16kHz
- 声道：单声道
- 比特率：32kbps（推荐）
- 时长：< 3 秒

### 步骤 3：放置语音文件

将生成的 15 个 OGG 文件复制到：

```
main/assets/locales/zh-CN/
├── tamper_alert.ogg
├── door_not_closed.ogg
├── auth_fail_prefix.ogg
├── auth_fail_suffix.ogg
├── locked_prefix.ogg
├── locked_suffix.ogg
├── fp_press.ogg
├── fp_lift.ogg
├── fp_press_again.ogg
├── nfc_tap.ogg
├── nfc_tap_again.ogg
├── enroll_success.ogg
├── enroll_fail.ogg
├── already_exists.ogg
└── id_occupied.ogg
```

### 步骤 4：更新 lang_config.h

在 `main/assets/lang_config.h` 的 `namespace Sounds` 中添加以下常量定义：

```cpp
// ========== 新增语音资源 ==========

// 安全告警类
extern const char ogg_tamper_alert_start[] asm("_binary_tamper_alert_ogg_start");
extern const char ogg_tamper_alert_end[] asm("_binary_tamper_alert_ogg_end");
static const std::string_view OGG_TAMPER_ALERT {
    static_cast<const char*>(ogg_tamper_alert_start),
    static_cast<size_t>(ogg_tamper_alert_end - ogg_tamper_alert_start)
};

extern const char ogg_door_not_closed_start[] asm("_binary_door_not_closed_ogg_start");
extern const char ogg_door_not_closed_end[] asm("_binary_door_not_closed_ogg_end");
static const std::string_view OGG_DOOR_NOT_CLOSED {
    static_cast<const char*>(ogg_door_not_closed_start),
    static_cast<size_t>(ogg_door_not_closed_end - ogg_door_not_closed_start)
};

// 认证失败类
extern const char ogg_auth_fail_prefix_start[] asm("_binary_auth_fail_prefix_ogg_start");
extern const char ogg_auth_fail_prefix_end[] asm("_binary_auth_fail_prefix_ogg_end");
static const std::string_view OGG_AUTH_FAIL_PREFIX {
    static_cast<const char*>(ogg_auth_fail_prefix_start),
    static_cast<size_t>(ogg_auth_fail_prefix_end - ogg_auth_fail_prefix_start)
};

extern const char ogg_auth_fail_suffix_start[] asm("_binary_auth_fail_suffix_ogg_start");
extern const char ogg_auth_fail_suffix_end[] asm("_binary_auth_fail_suffix_ogg_end");
static const std::string_view OGG_AUTH_FAIL_SUFFIX {
    static_cast<const char*>(ogg_auth_fail_suffix_start),
    static_cast<size_t>(ogg_auth_fail_suffix_end - ogg_auth_fail_suffix_start)
};

// 设备锁定类
extern const char ogg_locked_prefix_start[] asm("_binary_locked_prefix_ogg_start");
extern const char ogg_locked_prefix_end[] asm("_binary_locked_prefix_ogg_end");
static const std::string_view OGG_LOCKED_PREFIX {
    static_cast<const char*>(ogg_locked_prefix_start),
    static_cast<size_t>(ogg_locked_prefix_end - ogg_locked_prefix_start)
};

extern const char ogg_locked_suffix_start[] asm("_binary_locked_suffix_ogg_start");
extern const char ogg_locked_suffix_end[] asm("_binary_locked_suffix_ogg_end");
static const std::string_view OGG_LOCKED_SUFFIX {
    static_cast<const char*>(ogg_locked_suffix_start),
    static_cast<size_t>(ogg_locked_suffix_end - ogg_locked_suffix_start)
};

// 指纹录入类
extern const char ogg_fp_press_start[] asm("_binary_fp_press_ogg_start");
extern const char ogg_fp_press_end[] asm("_binary_fp_press_ogg_end");
static const std::string_view OGG_FP_PRESS {
    static_cast<const char*>(ogg_fp_press_start),
    static_cast<size_t>(ogg_fp_press_end - ogg_fp_press_start)
};

extern const char ogg_fp_lift_start[] asm("_binary_fp_lift_ogg_start");
extern const char ogg_fp_lift_end[] asm("_binary_fp_lift_ogg_end");
static const std::string_view OGG_FP_LIFT {
    static_cast<const char*>(ogg_fp_lift_start),
    static_cast<size_t>(ogg_fp_lift_end - ogg_fp_lift_start)
};

extern const char ogg_fp_press_again_start[] asm("_binary_fp_press_again_ogg_start");
extern const char ogg_fp_press_again_end[] asm("_binary_fp_press_again_ogg_end");
static const std::string_view OGG_FP_PRESS_AGAIN {
    static_cast<const char*>(ogg_fp_press_again_start),
    static_cast<size_t>(ogg_fp_press_again_end - ogg_fp_press_again_start)
};

// NFC 录入类
extern const char ogg_nfc_tap_start[] asm("_binary_nfc_tap_ogg_start");
extern const char ogg_nfc_tap_end[] asm("_binary_nfc_tap_ogg_end");
static const std::string_view OGG_NFC_TAP {
    static_cast<const char*>(ogg_nfc_tap_start),
    static_cast<size_t>(ogg_nfc_tap_end - ogg_nfc_tap_start)
};

extern const char ogg_nfc_tap_again_start[] asm("_binary_nfc_tap_again_ogg_start");
extern const char ogg_nfc_tap_again_end[] asm("_binary_nfc_tap_again_ogg_end");
static const std::string_view OGG_NFC_TAP_AGAIN {
    static_cast<const char*>(ogg_nfc_tap_again_start),
    static_cast<size_t>(ogg_nfc_tap_again_end - ogg_nfc_tap_again_start)
};

// 录入结果类
extern const char ogg_enroll_success_start[] asm("_binary_enroll_success_ogg_start");
extern const char ogg_enroll_success_end[] asm("_binary_enroll_success_ogg_end");
static const std::string_view OGG_ENROLL_SUCCESS {
    static_cast<const char*>(ogg_enroll_success_start),
    static_cast<size_t>(ogg_enroll_success_end - ogg_enroll_success_start)
};

extern const char ogg_enroll_fail_start[] asm("_binary_enroll_fail_ogg_start");
extern const char ogg_enroll_fail_end[] asm("_binary_enroll_fail_ogg_end");
static const std::string_view OGG_ENROLL_FAIL {
    static_cast<const char*>(ogg_enroll_fail_start),
    static_cast<size_t>(ogg_enroll_fail_end - ogg_enroll_fail_start)
};

extern const char ogg_already_exists_start[] asm("_binary_already_exists_ogg_start");
extern const char ogg_already_exists_end[] asm("_binary_already_exists_ogg_end");
static const std::string_view OGG_ALREADY_EXISTS {
    static_cast<const char*>(ogg_already_exists_start),
    static_cast<size_t>(ogg_already_exists_end - ogg_already_exists_start)
};

extern const char ogg_id_occupied_start[] asm("_binary_id_occupied_ogg_start");
extern const char ogg_id_occupied_end[] asm("_binary_id_occupied_ogg_end");
static const std::string_view OGG_ID_OCCUPIED {
    static_cast<const char*>(ogg_id_occupied_start),
    static_cast<size_t>(ogg_id_occupied_end - ogg_id_occupied_start)
};
```

### 步骤 5：取消代码注释

在 `main/application.cc` 中取消以下注释：

**撬锁报警**（约第 1126 行）：
```cpp
// 修改前
// audio_service_.PlaySound(Lang::Sounds::OGG_TAMPER_ALERT);

// 修改后
audio_service_.PlaySound(Lang::Sounds::OGG_TAMPER_ALERT);
```

**门未关闭**（约第 1134 行）：
```cpp
// 修改前
// audio_service_.PlaySound(Lang::Sounds::OGG_DOOR_NOT_CLOSED);

// 修改后
audio_service_.PlaySound(Lang::Sounds::OGG_DOOR_NOT_CLOSED);
```

**认证失败语音**（`PlayAuthFailVoice()` 函数内）：
```cpp
// 修改前
// audio_service_.PlaySound(Lang::Sounds::OGG_AUTH_FAIL_PREFIX);
// ...
// audio_service_.PlaySound(Lang::Sounds::OGG_AUTH_FAIL_SUFFIX);

// 修改后
audio_service_.PlaySound(Lang::Sounds::OGG_AUTH_FAIL_PREFIX);
// ...
audio_service_.PlaySound(Lang::Sounds::OGG_AUTH_FAIL_SUFFIX);
```

**设备锁定语音**（`PlayLockedVoice()` 函数内）：
```cpp
// 修改前
// audio_service_.PlaySound(Lang::Sounds::OGG_LOCKED_PREFIX);
// ...
// audio_service_.PlaySound(Lang::Sounds::OGG_LOCKED_SUFFIX);

// 修改后
audio_service_.PlaySound(Lang::Sounds::OGG_LOCKED_PREFIX);
// ...
audio_service_.PlaySound(Lang::Sounds::OGG_LOCKED_SUFFIX);
```

### 步骤 6：编译验证

```bash
# 清理并重新编译
idf.py fullclean
idf.py build

# 检查嵌入的音效文件
objdump -t build/main/libmain.a | findstr ogg

# 查看固件大小变化
idf.py size-components
```

---

## 四、API 使用参考

### 4.1 基本播放方法

```cpp
// 通过 Application 单例播放
Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);

// 通过 AudioService 直接播放
auto& audio_service = Application::GetInstance().GetAudioService();
audio_service.PlaySound(Lang::Sounds::OGG_EXCLAMATION);
```

### 4.2 语音拼接播放

```cpp
// 认证失败语音（已封装）
Application::GetInstance().PlayAuthFailVoice(remaining_attempts);

// 设备锁定语音（已封装）
Application::GetInstance().PlayLockedVoice(remaining_minutes);

// 数字语音
Application::GetInstance().PlayNumberVoice(number);
```

### 4.3 使用场景示例

```cpp
// 撬锁报警
case LockEventType::EVT_TAMPER:
    ESP_LOGW(TAG, "撬锁报警 (级别 %d)", param);
    audio_service_.PlaySound(Lang::Sounds::OGG_TAMPER_ALERT);
    break;

// 门未关闭
case LockEventType::EVT_DOOR_OPEN:
    ESP_LOGW(TAG, "门未关超时 (%d 分钟)", param);
    audio_service_.PlaySound(Lang::Sounds::OGG_DOOR_NOT_CLOSED);
    break;

// 指纹录入
case FingerprintEvent::FP_PRESS_FINGER:
    if (press_count == 1) {
        PlaySound(Lang::Sounds::OGG_FP_PRESS);
    } else {
        PlaySound(Lang::Sounds::OGG_FP_PRESS_AGAIN);
    }
    break;

// NFC 录入
case NfcRespStatus::NFC_TAP:
    PlaySound(Lang::Sounds::OGG_NFC_TAP);
    break;
```

---

## 五、注意事项

### 5.1 内存使用

- 每个 OGG 文件会增加固件大小（约 2-5KB/秒）
- 15 个新增语音预计增加约 50-100KB 固件大小
- 播放时需要临时解码缓冲区

### 5.2 播放特性

- `PlaySound()` 是异步调用，不阻塞当前任务
- 新的播放会中断当前正在播放的音效
- 播放完成后音频输出自动关闭以节省功耗

### 5.3 调试技巧

```cpp
// 检查音效是否正确嵌入
ESP_LOGI(TAG, "音效大小: %d 字节", Lang::Sounds::OGG_TAMPER_ALERT.size());

// 如果大小为 0，说明文件未正确嵌入
```

### 5.4 常见问题

1. **编译报错 "undefined reference to _binary_xxx"**
   - 检查 OGG 文件是否放在正确目录
   - 检查文件名是否与常量定义匹配（注意下划线）

2. **播放无声音**
   - 检查音频输出是否启用
   - 确认 OGG 文件格式正确（Opus 编码）
   - 查看串口日志中的错误信息

3. **固件过大**
   - 压缩语音时长（< 2 秒）
   - 降低比特率（24kbps）
   - 合并相似语音

---

## 六、快速检查清单

- [ ] 安装 edge-tts 和 FFmpeg
- [ ] 生成 15 个 OGG 语音文件
- [ ] 复制文件到 `main/assets/locales/zh-CN/`
- [ ] 更新 `main/assets/lang_config.h` 添加常量定义
- [ ] 取消 `main/application.cc` 中的播放代码注释
- [ ] 执行 `idf.py build` 编译
- [ ] 烧录测试语音播放功能

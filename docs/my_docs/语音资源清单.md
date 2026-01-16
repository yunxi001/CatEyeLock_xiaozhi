# 语音资源清单

本文档列出智能猫眼门锁系统需要的本地语音资源，用于各种事件的语音提示。

## 设计原则

1. **数字拼接**：利用现有数字语音（0-9）进行拼接，节省 Flash 空间
2. **简短明确**：每条语音控制在 2-3 秒内
3. **格式统一**：OGG 格式，采样率与现有资源保持一致

---

## 一、安全告警类

| 文件名 | 语音内容 | 触发场景 | 备注 |
|--------|----------|----------|------|
| `tamper_alert.ogg` | "检测到异常，请注意安全" | 撬锁报警 (EVT_TAMPER) | 新增 |
| `door_not_closed.ogg` | "门未关闭，请注意关门" | 门未关超时 (EVT_DOOR_OPEN) | 新增 |

---

## 二、认证失败与锁定类

### 2.1 认证失败提示

使用数字拼接方式：`"认证失败，还剩"` + `数字` + `"次机会"`

| 文件名 | 语音内容 | 备注 |
|--------|----------|------|
| `auth_fail_prefix.ogg` | "认证失败，还剩" | 前缀 |
| `auth_fail_suffix.ogg` | "次机会" | 后缀 |

**拼接示例**：`auth_fail_prefix.ogg` + `4.ogg` + `auth_fail_suffix.ogg` = "认证失败，还剩4次机会"

### 2.2 设备锁定提示

使用数字拼接方式：`"设备已锁定，请"` + `数字` + `"分钟后再试"`

| 文件名 | 语音内容 | 备注 |
|--------|----------|------|
| `locked_prefix.ogg` | "设备已锁定，请" | 前缀 |
| `locked_suffix.ogg` | "分钟后再试" | 后缀 |

**拼接示例**：`locked_prefix.ogg` + `3.ogg` + `locked_suffix.ogg` = "设备已锁定，请3分钟后再试"

**锁定时间对应表**：

| 锁定等级 | 锁定时间 | 拼接数字 |
|----------|----------|----------|
| Level 0 | 1 分钟 | `1.ogg` |
| Level 1 | 3 分钟 | `3.ogg` |
| Level 2 | 5 分钟 | `5.ogg` |
| Level 3 | 10 分钟 | `1.ogg` + `0.ogg` |
| Level 4 | 30 分钟 | `3.ogg` + `0.ogg` |

---

## 三、指纹录入类

| 文件名 | 语音内容 | 触发场景 | 备注 |
|--------|----------|----------|------|
| `fp_press.ogg` | "请按压手指" | 首次按压提示 (FP_PRESS_FINGER, 第1次) | 新增 |
| `fp_lift.ogg` | "请抬起手指" | 抬起提示 (FP_LIFT_FINGER) | 新增 |
| `fp_press_again.ogg` | "请再次按压" | 后续按压提示 (FP_PRESS_FINGER, 第2次及以后) | 新增 |

---

## 四、NFC 录入类

| 文件名 | 语音内容 | 触发场景 | 备注 |
|--------|----------|----------|------|
| `nfc_tap.ogg` | "请刷卡" | NFC 等待刷卡 (NFC_RESP, D0=0x01) | 新增 |
| `nfc_tap_again.ogg` | "请再次刷卡" | NFC 再次刷卡 (NFC_RESP, D0=0x02) | 新增 |

---

## 五、录入结果类

| 文件名 | 语音内容 | 触发场景 | 备注 |
|--------|----------|----------|------|
| `enroll_success.ogg` | "录入成功" | 指纹/NFC 录入成功 (FP_SUCCESS) | 新增 |
| `enroll_fail.ogg` | "录入失败，请重试" | 指纹/NFC 录入失败 (FP_FAILED) | 新增 |
| `already_exists.ogg` | "该特征已存在" | 指纹/NFC 已存在 (FP_ALREADY_EXISTS) | 新增 |
| `id_occupied.ogg` | "指定编号已占用，已自动分配新编号" | ID 被占用 (FP_ID_OCCUPIED) | 新增 |

---

## 六、现有数字语音（复用）

以下数字语音已存在于项目中，可直接复用：

| 文件名 | 语音内容 |
|--------|----------|
| `0.ogg` | "零" |
| `1.ogg` | "一" |
| `2.ogg` | "二" |
| `3.ogg` | "三" |
| `4.ogg` | "四" |
| `5.ogg` | "五" |
| `6.ogg` | "六" |
| `7.ogg` | "七" |
| `8.ogg` | "八" |
| `9.ogg` | "九" |

---

## 七、语音播放逻辑

### 7.1 认证失败语音

```
失败次数 = D2 (从 RPT_UNLOCK 获取)
剩余次数 = 5 - 失败次数

播放序列：
1. auth_fail_prefix.ogg
2. {剩余次数}.ogg  (如 4.ogg)
3. auth_fail_suffix.ogg
```

### 7.2 设备锁定语音

```
剩余时间 = D1 (从 RPT_UNLOCK 获取，单位：分钟)

播放序列：
1. locked_prefix.ogg
2. 数字拼接（见下方规则）
3. locked_suffix.ogg

数字拼接规则：
- 1-9: 直接播放对应数字
- 10: 播放 1.ogg + 0.ogg
- 30: 播放 3.ogg + 0.ogg
```

### 7.3 指纹录入语音

```
收到 FP_PRESS_FINGER:
  if D1 == 1:  // 第一次
    播放 fp_press.ogg
  else:
    播放 fp_press_again.ogg

收到 FP_LIFT_FINGER:
  播放 fp_lift.ogg

收到 FP_SUCCESS:
  播放 enroll_success.ogg

收到 FP_FAILED:
  播放 enroll_fail.ogg

收到 FP_ALREADY_EXISTS:
  播放 already_exists.ogg

收到 FP_ID_OCCUPIED:
  播放 id_occupied.ogg
```

### 7.4 NFC 录入语音

```
收到 NFC_RESP:
  switch D0:
    case 0x01:  // 请刷卡（录入中）
      播放 nfc_tap.ogg
    case 0x02:  // 请再次刷卡
      播放 nfc_tap_again.ogg
    case 0x03:  // 成功
      播放 enroll_success.ogg
    case 0x04:  // 失败
      播放 enroll_fail.ogg
    case 0x06:  // 已存在
      播放 already_exists.ogg
    case 0x07:  // ID 被占用
      播放 id_occupied.ogg
```

---

## 八、文件清单汇总

### 需要新增的语音文件（共 15 个）

| 序号 | 文件名 | 语音内容 |
|------|--------|----------|
| 1 | `tamper_alert.ogg` | "检测到异常，请注意安全" |
| 2 | `door_not_closed.ogg` | "门未关闭，请注意关门" |
| 3 | `auth_fail_prefix.ogg` | "认证失败，还剩" |
| 4 | `auth_fail_suffix.ogg` | "次机会" |
| 5 | `locked_prefix.ogg` | "设备已锁定，请" |
| 6 | `locked_suffix.ogg` | "分钟后再试" |
| 7 | `fp_press.ogg` | "请按压手指" |
| 8 | `fp_lift.ogg` | "请抬起手指" |
| 9 | `fp_press_again.ogg` | "请再次按压" |
| 10 | `nfc_tap.ogg` | "请刷卡" |
| 11 | `nfc_tap_again.ogg` | "请再次刷卡" |
| 12 | `enroll_success.ogg` | "录入成功" |
| 13 | `enroll_fail.ogg` | "录入失败，请重试" |
| 14 | `already_exists.ogg` | "该特征已存在" |
| 15 | `id_occupied.ogg` | "指定编号已占用，已自动分配新编号" |

### 复用现有数字语音（10 个）

`0.ogg` ~ `9.ogg`

---

## 九、代码修改清单

### 9.1 已修改的文件

| 文件 | 修改内容 |
|------|----------|
| `main/lock_control/lock_protocol.h` | 新增 `NfcRespStatus` 枚举、`UnlockResult` 枚举、`MAX_AUTH_FAIL_COUNT` 常量 |
| `main/assets/lang_config.h` | 新增 15 个语音资源常量定义 |
| `main/application.h` | 新增 `PlayAuthFailVoice()`、`PlayLockedVoice()`、`PlayNumberVoice()` 方法声明 |
| `main/application.cc` | 修改事件处理逻辑，添加语音播放实现 |

### 9.2 协议变更

**RPT_UNLOCK (0xA1) - 开锁日志**

| 字段 | 值 | 含义 |
|------|-----|------|
| D0 | 0x01-0x07 | 开锁方式 |
| D1 | 用户ID / 剩余锁定时间 | 成功/失败时为用户ID，锁定时为剩余分钟数 |
| D2 | 0x00 | 成功 |
| D2 | 0x01-0x05 | 失败次数 |
| D2 | 0x06 | 已锁定（新增） |

**NFC_RESP (0x21) - NFC 反馈新增中间状态**

| D0 值 | 含义 |
|-------|------|
| 0x01 | 请刷卡（录入中）- 新增 |
| 0x02 | 请再次刷卡 - 新增 |
| 0x03 | 成功 |
| 0x04 | 失败 |
| 0x05 | 数量查询响应 |
| 0x06 | 已存在 |
| 0x07 | ID被占用 |

---

## 十、存放路径

建议将新增语音文件存放在：
```
main/assets/lang_zh/sounds/
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
├── enroll_success.ogg
├── enroll_fail.ogg
├── already_exists.ogg
└── id_occupied.ogg
```

并在 `main/assets/lang_config.h` 中添加对应的常量定义。

**注意**：`nfc_tap_again.ogg` 也需要添加到目录中。

`lang_config.h` 中的常量定义已完成。

---

## 十一、待办事项

1. **添加语音文件**：按照上述清单录制/生成 15 个 OGG 格式语音文件
2. **STM32 协议升级**：
   - RPT_UNLOCK 支持 D2=0x06（已锁定）和 D1=剩余锁定时间
   - NFC_RESP 支持 D0=0x01（请刷卡）和 D0=0x02（请再次刷卡）
3. **编译测试**：添加语音文件后进行完整编译测试

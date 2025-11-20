## 目标
- 以 `main/boards/bread-compact-wifi-s3cam/` 为唯一参考来源，修正并完善已生成文档：硬件接线、项目总览、开发与构建。
- 输出可直接用于面包板 S3CAM 方案的引脚与配置说明，消除之前以 `waveshare-s3-touch-lcd-3.5b` 为基准的错误。

## 差异与配置解读
- 音频：默认 SIMPLEX I2S（`config.h:9-20`）
  - MIC：`WS=GPIO1`、`SCK=GPIO2`、`DIN=GPIO42`
  - SPK：`DOUT=GPIO39`、`BCLK=GPIO40`、`LRCK=GPIO41`
  - 若改为 Duplex：使用 `AUDIO_I2S_GPIO_*` 宏（`config.h:21-28`）
- 摄像头（OV2640，DVP + SCCB，`config.h:37-55`）
  - 数据总线 D0..D7：`11,9,8,10,12,18,17,16`
  - 同步/时钟：`XCLK=15@20MHz`、`PCLK=13`、`VSYNC=6`、`HREF=7`
  - SCCB：`SIOC=5`、`SIOD=4`；`PWDN/RESET=NC`
  - 初始化流程：`compact_wifi_board_s3cam.cc:128-171`
- 显示（SPI3，`config.h:57-63`，`compact_wifi_board_s3cam.cc:71-126`）
  - 背光：`GPIO38`
  - MOSI/CLK：`GPIO20/19`（占用 USB 19/20）
  - DC/RST/CS：`GPIO47/21/45`
  - 面板型号通过 `CONFIG_LCD_*` 选择（`config.h:65-300`），默认走 ST7789/ILI9341/GC9A01 分支，SPI 模式与分辨率由宏控制。
- 按键/LED：`BOOT=GPIO0`、`LED=GPIO48`（`config.h:31-35`）

## 修订范围
1. 更新 `docs/硬件接线-面包板摄像头.md`
   - 用 bread-compact-wifi-s3cam 的引脚表替换摄像头/显示/音频章节；新增“USB 19/20 被显示占用”提示；补充 SCCB 引脚与 SIMPLEX/DUPEX 说明。
   - 更新源码定位：`main/boards/bread-compact-wifi-s3cam/config.h` 与 `compact_wifi_board_s3cam.cc` 的行号。
2. 更新 `docs/项目总览.md`
   - 板级示例改为 `bread-compact-wifi-s3cam`；更新板级初始化与接口说明的路径与行号。
3. 更新 `docs/开发与构建.md`
   - 在 `menuconfig` 板型选择处补充本板 README 指引：“面包板新版接线（WiFi）+ LCD + Camera”；
   - 说明 `CONFIG_LCD_*` 的选择项与常见面板（ST7789/ILI9341/GC9A01），以及 SIMPLEX/DUPEX 的切换。

## 交付内容
- 三个文档被修订并生效，所有引脚、代码路径与配置项均与 `bread-compact-wifi-s3cam` 一致。
- 在 `README.md` 的开发者文档链接不变，如需强调本板型，追加一句提示（可选）。

## 验证
- 编译目标 `esp32s3`，在 `menuconfig` 选择“面包板新版接线（WiFi）+ LCD + Camera”，并选择对应 `CONFIG_LCD_*`。
- 监视串口：观察显示初始化、摄像头初始化与 I2S 任务启动日志；如显示不亮，检查 MOSI/CLK 与 DC/CS/RST；如摄像头无图，核对 SCCB 与 XCLK 频率。

## 备注
- 若您的 docx 有不同接线，请在 `config.h` 替换为您的引脚并保持相同宏名；文档中将保留“推荐（源码）/自定义（教程）”两列展示。
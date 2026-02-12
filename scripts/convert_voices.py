#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
语音文件批量转换脚本
将 MP3 文件转换为 OGG (Opus 编码) 格式，用于嵌入 ESP32 固件
"""

import subprocess
import os
import sys

# 文件名映射（中文名 -> 英文名）
FILE_MAPPING = {
    "检测到异常，请注意安全.mp3": "tamper_alert.ogg",
    "门未关闭，请注意关门.mp3": "door_not_closed.ogg",
    "认证失败，还剩.mp3": "auth_fail_prefix.ogg",
    "次机会.mp3": "auth_fail_suffix.ogg",
    "设备已锁定，请.mp3": "locked_prefix.ogg",
    "分钟后再试.mp3": "locked_suffix.ogg",
    "请按压手指.mp3": "fp_press.ogg",
    "请抬起手指.mp3": "fp_lift.ogg",
    "请再次按压.mp3": "fp_press_again.ogg",
    "请刷卡.mp3": "nfc_tap.ogg",
    "请再次刷卡.mp3": "nfc_tap_again.ogg",
    "录入成功.mp3": "enroll_success.ogg",
    "录入失败，请重试.mp3": "enroll_fail.ogg",
    "该特征已存在.mp3": "already_exists.ogg",
    "指定编号已占用，已自动分配新编号.mp3": "id_occupied.ogg",
}

def check_ffmpeg():
    """检查 FFmpeg 是否安装"""
    try:
        subprocess.run(["ffmpeg", "-version"], 
                      stdout=subprocess.DEVNULL, 
                      stderr=subprocess.DEVNULL, 
                      check=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False

def convert_to_ogg(input_file, output_file):
    """
    转换 MP3 为 OGG (Opus 编码)
    参数：
    - 编码器：libopus
    - 比特率：32kbps (适合语音)
    - 采样率：16kHz 输入 → 48kHz 输出 (与项目原有语音保持一致)
    - 声道：单声道
    - 帧时长：60ms (与项目原有语音保持一致)
    
    注意：使用 16kHz 输入采样率，让 OpusHead 中的采样率字段为 16000，
    与原有数字语音保持一致，避免触发不必要的重采样。
    """
    cmd = [
        "ffmpeg", "-y",  # 覆盖已存在的文件
        "-i", input_file,
        "-ar", "16000",     # 16kHz 输入采样率（关键：让 OpusHead 字段为 16000）
        "-c:a", "libopus",  # Opus 编码器
        "-b:a", "32k",      # 32kbps 比特率
        "-ac", "1",         # 单声道
        "-frame_duration", "60",  # 60ms 帧时长（与原有语音一致）
        output_file
    ]
    
    try:
        subprocess.run(cmd, check=True, 
                      stdout=subprocess.DEVNULL, 
                      stderr=subprocess.PIPE)
        return True
    except subprocess.CalledProcessError as e:
        print(f"  ✗ 转换失败: {e.stderr.decode('utf-8', errors='ignore')}")
        return False

def main():
    # 检查 FFmpeg
    if not check_ffmpeg():
        print("错误：未找到 FFmpeg！")
        print("请安装 FFmpeg: https://ffmpeg.org/download.html")
        print("或使用包管理器安装：")
        print("  - Windows: choco install ffmpeg")
        print("  - macOS: brew install ffmpeg")
        print("  - Linux: sudo apt install ffmpeg")
        sys.exit(1)
    
    # 目录设置
    input_dir = "voice"
    output_dir = "main/assets/locales/zh-CN"
    
    if not os.path.exists(input_dir):
        print(f"错误：输入目录不存在: {input_dir}")
        sys.exit(1)
    
    if not os.path.exists(output_dir):
        print(f"错误：输出目录不存在: {output_dir}")
        print(f"请确保项目结构正确")
        sys.exit(1)
    
    print("=" * 60)
    print("语音文件批量转换")
    print("=" * 60)
    print(f"输入目录: {input_dir}")
    print(f"输出目录: {output_dir}")
    print(f"格式: MP3 → OGG (Opus 32kbps, 16kHz input, Mono, 60ms frame)")
    print("=" * 60)
    print()
    
    # 统计
    success_count = 0
    fail_count = 0
    total_input_size = 0
    total_output_size = 0
    
    # 转换文件
    for mp3_name, ogg_name in FILE_MAPPING.items():
        input_path = os.path.join(input_dir, mp3_name)
        output_path = os.path.join(output_dir, ogg_name)
        
        if not os.path.exists(input_path):
            print(f"⚠ 跳过: {mp3_name} (文件不存在)")
            fail_count += 1
            continue
        
        input_size = os.path.getsize(input_path)
        total_input_size += input_size
        
        print(f"转换: {mp3_name}")
        print(f"  → {ogg_name} ... ", end="", flush=True)
        
        if convert_to_ogg(input_path, output_path):
            output_size = os.path.getsize(output_path)
            total_output_size += output_size
            compression_ratio = (1 - output_size / input_size) * 100
            
            print(f"✓ ({input_size:,} B → {output_size:,} B, 压缩 {compression_ratio:.1f}%)")
            success_count += 1
        else:
            fail_count += 1
    
    # 总结
    print()
    print("=" * 60)
    print("转换完成")
    print("=" * 60)
    print(f"成功: {success_count} 个文件")
    print(f"失败: {fail_count} 个文件")
    print(f"总输入大小: {total_input_size:,} B ({total_input_size / 1024:.1f} KB)")
    print(f"总输出大小: {total_output_size:,} B ({total_output_size / 1024:.1f} KB)")
    
    if total_input_size > 0:
        total_compression = (1 - total_output_size / total_input_size) * 100
        print(f"总压缩率: {total_compression:.1f}%")
    
    print("=" * 60)
    
    if success_count > 0:
        print()
        print("下一步操作：")
        print("1. 更新 main/assets/lang_config.h 添加语音常量定义")
        print("2. 取消 main/application.cc 中的播放代码注释")
        print("3. 执行 idf.py build 编译")

if __name__ == "__main__":
    main()

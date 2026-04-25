# Game Profiles 游戏配置

此目录包含各个 DOS 游戏的字幕提取配置文件。

## 配置文件格式 (.ini)

### 基本结构

```ini
[Game Info]
Title=游戏名称
Year=1991
CodePageFrom=cp437
CodePageTo=utf-8

[Subtitle Addresses]
; DOS 内存地址（必填）
text_address=0x1A000
; 可选：字幕显示时长地址
duration_address=0x1A050
; 可选：字幕状态标志地址
status_address=0x1A0A0

[Subtitle Format]
; 字幕结束符（通常是零）
terminator=0x00
; 最大字幕长度
max_length=256

[Rendering]
position_x=640
position_y=460
width=1280
height=100
font_size=14
color=0xFFFFFFFF
background_color=0x00000080
# DOSBox-X Subtitle Extractor Tool

一个使用 Game Link 协议从 DOS 游戏中实时提取字幕的工具。

## 功能特性

- ✅ 通过 Game Link 从 DOS 程序内存读取字幕数据
- ✅ 实时字幕显示和同步
- ✅ 支持多种字幕格式导出 (SRT, ASS, JSON)
- ✅ DOS 代码页自动转换 (CP437 -> UTF-8)
- ✅ 游戏配置文件支持
- ✅ 跨平台支持 (Windows/Linux/macOS)

## 项目结构
dosbox-x-subtitle-extractor/ ├── include/ # 头文件 │ ├── gamelink_interface.h # Game Link 共享内存接口 │ ├── subtitle_extractor.h # 字幕提取器 │ ├── subtitle_renderer.h # 字幕渲染器 │ ├── memory_scanner.h # 内存扫描器 │ └── subtitle_format.h # 字幕格式处理 ├── src/ # 源文件 │ ├── gamelink_interface.cpp # Game Link 实现 │ ├── subtitle_extractor.cpp # 字幕提取实现 │ ├── subtitle_renderer.cpp # 字幕渲染实现 │ ├── memory_scanner.cpp # 内存扫描实现 │ ├── subtitle_format.cpp # 字幕格式实现 │ └── main.cpp # 主程序 ├── game_profiles/ # 游戏配置文件 │ ├── monkey_island_2.ini # Monkey Island 2 配置示例 │ ├── loom.ini # Loom 配置示例 │ └── README.md # 配置指南 ├── CMakeLists.txt # CMake 构建配置 ├── .gitignore └── README.md # 本文件

Code

## 快速开始

### 构建

```bash
mkdir build && cd build
cmake .. && make
使用
bash
./subtitle_extractor --game "Monkey Island 2" --output subtitles.srt --display
详细文档
见各源文件的注释和 game_profiles/ 目录。n
# DOSBox-X Subtitle Extractor

A real-time game subtitle extraction and translation tool using OCR and Game Link protocol.

## Features

- **OCR Mode**: Real-time text recognition using macOS Vision framework
- **Web Remote Control**: Browser-based interface for remote gameplay and subtitle viewing
- **Multi-language Translation**: Automatic translation via Ollama (supports Chinese, English, Japanese, Korean, Spanish, French, German, Russian)
- **Game Link Mode**: Direct memory access for subtitle extraction
- **Subtitle Export**: Support for SRT, ASS, VTT, JSON formats

## Quick Start

### Prerequisites

- macOS (for OCR mode with Vision framework)
- DOSBox-X with Game Link support
- Ollama with a translation model (e.g., `gemma4:26b`)

### Build

```bash
mkdir build && cd build
cmake .. && make -j4
```

### Run OCR Mode with Web Interface

```bash
./subtitle_extractor --web --web-port 9090 --ocr-continuous
```

Then open `http://localhost:9090` in your browser.

## DOSBox-X Game Link Configuration

Add these settings to your DOSBox-X configuration file (e.g., `dosbox-x.conf`):

```ini
[sdl]
fullscreen = false
output = gamelink
gamelink master = true
```

This enables Game Link protocol for:
- Real-time frame buffer access
- Remote keyboard/mouse input
- OCR region selection

## OCR Mode Usage

### Web Interface Features

1. **OCR Region Selection**
   - Click "Select OCR Region" button
   - Drag on the game screen to select the text area
   - Only the selected region will be processed for OCR
   - Region is saved automatically and restored on page refresh

2. **Translation Box**
   - Drag to move position
   - Resize from bottom-right corner (orange triangle)
   - Position and size are saved automatically

3. **Language Selection**
   - Choose target language from dropdown (Chinese, English, Japanese, etc.)
   - Selection is saved automatically
   - Translation uses recent OCR results as context for better accuracy

### OCR Mode Options

```bash
# Web remote control with OCR
./subtitle_extractor --web --web-port 9090 --ocr-continuous

# Standalone OCR mode
./subtitle_extractor --ocr --ocr-continuous --ocr-interval 1000 --ocr-min-confidence 0.5

# Export subtitles to file
./subtitle_extractor --ocr --ocr-continuous --output subtitles.srt
```

### OCR Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `--ocr` | Enable OCR mode | - |
| `--ocr-continuous` | Continuously monitor screen | false |
| `--ocr-interval <ms>` | OCR interval in milliseconds | 1000 |
| `--ocr-min-confidence <f>` | Minimum confidence threshold | 0.5 |

## Scan Mode (Memory Scanning)

Experimental feature for searching text patterns in game memory:

```bash
./subtitle_extractor --scan --scan-continuous --scan-charset japanese
```

| Parameter | Description |
|-----------|-------------|
| `--scan` | Enable memory scan mode |
| `--scan-start <addr>` | Scan start address (hex) |
| `--scan-end <addr>` | Scan end address (hex) |
| `--scan-interval <ms>` | Scan interval |
| `--scan-min-len <n>` | Minimum string length |
| `--scan-charset <type>` | Character set: japanese, ascii, all |
| `--scan-text <text>` | Search for specific text pattern |
| `--scan-offset <addr>` | Memory offset (hex) |

## Web Remote API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Web interface HTML |
| `/frame` | GET | Get frame buffer (ARGB data) |
| `/ocr` | GET | Get latest OCR result |
| `/region` | POST | Set OCR region `{x, y, width, height}` |
| `/input` | POST | Send keyboard input `{keys: [...]}` |

## Translation Setup

1. Install Ollama: https://ollama.ai
2. Pull a translation model:
   ```bash
   ollama pull gemma4:26b
   ```
3. Ensure Ollama is running on `localhost:11434`

## Project Structure

```
dosbox-x-subtitle-extractor/
├── include/
│   ├── gamelink_interface.h
│   ├── subtitle_extractor.h
│   ├── subtitle_format.h
│   ├── ocr_bridge.h
│   └── web_server.h
├── src/
│   ├── main.cpp
│   ├── gamelink_interface.cpp
│   ├── subtitle_extractor.cpp
│   ├── subtitle_format.cpp
│   ├── ocr_bridge.mm
│   └── web_server.cpp
├── resources/
│   └── web_remote.html
├── game_profiles/
├── CMakeLists.txt
└── README.md
```

## License

MIT License
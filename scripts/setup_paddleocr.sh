#!/bin/bash
# Setup script for PaddleOCR C++ inference
# Downloads Paddle Inference library and PP-OCRv5 models

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== PaddleOCR Setup Script ==="
echo "Project directory: $PROJECT_DIR"

# 1. Download Paddle Inference library for macOS M1
echo ""
echo "[1] Downloading Paddle Inference library..."
PADDLE_INFER_DIR="$PROJECT_DIR/paddle_inference"

if [ ! -d "$PADDLE_INFER_DIR" ]; then
    mkdir -p "$PADDLE_INFER_DIR"
    cd "$PADDLE_INFER_DIR"

    echo "Downloading from: https://paddle-inference-lib.bj.bcebos.com/3.0.0/cxx_c/MacOS/m1_clang_noavx_accelerate_blas/paddle_inference.tgz"
    curl -L "https://paddle-inference-lib.bj.bcebos.com/3.0.0/cxx_c/MacOS/m1_clang_noavx_accelerate_blas/paddle_inference.tgz" -o paddle_inference.tgz

    echo "Extracting..."
    tar -xzf paddle_inference.tgz

    echo "Paddle Inference library downloaded successfully"
else
    echo "Paddle Inference library already exists, skipping"
fi

# 2. Download PP-OCRv5 models
echo ""
echo "[2] Downloading PP-OCRv5 models..."
MODELS_DIR="$PROJECT_DIR/models"

mkdir -p "$MODELS_DIR"
cd "$MODELS_DIR"

# Detection model
if [ ! -d "PP-OCRv5_server_det_infer" ]; then
    echo "Downloading PP-OCRv5 detection model..."
    curl -L "https://paddle-model-ecology.bj.bcebos.com/paddlex/official_inference_model/paddle3.0.0/PP-OCRv5_server_det_infer.tar" -o det.tar
    tar -xf det.tar
    echo "Detection model downloaded"
else
    echo "Detection model already exists, skipping"
fi

# Recognition model
if [ ! -d "PP-OCRv5_server_rec_infer" ]; then
    echo "Downloading PP-OCRv5 recognition model..."
    curl -L "https://paddle-model-ecology.bj.bcebos.com/paddlex/official_inference_model/paddle3.0.0/PP-OCRv5_server_rec_infer.tar" -o rec.tar
    tar -xf rec.tar
    echo "Recognition model downloaded"
else
    echo "Recognition model already exists, skipping"
fi

# 3. Extract character dictionary from recognition model config
echo ""
echo "[3] Extracting character dictionary..."
if [ -f "PP-OCRv5_server_rec_infer/inference.yml" ]; then
    python3 -c "
import yaml
with open('PP-OCRv5_server_rec_infer/inference.yml', 'r') as f:
    data = yaml.safe_load(f)

char_dict = data.get('PostProcess', {}).get('character_dict', [])
print(f'Character dict size: {len(char_dict)}')

with open('ppocr_keys_v1.txt', 'w') as f:
    for char in char_dict:
        f.write(char + '\n')
print('Dictionary saved to ppocr_keys_v1.txt')
"
    echo "Character dictionary extracted from inference.yml"
else
    echo "Warning: inference.yml not found, dictionary extraction skipped"
fi

# 4. Cleanup
echo ""
echo "[4] Cleanup..."
cd "$MODELS_DIR"
rm -f det.tar rec.tar 2>/dev/null || true
cd "$PADDLE_INFER_DIR"
rm -f paddle_inference.tgz 2>/dev/null || true

echo ""
echo "=== Setup Complete ==="
echo ""
echo "Directory structure:"
ls -la "$PADDLE_INFER_DIR/paddle_inference_install_dir/paddle/lib/" 2>/dev/null | head -5 || echo "Paddle lib not found"
ls -la "$MODELS_DIR/" 2>/dev/null | head -10 || echo "Models not found"

echo ""
echo "To build the project:"
echo "  cd $PROJECT_DIR && mkdir -p build && cd build && cmake .. && make -j4"
echo ""
echo "To run tests:"
echo "  cd $PROJECT_DIR/tests && mkdir -p build && cd build && cmake .. && make -j4 && ./test_ocr"
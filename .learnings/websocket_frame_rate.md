# WebSocket Video Frame Rate Issue Resolution

## Problem
WebSocket binary frame transmission could not achieve stable 30 FPS:
- Original approach: sending raw BGRA frames (1.2MB each)
- Result: only 0.1-1.4 FPS
- Root cause: libwebsockets writable callback mechanism not suited for high-frequency large-frame transmission

## Solution: JPEG Compression

### Implementation
1. Compress BGRA frames to JPEG using OpenCV `imencode`
2. Message format: type(0x02) + width(2B) + height(2B) + JPEG data
3. Compression ratio: 96% (1.2MB -> ~57KB)
4. JPEG quality: 80%

### Key Code Changes
```cpp
// BroadcastFrame: compress to JPEG
cv::Mat mat(height, width, CV_8UC4, (void*)frame_data.data());
cv::Mat bgr;
cv::cvtColor(mat, bgr, cv::COLOR_BGRA2BGR);
std::vector<uint8_t> jpeg_data;
cv::imencode(".jpg", bgr, jpeg_data, {cv::IMWRITE_JPEG_QUALITY, 80});
```

```javascript
// Frontend: decode JPEG
const jpegData = new Uint8Array(data, 5);
const blob = new Blob([jpegData], {type: 'image/jpeg'});
const img = new Image();
img.onload = () => ctx.drawImage(img, 0, 0);
img.src = URL.createObjectURL(blob);
```

### libwebsockets Optimization
- Always call `lws_callback_on_writable(wsi)` in writable callback
- Call `lws_service(m_context, 5)` immediately after triggering writable
- This maintains continuous send loop at ~30 FPS

## Results
- Frame rate: **29.7 FPS** (stable)
- Latency: 33.6ms
- Frame size: 57KB (96% reduction)

## Alternative Considered
WebRTC was considered but JPEG compression over WebSocket proved sufficient:
- Simpler implementation (no new dependencies)
- Already have OpenCV for compression
- Works well with 57KB frames
- Browser natively supports JPEG decoding

## Date
2025-05-01
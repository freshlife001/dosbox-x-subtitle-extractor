#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Subtitle {

// ============================================================================
// 字幕项
// ============================================================================

struct SubtitleItem {
    uint32_t id;
    uint32_t start_ms;          // 开始时间 (毫秒)
    uint32_t end_ms;            // 结束时间 (毫秒)
    std::string text;           // 字幕文本
    std::string speaker;        // 发言人 (可选)
};

// ============================================================================
// 字幕文件格式处理
// ============================================================================

class SubtitleFile {
public:
    SubtitleFile();
    ~SubtitleFile();
    
    /// 添加字幕项
    void AddSubtitle(
        uint32_t start_ms,
        uint32_t end_ms,
        const std::string& text,
        const std::string& speaker = ""
    );
    
    /// 清空所有字幕
    void Clear();
    
    /// 导出为 SRT 格式
    /// @param output_path 输出文件路径
    /// @return 成功返回 true
    bool ExportToSRT(const std::string& output_path) const;
    
    /// 导出为 ASS 格式 (Advanced SubStation Alpha)
    /// @param output_path 输出文件路径
    /// @return 成功返回 true
    bool ExportToASS(const std::string& output_path) const;
    
    /// 导出为 VTT 格式 (WebVTT)
    /// @param output_path 输出文件路径
    /// @return 成功返回 true
    bool ExportToVTT(const std::string& output_path) const;
    
    /// 导出为 JSON 格式
    /// @param output_path 输出文件路径
    /// @return 成功返回 true
    bool ExportToJSON(const std::string& output_path) const;
    
    /// 导入 SRT 文件
    bool ImportFromSRT(const std::string& input_path);
    
    /// 获取字幕数量
    size_t GetSubtitleCount() const;
    
    /// 获取指定索引的字幕
    const SubtitleItem* GetSubtitle(size_t index) const;
    
private:
    std::vector<SubtitleItem> m_subtitles;
    std::string m_gameTitle;
    
    // 格式转换辅助函数
    static std::string TimeToSRTFormat(uint32_t ms);
    static std::string TimeToASSFormat(uint32_t ms);
    static std::string TimeToVTTFormat(uint32_t ms);
    static std::string EscapeJSON(const std::string& text);
    static uint32_t SRTTimeToMs(const std::string& time);
};

} // namespace Subtitle

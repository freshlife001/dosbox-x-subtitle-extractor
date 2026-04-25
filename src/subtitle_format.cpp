#include "../include/subtitle_format.h"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace Subtitle {

SubtitleFile::SubtitleFile()
    : m_subtitles()
{
}

SubtitleFile::~SubtitleFile() {
}

void SubtitleFile::AddSubtitle(
    uint32_t start_ms,
    uint32_t end_ms,
    const std::string& text,
    const std::string& speaker
) {
    SubtitleItem item;
    item.id = static_cast<uint32_t>(m_subtitles.size() + 1);
    item.start_ms = start_ms;
    item.end_ms = end_ms;
    item.text = text;
    item.speaker = speaker;
    
    m_subtitles.push_back(item);
}

void SubtitleFile::Clear() {
    m_subtitles.clear();
}

bool SubtitleFile::ExportToSRT(const std::string& output_path) const {
    std::ofstream file(output_path, std::ios::binary);
    if (!file.is_open()) return false;
    
    for (const auto& subtitle : m_subtitles) {
        file << subtitle.id << "\r\n";
        file << TimeToSRTFormat(subtitle.start_ms) << " --> "
             << TimeToSRTFormat(subtitle.end_ms) << "\r\n";
        
        if (!subtitle.speaker.empty()) {
            file << "<v " << subtitle.speaker << ">";
        }
        
        file << subtitle.text << "\r\n\r\n";
    }
    
    file.close();
    return true;
}

bool SubtitleFile::ExportToASS(const std::string& output_path) const {
    std::ofstream file(output_path, std::ios::binary);
    if (!file.is_open()) return false;
    
    // ASS 头
    file << "[Script Info]\r\n";
    file << "Title: " << m_gameTitle << " Subtitles\r\n";
    file << "ScriptType: v4.00+\r\n";
    file << "Collisions: Normal\r\n";
    file << "PlayResX: 1280\r\n";
    file << "PlayResY: 720\r\n";
    file << "\r\n";
    
    // 样式
    file << "[V4+ Styles]\r\n";
    file << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
         << "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
         << "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
         << "Alignment, MarginL, MarginR, MarginV, Encoding\r\n";
    file << "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,"
         << "0,0,0,0,100,100,0,0,1,0,0,2,0,0,0,1\r\n";
    file << "\r\n";
    
    // 事件
    file << "[Events]\r\n";
    file << "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";
    
    for (const auto& subtitle : m_subtitles) {
        file << "Dialogue: 0," << TimeToASSFormat(subtitle.start_ms) << ","
             << TimeToASSFormat(subtitle.end_ms) << ",Default,"
             << (!subtitle.speaker.empty() ? subtitle.speaker : "") << ",0,0,0,,"
             << subtitle.text << "\r\n";
    }
    
    file.close();
    return true;
}

bool SubtitleFile::ExportToVTT(const std::string& output_path) const {
    std::ofstream file(output_path, std::ios::binary);
    if (!file.is_open()) return false;
    
    file << "WEBVTT\r\n\r\n";
    
    for (const auto& subtitle : m_subtitles) {
        file << TimeToVTTFormat(subtitle.start_ms) << " --> "
             << TimeToVTTFormat(subtitle.end_ms) << "\r\n";
        file << subtitle.text << "\r\n\r\n";
    }
    
    file.close();
    return true;
}

bool SubtitleFile::ExportToJSON(const std::string& output_path) const {
    std::ofstream file(output_path, std::ios::binary);
    if (!file.is_open()) return false;
    
    file << "{\r\n";
    file << "  \"metadata\": {\r\n";
    file << "    \"game\": \"" << m_gameTitle << "\",\r\n";
    file << "    \"encoding\": \"utf-8\",\r\n";
    file << "    \"subtitle_count\": " << m_subtitles.size() << "\r\n";
    file << "  },\r\n";
    file << "  \"subtitles\": [\r\n";
    
    for (size_t i = 0; i < m_subtitles.size(); ++i) {
        const auto& subtitle = m_subtitles[i];
        
        file << "    {\r\n";
        file << "      \"id\": " << subtitle.id << ",\r\n";
        file << "      \"start_ms\": " << subtitle.start_ms << ",\r\n";
        file << "      \"end_ms\": " << subtitle.end_ms << ",\r\n";
        file << "      \"text\": \"" << EscapeJSON(subtitle.text) << "\"";
        
        if (!subtitle.speaker.empty()) {
            file << ",\r\n      \"speaker\": \"" << subtitle.speaker << "\"";
        }
        
        file << "\r\n    }";
        
        if (i < m_subtitles.size() - 1) {
            file << ",";
        }
        file << "\r\n";
    }
    
    file << "  ]\r\n";
    file << "}\r\n";
    
    file.close();
    return true;
}

bool SubtitleFile::ImportFromSRT(const std::string& input_path) {
    std::ifstream file(input_path);
    if (!file.is_open()) return false;
    
    std::string line;
    uint32_t id = 0;
    uint32_t start_ms = 0;
    uint32_t end_ms = 0;
    std::string text;
    
    enum { ID, TIME, TEXT } state = ID;
    
    while (std::getline(file, line)) {
        // 移除 \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) {
            if (state == TEXT && !text.empty()) {
                AddSubtitle(start_ms, end_ms, text);
                text.clear();
                state = ID;
            }
            continue;
        }
        
        if (state == ID) {
            try {
                id = std::stoul(line);
                state = TIME;
            } catch (...) {}
        } else if (state == TIME) {
            size_t arrow = line.find(" --> ");
            if (arrow != std::string::npos) {
                std::string start_str = line.substr(0, arrow);
                std::string end_str = line.substr(arrow + 5);
                
                start_ms = SRTTimeToMs(start_str);
                end_ms = SRTTimeToMs(end_str);
                state = TEXT;
            }
        } else if (state == TEXT) {
            if (!text.empty()) text += "\n";
            text += line;
        }
    }
    
    file.close();
    return true;
}

size_t SubtitleFile::GetSubtitleCount() const {
    return m_subtitles.size();
}

const SubtitleItem* SubtitleFile::GetSubtitle(size_t index) const {
    if (index >= m_subtitles.size()) return nullptr;
    return &m_subtitles[index];
}

std::string SubtitleFile::TimeToSRTFormat(uint32_t ms) {
    uint32_t hours = ms / (3600 * 1000);
    uint32_t minutes = (ms % (3600 * 1000)) / (60 * 1000);
    uint32_t seconds = (ms % (60 * 1000)) / 1000;
    uint32_t millis = ms % 1000;
    
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << hours << ":"
        << std::setw(2) << minutes << ":"
        << std::setw(2) << seconds << ","
        << std::setw(3) << millis;
    return oss.str();
}

std::string SubtitleFile::TimeToASSFormat(uint32_t ms) {
    uint32_t hours = ms / (3600 * 1000);
    uint32_t minutes = (ms % (3600 * 1000)) / (60 * 1000);
    uint32_t seconds = (ms % (60 * 1000)) / 1000;
    uint32_t centisecs = (ms % 1000) / 10;
    
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(1) << hours << ":"
        << std::setw(2) << minutes << ":"
        << std::setw(2) << seconds << "."
        << std::setw(2) << centisecs;
    return oss.str();
}

std::string SubtitleFile::TimeToVTTFormat(uint32_t ms) {
    return TimeToSRTFormat(ms);  // VTT 使用相同的格式
}

std::string SubtitleFile::EscapeJSON(const std::string& text) {
    std::string result;
    for (char ch : text) {
        switch (ch) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (ch < 32) {
                    std::ostringstream oss;
                    oss << "\\u" << std::hex << std::setfill('0') << std::setw(4)
                        << static_cast<int>(ch);
                    result += oss.str();
                } else {
                    result += ch;
                }
        }
    }
    return result;
}

uint32_t SubtitleFile::SRTTimeToMs(const std::string& time) {
    // Format: HH:MM:SS,mmm
    uint32_t hours = 0, minutes = 0, seconds = 0, millis = 0;
    
    try {
        size_t pos = 0;
        hours = std::stoul(time.substr(pos), &pos);
        minutes = std::stoul(time.substr(pos + 1), &pos);
        seconds = std::stoul(time.substr(pos + 1), &pos);
        millis = std::stoul(time.substr(pos + 1));
    } catch (...) {}
    
    return hours * 3600000 + minutes * 60000 + seconds * 1000 + millis;
}

}  // namespace Subtitle

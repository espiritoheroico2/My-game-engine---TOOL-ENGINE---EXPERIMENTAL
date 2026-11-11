#include "SpriteUi/Config/UIConfig.hpp"

#include <charconv>
#include <cstdio>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace ToolEngine::SpriteUi::Config {

namespace {

constexpr std::size_t MaxNestingDepth = 32;
constexpr std::size_t MaxWindowCount = 1024;
constexpr std::size_t MaxNameBytes = 256;
constexpr float CoordinateLimit = 100000.0f;

// Parser JSON minimalista e estrito (sem dependencias): so le, nunca escreve no destino.
class JsonCursor {
public:
    explicit JsonCursor(std::string_view text) noexcept : s_{text} {}

    void SkipWs() noexcept {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' || s_[pos_] == '\n')) {
            ++pos_;
        }
    }

    [[nodiscard]] bool Consume(char c) noexcept {
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    [[nodiscard]] bool Peek(char c) noexcept {
        SkipWs();
        return pos_ < s_.size() && s_[pos_] == c;
    }

    [[nodiscard]] bool AtEnd() noexcept { SkipWs(); return pos_ >= s_.size(); }

    [[nodiscard]] std::size_t Position() const noexcept { return pos_; }
    void Restore(std::size_t pos) noexcept { pos_ = pos; }

    [[nodiscard]] std::optional<std::string> ParseString() {
        SkipWs();
        if (pos_ >= s_.size() || s_[pos_] != '"') return std::nullopt;
        ++pos_;
        std::string out;
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
            if (c != '\\') { out.push_back(c); continue; }
            if (pos_ >= s_.size()) return std::nullopt;
            const char e = s_[pos_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (pos_ + 4 > s_.size()) return std::nullopt;
                unsigned code = 0;
                const auto [ptr, ec] = std::from_chars(s_.data() + pos_, s_.data() + pos_ + 4, code, 16);
                if (ec != std::errc() || ptr != s_.data() + pos_ + 4) return std::nullopt;
                pos_ += 4;
                if (code >= 0xD800 && code <= 0xDFFF) return std::nullopt; // Surrogates nao suportados.
                AppendUtf8(out, code);
                break;
            }
            default: return std::nullopt;
            }
        }
        return std::nullopt; // String nao terminada.
    }

    [[nodiscard]] std::optional<double> ParseNumber() {
        SkipWs();
        const std::size_t start = pos_;
        std::size_t p = pos_;
        if (p < s_.size() && s_[p] == '-') ++p;
        while (p < s_.size() && ((s_[p] >= '0' && s_[p] <= '9') || s_[p] == '.' ||
                                 s_[p] == 'e' || s_[p] == 'E' || s_[p] == '+' || s_[p] == '-')) {
            ++p;
        }
        if (p == start) return std::nullopt;
        double value = 0.0;
        const auto [ptr, ec] = std::from_chars(s_.data() + start, s_.data() + p, value);
        if (ec != std::errc() || ptr != s_.data() + p) return std::nullopt;
        pos_ = p;
        return value;
    }

    [[nodiscard]] std::optional<std::int64_t> ParseInteger() {
        SkipWs();
        const std::size_t start = pos_;
        std::size_t p = pos_;
        if (p < s_.size() && s_[p] == '-') ++p;
        const std::size_t digits = p;
        while (p < s_.size() && s_[p] >= '0' && s_[p] <= '9') ++p;
        if (p == digits) return std::nullopt;
        if (p < s_.size() && (s_[p] == '.' || s_[p] == 'e' || s_[p] == 'E')) return std::nullopt;
        std::int64_t value = 0;
        const auto [ptr, ec] = std::from_chars(s_.data() + start, s_.data() + p, value);
        if (ec != std::errc() || ptr != s_.data() + p) return std::nullopt;
        pos_ = p;
        return value;
    }

    [[nodiscard]] std::optional<bool> ParseBool() noexcept {
        SkipWs();
        if (s_.substr(pos_, 4) == "true") { pos_ += 4; return true; }
        if (s_.substr(pos_, 5) == "false") { pos_ += 5; return false; }
        return std::nullopt;
    }

    // Consome qualquer valor JSON (usado para chaves desconhecidas e tipos inesperados).
    [[nodiscard]] bool SkipValue(std::size_t depth = 0) {
        if (depth > MaxNestingDepth) return false;
        SkipWs();
        if (pos_ >= s_.size()) return false;
        const char c = s_[pos_];
        if (c == '"') return ParseString().has_value();
        if (c == '{') {
            ++pos_;
            if (Consume('}')) return true;
            do {
                if (!ParseString() || !Consume(':') || !SkipValue(depth + 1)) return false;
            } while (Consume(','));
            return Consume('}');
        }
        if (c == '[') {
            ++pos_;
            if (Consume(']')) return true;
            do {
                if (!SkipValue(depth + 1)) return false;
            } while (Consume(','));
            return Consume(']');
        }
        if (ParseBool()) return true;
        if (s_.substr(pos_, 4) == "null") { pos_ += 4; return true; }
        return ParseNumber().has_value();
    }

private:
    static void AppendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    std::string_view s_;
    std::size_t pos_ = 0;
};

// Trunca em fronteira de codepoint UTF-8 para nao deixar bytes cortados.
std::string TruncateUtf8(std::string value, std::size_t maxBytes) {
    if (value.size() <= maxBytes) return value;
    std::size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(value[cut]) & 0xC0) == 0x80) --cut;
    value.resize(cut);
    return value;
}

float ReadFloatOr(JsonCursor& json, float fallback, float lo, float hi) {
    const std::size_t mark = json.Position();
    if (const auto number = json.ParseNumber()) {
        return ReadFiniteFloat(static_cast<float>(*number), fallback, lo, hi);
    }
    json.Restore(mark);
    return fallback;
}

std::string EscapeJson(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buffer;
            } else {
                out.push_back(c);
            }
        }
    }
    return out;
}

// Le um objeto de janela; devolve false se a estrutura for invalida.
bool ParseWindow(JsonCursor& json, UIWindowConfig& win) {
    if (!json.Consume('{')) return false;
    if (json.Consume('}')) return true;
    do {
        const auto key = json.ParseString();
        if (!key || !json.Consume(':')) return false;

        if (*key == "name") {
            const std::size_t mark = json.Position();
            if (auto name = json.ParseString()) {
                win.name = TruncateUtf8(std::move(*name), MaxNameBytes);
            } else {
                json.Restore(mark);
                if (!json.SkipValue()) return false;
            }
        } else if (*key == "offsetMinX") {
            win.offsetMin.x = ReadFloatOr(json, 0.0f, -CoordinateLimit, CoordinateLimit);
        } else if (*key == "offsetMinY") {
            win.offsetMin.y = ReadFloatOr(json, 0.0f, -CoordinateLimit, CoordinateLimit);
        } else if (*key == "offsetMaxX") {
            win.offsetMax.x = ReadFloatOr(json, 150.0f, -CoordinateLimit, CoordinateLimit);
        } else if (*key == "offsetMaxY") {
            win.offsetMax.y = ReadFloatOr(json, 100.0f, -CoordinateLimit, CoordinateLimit);
        } else if (*key == "isMinimized") {
            const std::size_t mark = json.Position();
            if (const auto b = json.ParseBool()) {
                win.isMinimized = *b;
            } else {
                json.Restore(mark);
                if (!json.SkipValue()) return false;
            }
        } else if (!json.SkipValue()) {
            return false; // Chave desconhecida: ignora o valor, preservando compatibilidade futura.
        }
    } while (json.Consume(','));
    return json.Consume('}');
}

} // namespace

bool UIConfigManager::ValidateAndStage(
    std::string_view jsonText,
    UISettingsConfig& outStaged)
{
    if (jsonText.empty()) return false;

    JsonCursor json{jsonText};
    UISettingsConfig staged;
    bool foundSchema = false;

    if (!json.Consume('{')) return false;
    if (!json.Peek('}')) {
        do {
            const auto key = json.ParseString();
            if (!key || !json.Consume(':')) return false;

            if (*key == "schemaVersion") {
                const auto version = json.ParseInteger();
                if (!version || *version != static_cast<std::int64_t>(CurrentSchemaVersion)) {
                    return false; // Versao ausente, de tipo errado ou incompativel.
                }
                staged.schemaVersion = static_cast<std::uint32_t>(*version);
                foundSchema = true;
            } else if (*key == "globalScale") {
                staged.globalScale = ReadFloatOr(json, 1.0f, 0.1f, 10.0f);
            } else if (*key == "activeTheme") {
                const std::size_t mark = json.Position();
                if (const auto theme = json.ParseInteger();
                    theme && *theme >= std::numeric_limits<std::int32_t>::min() &&
                    *theme <= std::numeric_limits<std::int32_t>::max()) {
                    staged.activeTheme = static_cast<std::int32_t>(*theme);
                } else {
                    json.Restore(mark);
                    if (!json.SkipValue()) return false;
                }
            } else if (*key == "enableVSync") {
                const std::size_t mark = json.Position();
                if (const auto b = json.ParseBool()) {
                    staged.enableVSync = *b;
                } else {
                    json.Restore(mark);
                    if (!json.SkipValue()) return false;
                }
            } else if (*key == "windows") {
                if (!json.Consume('[')) return false;
                if (!json.Consume(']')) {
                    do {
                        if (staged.windows.size() >= MaxWindowCount) return false;
                        UIWindowConfig win;
                        if (!ParseWindow(json, win)) return false;
                        staged.windows.push_back(std::move(win));
                    } while (json.Consume(','));
                    if (!json.Consume(']')) return false;
                }
            } else if (!json.SkipValue()) {
                return false;
            }
        } while (json.Consume(','));
    }
    if (!json.Consume('}') || !json.AtEnd()) return false; // Exige objeto fechado e sem lixo final.

    if (!foundSchema || !staged.IsValid()) return false;

    outStaged = std::move(staged); // Escrita transacional: so aqui o destino e tocado.
    return true;
}

void UIConfigManager::Commit(UISettingsConfig& target, const UISettingsConfig& staged) noexcept {
    if (staged.IsValid()) {
        target = staged;
    }
}

std::string UIConfigManager::Serialize(const UISettingsConfig& config) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic()); // Ponto decimal estavel independente do locale do SO.
    ss.precision(9);                  // Round-trip exato de float.

    const auto finiteOr = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };

    ss << "{\n";
    ss << "  \"schemaVersion\": " << config.schemaVersion << ",\n";
    ss << "  \"globalScale\": " << finiteOr(config.globalScale, 1.0f) << ",\n";
    ss << "  \"activeTheme\": " << config.activeTheme << ",\n";
    ss << "  \"enableVSync\": " << (config.enableVSync ? "true" : "false") << ",\n";
    ss << "  \"windows\": [\n";

    for (std::size_t i = 0; i < config.windows.size(); ++i) {
        const auto& win = config.windows[i];
        ss << "    {\n";
        ss << "      \"name\": \"" << EscapeJson(win.name) << "\",\n";
        ss << "      \"offsetMinX\": " << finiteOr(win.offsetMin.x, 0.0f) << ",\n";
        ss << "      \"offsetMinY\": " << finiteOr(win.offsetMin.y, 0.0f) << ",\n";
        ss << "      \"offsetMaxX\": " << finiteOr(win.offsetMax.x, 150.0f) << ",\n";
        ss << "      \"offsetMaxY\": " << finiteOr(win.offsetMax.y, 100.0f) << ",\n";
        ss << "      \"isMinimized\": " << (win.isMinimized ? "true" : "false") << "\n";
        ss << "    }" << (i + 1 < config.windows.size() ? ",\n" : "\n");
    }

    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

} // namespace ToolEngine::SpriteUi::Config

#include "local_storage.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

namespace mystral {
namespace storage {

// ============================================================================
// Minimal JSON helpers for flat {string: string} objects
// ============================================================================

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

static std::string jsonUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            i++;
            switch (s[i]) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += '\\'; out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// Parse a JSON string token starting after the opening quote.
// Returns the parsed string and advances pos past the closing quote.
static std::string parseJsonString(const std::string& json, size_t& pos) {
    std::string result;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') {
            return jsonUnescape(result);
        }
        if (c == '\\' && pos < json.size()) {
            result += c;
            result += json[pos++];
        } else {
            result += c;
        }
    }
    return result;  // Unterminated string
}

static void skipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        pos++;
    }
}

// Parse a flat JSON object { "key": "value", ... } preserving insertion order.
static bool parseJsonObject(const std::string& json,
                            std::map<std::string, std::string>& data,
                            std::vector<std::string>& order) {
    size_t pos = 0;
    skipWhitespace(json, pos);

    if (pos >= json.size() || json[pos] != '{') return false;
    pos++;  // skip '{'

    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == '}') return true;  // empty object

    while (pos < json.size()) {
        skipWhitespace(json, pos);
        if (pos >= json.size() || json[pos] != '"') return false;
        pos++;  // skip opening quote
        std::string key = parseJsonString(json, pos);

        skipWhitespace(json, pos);
        if (pos >= json.size() || json[pos] != ':') return false;
        pos++;  // skip ':'

        skipWhitespace(json, pos);
        if (pos >= json.size() || json[pos] != '"') return false;
        pos++;  // skip opening quote
        std::string value = parseJsonString(json, pos);

        data[key] = value;
        order.push_back(key);

        skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ',') {
            pos++;  // skip ','
        } else if (pos < json.size() && json[pos] == '}') {
            break;
        } else {
            break;  // Malformed, but we got what we could
        }
    }
    return true;
}

static std::string toJson(const std::map<std::string, std::string>& data,
                          const std::vector<std::string>& order) {
    std::string out = "{\n";
    bool first = true;
    for (const auto& key : order) {
        auto it = data.find(key);
        if (it == data.end()) continue;
        if (!first) out += ",\n";
        out += "  \"" + jsonEscape(it->first) + "\": \"" + jsonEscape(it->second) + "\"";
        first = false;
    }
    if (!data.empty()) out += "\n";
    out += "}\n";
    return out;
}

// ============================================================================
// Platform storage directory
// ============================================================================

std::string LocalStorage::getStorageDirectory() {
    std::string base;

#ifdef _WIN32
    // Windows: %APPDATA%\Mystral\storage
    char* appdata = nullptr;
    size_t len = 0;
    if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata) {
        base = std::string(appdata);
        free(appdata);
    } else {
        base = ".";
    }
    base += "\\Mystral\\storage";
#elif defined(__APPLE__)
    // macOS: ~/Library/Application Support/Mystral/storage
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : ".";
    }
    base = std::string(home) + "/Library/Application Support/Mystral/storage";
#else
    // Linux: ~/.local/share/mystral/storage
    const char* xdgData = getenv("XDG_DATA_HOME");
    if (xdgData && xdgData[0] != '\0') {
        base = std::string(xdgData) + "/mystral/storage";
    } else {
        const char* home = getenv("HOME");
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : ".";
        }
        base = std::string(home) + "/.local/share/mystral/storage";
    }
#endif

    return base;
}

std::string LocalStorage::deriveStorageFilename(const std::string& identifier) {
    std::string safe;
    safe.reserve(identifier.size());
    for (char c : identifier) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            safe += c;
        } else {
            safe += '_';
        }
    }
    if (safe.empty()) safe = "default";
    return safe + ".json";
}

// ============================================================================
// LocalStorage implementation
// ============================================================================

void LocalStorage::init(const std::string& filePath) {
    filePath_ = filePath;

    // Create parent directories if they don't exist
    std::filesystem::path p(filePath_);
    auto parentDir = p.parent_path();
    if (!parentDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parentDir, ec);
        if (ec) {
            std::cerr << "[Storage] Failed to create directory " << parentDir << ": " << ec.message() << std::endl;
        }
    }

    load();
}

void LocalStorage::load() {
    data_.clear();
    insertionOrder_.clear();

    std::ifstream file(filePath_);
    if (!file.is_open()) {
        // File doesn't exist yet - that's fine, start empty
        return;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    file.close();

    if (content.empty()) return;

    if (!parseJsonObject(content, data_, insertionOrder_)) {
        std::cerr << "[Storage] Warning: Failed to parse " << filePath_ << ", starting fresh" << std::endl;
        data_.clear();
        insertionOrder_.clear();
    }

    std::cout << "[Storage] Loaded " << data_.size() << " entries from " << filePath_ << std::endl;
}

void LocalStorage::flush() {
    if (filePath_.empty()) return;

    std::string json = toJson(data_, insertionOrder_);

    // Atomic write: write to .tmp then rename
    std::string tmpPath = filePath_ + ".tmp";
    {
        std::ofstream file(tmpPath, std::ios::trunc);
        if (!file.is_open()) {
            std::cerr << "[Storage] Failed to write to " << tmpPath << std::endl;
            return;
        }
        file << json;
        file.flush();
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, filePath_, ec);
    if (ec) {
        std::cerr << "[Storage] Failed to rename " << tmpPath << " -> " << filePath_ << ": " << ec.message() << std::endl;
        // Fallback: try direct write
        std::ofstream file(filePath_, std::ios::trunc);
        if (file.is_open()) {
            file << json;
        }
    }
}

std::string LocalStorage::getItem(const std::string& key) const {
    auto it = data_.find(key);
    if (it != data_.end()) {
        return it->second;
    }
    return "";
}

bool LocalStorage::has(const std::string& key) const {
    return data_.find(key) != data_.end();
}

void LocalStorage::setItem(const std::string& key, const std::string& value) {
    if (data_.find(key) == data_.end()) {
        insertionOrder_.push_back(key);
    }
    data_[key] = value;
    flush();
}

void LocalStorage::removeItem(const std::string& key) {
    if (data_.erase(key) > 0) {
        insertionOrder_.erase(
            std::remove(insertionOrder_.begin(), insertionOrder_.end(), key),
            insertionOrder_.end()
        );
        flush();
    }
}

void LocalStorage::clear() {
    if (!data_.empty()) {
        data_.clear();
        insertionOrder_.clear();
        flush();
    }
}

std::string LocalStorage::key(int index) const {
    if (index >= 0 && index < static_cast<int>(insertionOrder_.size())) {
        return insertionOrder_[index];
    }
    return "";
}

int LocalStorage::length() const {
    return static_cast<int>(data_.size());
}

const std::vector<std::string>& LocalStorage::keys() const {
    return insertionOrder_;
}

}  // namespace storage
}  // namespace mystral

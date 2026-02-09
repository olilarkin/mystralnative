/**
 * LocalStorage - File-backed key-value storage
 *
 * Provides a browser-compatible localStorage implementation backed by a JSON
 * file on disk. Each game directory gets a separate storage file, keyed by
 * the current working directory name.
 *
 * Storage paths:
 *   macOS:   ~/Library/Application Support/Mystral/storage/
 *   Linux:   ~/.local/share/mystral/storage/
 *   Windows: %APPDATA%\Mystral\storage\
 */

#pragma once

#include <string>
#include <map>
#include <vector>

namespace mystral {
namespace storage {

class LocalStorage {
public:
    LocalStorage() = default;

    /**
     * Initialize storage from a JSON file on disk.
     * Creates directories and file if they don't exist.
     * @param filePath Absolute path to the JSON storage file
     */
    void init(const std::string& filePath);

    /**
     * Get a value by key.
     * @return The value string, or empty string if not found.
     *         Use has() to distinguish missing keys from empty values.
     */
    std::string getItem(const std::string& key) const;

    /**
     * Check if a key exists.
     */
    bool has(const std::string& key) const;

    /**
     * Set a key-value pair and flush to disk.
     */
    void setItem(const std::string& key, const std::string& value);

    /**
     * Remove a key and flush to disk.
     */
    void removeItem(const std::string& key);

    /**
     * Remove all keys and flush to disk.
     */
    void clear();

    /**
     * Get the key at a given index (insertion order).
     * @return The key string, or empty string if index out of range.
     */
    std::string key(int index) const;

    /**
     * Get the number of stored keys.
     */
    int length() const;

    /**
     * Get all keys in insertion order.
     */
    const std::vector<std::string>& keys() const;

    /**
     * Get the platform-specific base storage directory.
     *   macOS:   ~/Library/Application Support/Mystral/storage/
     *   Linux:   ~/.local/share/mystral/storage/
     *   Windows: %APPDATA%\Mystral\storage\
     */
    static std::string getStorageDirectory();

    /**
     * Derive a safe filename from an identifier string (e.g., cwd stem).
     * Replaces non-alphanumeric characters with underscores.
     */
    static std::string deriveStorageFilename(const std::string& identifier);

private:
    void load();
    void flush();

    std::string filePath_;
    std::map<std::string, std::string> data_;
    std::vector<std::string> insertionOrder_;
};

}  // namespace storage
}  // namespace mystral

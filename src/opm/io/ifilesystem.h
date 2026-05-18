#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct IFileSystem {
    virtual ~IFileSystem() = default;

    // Read complete file into out (binary-safe). out is overwritten.
    virtual bool readFileToVector(const std::string& path, std::vector<uint8_t>& out) = 0;

    // Write complete file from data (binary-safe). Overwrites existing file.
    virtual bool writeFileFromVector(const std::string& path, const std::vector<uint8_t>& data) = 0;

    // Convenience: write from string
    virtual bool writeFileFromString(const std::string& path, const std::string& s) {
        return writeFileFromVector(path, std::vector<uint8_t>(s.begin(), s.end()));
    }

    // Basic FS ops (not always supported on embedded FS; return false if unsupported)
    virtual bool exists(const std::string& path) = 0;
    virtual bool removeFile(const std::string& path) = 0;
    virtual bool renameFile(const std::string& oldPath, const std::string& newPath) = 0;

    // Optional capability: list directory entries (filenames only, no recursion)
    virtual bool listDir(const std::string& dir, std::vector<std::string>& outNames) {
        (void)dir;
        outNames.clear();
        return false;
    }

    // Optional helper: best-effort atomic write via temp+rename.
    // If rename/remove are not supported by the FS implementation, this returns false.
    virtual bool writeFileAtomic(const std::string& path, const std::vector<uint8_t>& data) {
        const std::string tmp = path + ".tmp";

        if (!writeFileFromVector(tmp, data)) return false;

        // Try to replace existing file.
        if (exists(path)) {
            if (!removeFile(path)) return false;
        }
        if (!renameFile(tmp, path)) return false;

        return true;
    }
};
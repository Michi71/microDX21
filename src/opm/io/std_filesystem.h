#pragma once
#include "ifilesystem.h"

#include <algorithm>
#include <fstream>
#include <filesystem>

struct StdFileSystem final : public IFileSystem {

    bool readFileToVector(const std::string& path, std::vector<uint8_t>& out) override
    {
        out.clear();

        std::ifstream in(path, std::ios::binary);
        if (!in) return false;

        in.seekg(0, std::ios::end);
        std::streamoff size = in.tellg();
        if (size < 0) return false;
        in.seekg(0, std::ios::beg);

        out.resize(static_cast<size_t>(size));
        if (!out.empty()) {
            in.read(reinterpret_cast<char*>(out.data()), size);
            if (!in) return false;
        }
        return true;
    }

    bool writeFileFromVector(const std::string& path, const std::vector<uint8_t>& data) override
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;

        if (!data.empty()) {
            out.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(data.size()));
            if (!out) return false;
        }
        return true;
    }

    bool exists(const std::string& path) override
    {
        std::error_code ec;
        return std::filesystem::exists(path, ec) && !ec;
    }

    bool removeFile(const std::string& path) override
    {
        std::error_code ec;
        bool ok = std::filesystem::remove(path, ec);
        return ok && !ec;
    }

    bool renameFile(const std::string& oldPath, const std::string& newPath) override
    {
        std::error_code ec;
        std::filesystem::rename(oldPath, newPath, ec);
        return !ec;
    }

    bool listDir(const std::string& dir, std::vector<std::string>& outNames) override
    {
        outNames.clear();

        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(dir, ec);
             !ec && it != std::filesystem::directory_iterator{};
             it.increment(ec)) {

            if (ec) break;
            if (it->is_regular_file()) {
                outNames.push_back(it->path().filename().string());
            }
        }
        if (ec) return false;

        std::sort(outNames.begin(), outNames.end());
        return true;
    }

    bool MakeDirectory(const std::string& path, bool recursive) override
    {
        if (path.empty()) return false;
        std::error_code ec;
        if (recursive) {
            std::filesystem::create_directories(path, ec);
        } else {
            std::filesystem::create_directory(path, ec);
        }
        // create_directories returns true even when the path already existed
        // (and was a directory). That matches `mkdir -p` semantics, which is
        // what callers expect.
        return !ec;
    }
};
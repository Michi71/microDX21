#pragma once
#include "ifilesystem.h"

#include <wrap_fatfs.h>  // Use Circle's wrapper to avoid DIR conflicts
#include <algorithm>

class FatFsFileSystem final : public IFileSystem
{
public:
    FatFsFileSystem(FATFS* fs, const char* root = "")
    : m_fs(fs)
    , m_root(root ? root : "")
    {
    }

    bool readFileToVector(const std::string& path, std::vector<uint8_t>& out) override
    {
        out.clear();
        FIL f;
        std::string p = buildPath(path);
        if (f_open(&f, p.c_str(), FA_READ) != FR_OK)
            return false;

        FSIZE_t size = f_size(&f);
        out.resize((size_t)size);
        UINT br = 0;
        FRESULT r = (size > 0) ? f_read(&f, out.data(), (UINT)size, &br) : FR_OK;
        f_close(&f);

        if (r != FR_OK || br != (UINT)size)
        {
            out.clear();
            return false;
        }
        return true;
    }

    bool writeFileFromVector(const std::string& path, const std::vector<uint8_t>& data) override
    {
        FIL f;
        std::string p = buildPath(path);
        if (f_open(&f, p.c_str(), FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
            return false;

        UINT bw = 0;
        FRESULT r = data.empty() ? FR_OK : f_write(&f, data.data(), (UINT)data.size(), &bw);
        f_close(&f);

        return (r == FR_OK && bw == (UINT)data.size());
    }

    bool exists(const std::string& path) override
    {
        FILINFO fi;
        std::string p = buildPath(path);
        return f_stat(p.c_str(), &fi) == FR_OK;
    }

    bool removeFile(const std::string& path) override
    {
        std::string p = buildPath(path);
        return f_unlink(p.c_str()) == FR_OK;
    }

    bool renameFile(const std::string& oldPath, const std::string& newPath) override
    {
        std::string o = buildPath(oldPath);
        std::string n = buildPath(newPath);
        return f_rename(o.c_str(), n.c_str()) == FR_OK;
    }

    bool listDir(const std::string& dir, std::vector<std::string>& outNames) override
    {
        outNames.clear();
        DIR d;
        FILINFO fi;

        std::string p = buildPath(dir);
        FRESULT res = f_opendir(&d, p.c_str());

        if (res != FR_OK) {
            return false;
        }

        while (f_readdir(&d, &fi) == FR_OK)
        {
            if (fi.fname[0] == '\0')
                break;
            // Skip directories and hidden/system files
            if (fi.fattrib & (AM_DIR | AM_HID | AM_SYS))
                continue;
            outNames.push_back(fi.fname);
        }
        f_closedir(&d);
        std::sort(outNames.begin(), outNames.end());
        return true;
    }

    bool MakeDirectory(const std::string& path, bool recursive) override
    {
        if (path.empty()) return false;
        std::string p = buildPath(path);

        if (!recursive) {
            return f_mkdir(p.c_str()) == FR_OK;
        }

        // Recursive: walk path components and create each missing one.
        // FatFS f_mkdir only creates a single level, so we manually
        // split on '/' and create prefix by prefix.
        std::string acc;
        if (!p.empty() && p.front() == '/') acc = "/";

        size_t i = 0;
        // skip leading slash (already in acc)
        if (!acc.empty()) i = 1;

        bool ok = true;
        while (i <= p.size()) {
            if (i == p.size() || p[i] == '/') {
                std::string component = p.substr(0, i);
                if (!component.empty() && component != "." && component != "/") {
                    // f_stat to probe; FR_OK means it already exists.
                    FILINFO fi;
                    FRESULT r = f_stat(component.c_str(), &fi);
                    if (r == FR_OK) {
                        if (!(fi.fattrib & AM_DIR)) {
                            // Path component exists but is a file -> cannot proceed.
                            ok = false;
                            break;
                        }
                    } else {
                        // Not present -> create.
                        FRESULT m = f_mkdir(component.c_str());
                        if (m != FR_OK && m != FR_EXIST) {
                            ok = false;
                            break;
                        }
                    }
                }
            }
            ++i;
        }
        return ok;
    }

private:
    std::string buildPath(const std::string& rel) const
    {
        if (m_root.empty())
            return rel;

        if (rel.compare(0, m_root.length(), m_root) == 0)
            return rel;

        std::string full = m_root;
        if (!full.empty() && full.back() != '/' && !rel.empty() && rel.front() != '/')
            full += '/';
        full += rel;
        return full;
    }

private:
    FATFS* m_fs;
    std::string m_root;
};

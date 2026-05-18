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

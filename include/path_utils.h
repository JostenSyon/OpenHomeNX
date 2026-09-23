#pragma once
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstdio>
#include <fstream>

inline bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

inline std::string parentDir(const std::string& p) {
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos) return "";
    return p.substr(0, slash);
}

inline std::string fileName(const std::string& p) {
    size_t slash = p.find_last_of('/');
    return (slash == std::string::npos) ? p : p.substr(slash + 1);
}

inline std::string stemOf(const std::string& p) {
    std::string f = fileName(p);
    size_t dot = f.find_last_of('.');
    if (dot == std::string::npos) return f;
    return f.substr(0, dot);
}

inline std::string dirName(const std::string& p) {
    std::string d = p;
    while (!d.empty() && d.back() == '/') d.pop_back();
    size_t slash = d.find_last_of('/');
    return (slash == std::string::npos) ? d : d.substr(slash + 1);
}

inline bool ensureDir(const std::string& dir) {
    std::string cur;
    for (size_t i = 0; i < dir.size(); i++) {
        cur += dir[i];
        if (dir[i] == '/' && cur.size() > 1)
            mkdir(cur.c_str(), 0755);
    }
    if (!cur.empty() && cur.back() != '/')
        mkdir(cur.c_str(), 0755);
    struct stat st;
    return stat(dir.c_str(), &st) == 0;
}

inline bool copyFile(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in.good()) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out.good()) return false;
    char buf[65536];
    while (in.good()) {
        in.read(buf, sizeof(buf));
        std::streamsize n = in.gcount();
        if (n > 0) out.write(buf, n);
    }
    out.flush();
    return out.good();
}

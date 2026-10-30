#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define ESP_LOGI(tag, fmt, ...) \
    do { fprintf(stderr, "[I][%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGW(tag, fmt, ...) \
    do { fprintf(stderr, "[W][%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGE(tag, fmt, ...) \
    do { fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)

#define MALLOC_CAP_EXEC 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_DEFAULT 2

static inline void *heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}
static inline void heap_caps_free(void *p) { free(p); }

class SerialClass {
public:
    void printf(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fputc('\n', stderr);
    }
    void println(const char *s) { fprintf(stderr, "%s\n", s); }
};
extern SerialClass Serial;

#define FILE_READ 0

namespace fs {
class File {
public:
    FILE *fp = nullptr;
    size_t sz = 0;

    operator bool() const { return fp != nullptr; }
    size_t size() const { return sz; }
    bool seek(uint32_t pos) {
        return fp && fseek(fp, (long)pos, SEEK_SET) == 0;
    }
    int read(uint8_t *buf, size_t len) {
        if (!fp) return 0;
        return (int)fread(buf, 1, len, fp);
    }
    void close() {
        if (fp) fclose(fp);
        fp = nullptr;
    }
};

} 

using fs::File;

namespace fs {
class FS {
public:
    std::string root;
    const char *mountpoint() const { return "/sd"; }

    File open(const char *path, int mode = FILE_READ) {
        (void)mode;
        File f;
        if (!path) return f;
        std::string p = path;
        const std::string mount = "/sd";
        if (p.rfind(mount, 0) == 0) p = p.substr(mount.size());
        if (!p.empty() && p[0] == '/') p = p.substr(1);
        std::string full = root + "/" + p;
        f.fp = (fopen)(full.c_str(), "rb");
        if (f.fp) {
            fseek(f.fp, 0, SEEK_END);
            f.sz = (size_t)ftell(f.fp);
            fseek(f.fp, 0, SEEK_SET);
        }
        return f;
    }
};
} 

extern fs::FS *global_fs;

extern "C" {
extern unsigned long (*g_host_sym_resolver)(const char *name);
unsigned long elf_find_sym_default(const char *name);
}

extern std::string g_host_fs_root;
inline FILE *host_fopen(const char *path, const char *mode) {
    std::string p = path;
    const std::string mount = "/sd";
    if (p.rfind(mount, 0) == 0) p = p.substr(mount.size());
    if (!p.empty() && p[0] == '/') p = p.substr(1);
    return (fopen)((g_host_fs_root + "/" + p).c_str(), mode);
}
#define fopen host_fopen

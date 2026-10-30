#include "esp_host_shims.h"

#include "overlay_loader.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

fs::FS *global_fs = nullptr;
SerialClass Serial;
std::string g_host_fs_root;
unsigned long (*g_host_sym_resolver)(const char *name) = nullptr;

unsigned long elf_find_sym_default(const char *name) {
    if (g_host_sym_resolver) return g_host_sym_resolver(name);
    return 0;
}

namespace {

constexpr uint32_t kEntryCanonical = 0x2bc;   
constexpr uint32_t kEntryStubOff = 0x130;
constexpr uint32_t kEntryBodyOff = 0xc4;
constexpr uint32_t kEntryKey = 0x000000c4;    
constexpr uint32_t kPatchWordOff = 0x114;      
constexpr uint32_t kHostRefOff = 0x118;        
constexpr unsigned long kFakeTextWidth = 0x12345678;
constexpr uint32_t kStubPatchOff = 0x11c;

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            g_failures++;                                               \
        }                                                               \
    } while (0)

uint32_t word_at(const void *base, uint32_t off) {
    uint32_t v;
    memcpy(&v, (const uint8_t *)base + off, sizeof(v));
    return v;
}

} 

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <fixture-blob>\n", argv[0]);
        return 2;
    }
    const char *blob_path = argv[1];

    std::string blob_dir(blob_path);
    size_t slash = blob_dir.find_last_of('/');
    blob_dir = (slash == std::string::npos) ? "." : blob_dir.substr(0, slash);
    fs::FS fsio;
    fsio.root = blob_dir;
    global_fs = &fsio;
    g_host_fs_root = blob_dir;
    g_host_sym_resolver = [](const char *name) -> unsigned long {
        if (strcmp(name, "pm_text_width") == 0) return kFakeTextWidth;
        return 0;
    };

    overlay_loader_t *loader = nullptr;
    std::string blob_name(blob_path);
    size_t last_sep = blob_name.find_last_of('/');
    if (last_sep != std::string::npos) {
        blob_name = blob_name.substr(last_sep + 1);
    }
    CHECK(overlay_loader_load(&loader, blob_name.c_str()) == OVERLAY_OK);
    CHECK(loader != nullptr);
    if (!loader) return 1;

    void *stub_block = overlay_loader_get_stub_block_base(loader);
    CHECK(stub_block != nullptr);
    CHECK(word_at(stub_block, 0) == (uint32_t)(uintptr_t)&overlay_enter);
    CHECK(word_at(stub_block, 4) == (uint32_t)(uintptr_t)&overlay_leave);
    CHECK(word_at(stub_block, 0x20) == kEntryKey);

    uint32_t stub_off = 0;
    CHECK(overlay_loader_find_stub_off(loader, kEntryCanonical, &stub_off)
          == OVERLAY_OK);
    CHECK(stub_off == kEntryStubOff);
    CHECK(overlay_loader_find_stub_off(loader, 0xdeadbeef, &stub_off)
          == OVERLAY_ERR_RANGE);

    overlay_section_map_t map{};
    map.entries[0].v_addr = 0x1b8;                 
    map.entries[0].addr = 0x403c93e0;              
    map.entries[0].size = 0x153;
    map.entries[1].v_addr = 0x30c;                 
    map.entries[1].addr = 0x3fcee8f0;              
    map.entries[1].size = 2;
    CHECK(overlay_loader_set_section_map(loader, &map) == OVERLAY_OK);
    CHECK(overlay_loader_set_section_map(loader, nullptr) == OVERLAY_ERR_RANGE);

    uint32_t resident = 0;
    CHECK(overlay_loader_resolve(loader, 0x2bc, &resident) == OVERLAY_OK);
    CHECK(resident == 0x403c93e0 + (0x2bc - 0x1b8));
    CHECK(overlay_loader_resolve(loader, 0x30c, &resident) == OVERLAY_OK);
    CHECK(resident == 0x3fcee8f0);
    CHECK(overlay_loader_resolve(loader, 0x30e, &resident) == OVERLAY_ERR_RANGE);
    CHECK(overlay_loader_resolve(loader, 0x1b7, &resident) == OVERLAY_ERR_RANGE);

    CHECK(overlay_loader_apply_redirects(loader) == OVERLAY_OK);

    overlay_loader_set_active(loader);
    void *body = overlay_enter(kEntryKey);
    CHECK(body != nullptr);   
    if (body) {
        size_t frame_size = overlay_loader_get_frame_size(loader);
        size_t frame_count = overlay_loader_get_frame_count(loader);
        CHECK(frame_count > 0);
        CHECK(frame_size > kEntryBodyOff);
        CHECK(((uintptr_t)body & 3) == 0);
    }

    if (body) {
        uint8_t *frame = (uint8_t *)body - kEntryBodyOff;
        CHECK(word_at(frame, kPatchWordOff) == 0x3fcee8f0);
        CHECK(word_at(frame, kHostRefOff) == kFakeTextWidth);
        uint32_t stub0 = word_at(frame, kStubPatchOff);
        CHECK(stub0 == (uint32_t)(uintptr_t)stub_block + 0x80);
    }

    if (body) {
        void *body2 = overlay_enter(kEntryKey);
        CHECK(body2 == body);
    }

    overlay_loader_unload(loader);
    overlay_loader_set_active(nullptr);

    CHECK(overlay_enter(kEntryKey) == nullptr);

    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    fprintf(stderr, "host loader tests passed\n");
    return 0;
}

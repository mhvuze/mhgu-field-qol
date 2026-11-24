// The part of Forge's plugin API (github.com/Fexty12573/forge) this plugin uses, as MHGU Recomp exposes it, and the
// few nn::fs calls (the game's own SDK, which plugins can import) it reads and writes its settings file with.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#define EXPORT __attribute__((visibility("default")))

typedef struct {
    u16 major, minor, patch;
    char name[64];
} ForgePluginInfo;

// Forge's Hook (24 bytes, returned through a hidden pointer): target, detour, the original's trampoline, context...
typedef struct {
    u32 target;
    u32 detour;
    u32 original;
    u32 reserved[3];
} ForgeHook;

enum { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 };

extern void forge_hook_create(ForgeHook *hook, u32 target, u32 detour, u32 *original);
extern u32 forge_input_isDown(u32 button); // (nn::hid NpadButton bits: A 0, B 1, X 2, Y 3, ... L 6, R 7, ZL 8, ZR 9)
extern u32 forge_mem_getMainTextAddr(void);

// The game's save data (the folder holding the save, save_0): mount, then files are at "<mount point>:/name";
// unmounting commits what was written.
extern bool forge_fs_mountSaveData(void);
extern void forge_fs_unmountSaveData(void);
extern const char *forge_fs_getSaveDataMountPoint(void);
extern void forge_log(int level, const char *fmt, ...);

// cimgui, as far as this plugin needs it.
extern void igSetCurrentContext(void *ctx);
extern void igSetAllocatorFunctions(void *alloc, void *free, void *user);
extern bool igTreeNode_Str(const char *label);
extern void igTreePop(void);
extern void igTextUnformatted(const char *text, const char *text_end);
extern bool igCheckbox(const char *label, bool *v);

// nn::fs (nn::Result is returned in r0: 0 = success)
typedef struct { void *handle; } FsFile;      // nn::fs::FileHandle
typedef struct { int flags; } FsWriteOption;  // nn::fs::WriteOption (1: flush)
enum { FS_READ = 1, FS_WRITE = 2, FS_APPEND = 4 };
extern u32 fs_create(const char *path, s64 size) __asm__("_ZN2nn2fs10CreateFileEPKcx");
extern u32 fs_open(FsFile *out, const char *path, int mode) __asm__("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci");
extern void fs_close(FsFile f) __asm__("_ZN2nn2fs9CloseFileENS0_10FileHandleE");
extern u32 fs_size(s64 *out, FsFile f) __asm__("_ZN2nn2fs11GetFileSizeEPxNS0_10FileHandleE");
extern u32 fs_set_size(FsFile f, s64 size) __asm__("_ZN2nn2fs11SetFileSizeENS0_10FileHandleEx");
extern u32 fs_read(FsFile f, s64 offset, void *buf, u32 size) __asm__("_ZN2nn2fs8ReadFileENS0_10FileHandleExPvj");
extern u32 fs_write(FsFile f, s64 offset, const void *buf, u32 size, const FsWriteOption *opt)
    __asm__("_ZN2nn2fs9WriteFileENS0_10FileHandleExPKvjRKNS0_11WriteOptionE");

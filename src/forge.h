// The part of Forge's plugin API (github.com/Fexty12573/forge) this plugin uses, as MHGU Recomp exposes it.
#pragma once

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef short s16;
typedef _Bool bool;
#define true 1
#define false 0
#define NULL ((void *)0)

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
extern u32 forge_mem_getMainTextAddr(void);
extern void forge_log(int level, const char *fmt, ...);

// cimgui, as far as this plugin needs it.
extern void igSetCurrentContext(void *ctx);
extern void igSetAllocatorFunctions(void *alloc, void *free, void *user);
extern bool igTreeNode_Str(const char *label);
extern void igTreePop(void);
extern void igTextUnformatted(const char *text, const char *text_end);
extern bool igCheckbox(const char *label, bool *v);

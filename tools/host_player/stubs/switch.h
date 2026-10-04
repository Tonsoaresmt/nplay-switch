#pragma once
/* Host stub do libnx para rodar o player real no Linux. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef u32 Result;
#define R_SUCCEEDED(r) ((r) == 0)
#define R_FAILED(r) ((r) != 0)
typedef enum { AppletType_None = -2, AppletType_Default = -1, AppletType_Application = 0, AppletType_LibraryApplet = 2 } AppletType;
AppletType appletGetAppletType(void);
Result appletSetMediaPlaybackState(bool state);
typedef enum { InfoType_TotalMemorySize = 6, InfoType_UsedMemorySize = 7 } InfoType;
#define CUR_PROCESS_HANDLE 0xFFFF8001
Result svcGetInfo(u64 *out, u32 id0, u32 handle, u64 id1);
typedef enum { PlServiceType_User = 0, PlServiceType_System = 1 } PlServiceType;
typedef enum { PlSharedFontType_Standard = 0, PlSharedFontType_NintendoExt = 5 } PlSharedFontType;
typedef struct { u32 type; u32 offset; u32 size; void *address; } PlFontData;
Result plInitialize(PlServiceType t);
void plExit(void);
Result plGetSharedFontByType(PlFontData *font, PlSharedFontType type);
typedef struct { int dummy; } SwkbdConfig;
Result swkbdCreate(SwkbdConfig *c, s32 max_dict);
void swkbdConfigMakePresetDefault(SwkbdConfig *c);
void swkbdConfigSetGuideText(SwkbdConfig *c, const char *s);
void swkbdConfigSetStringLenMax(SwkbdConfig *c, u32 n);
void swkbdConfigSetPasswordFlag(SwkbdConfig *c, u8 f);
void swkbdConfigSetInitialText(SwkbdConfig *c, const char *s);
void swkbdConfigSetHeaderText(SwkbdConfig *c, const char *s);
void swkbdConfigSetOkButtonText(SwkbdConfig *c, const char *s);
Result swkbdShow(SwkbdConfig *c, char *out, size_t cap);
void swkbdClose(SwkbdConfig *c);

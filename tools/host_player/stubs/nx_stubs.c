#include "switch.h"
#include <stdio.h>
#include <stdlib.h>
AppletType appletGetAppletType(void) { return AppletType_Application; }
Result appletSetMediaPlaybackState(bool state) { (void)state; return 0; }
Result svcGetInfo(u64 *out, u32 id0, u32 handle, u64 id1) { (void)handle; (void)id1; *out = id0 == 7 ? 300ull<<20 : 3200ull<<20; return 0; }
Result plInitialize(PlServiceType t) { (void)t; return 0; }
void plExit(void) {}
static void *g_font; static long g_font_size;
Result plGetSharedFontByType(PlFontData *font, PlSharedFontType type) {
    (void)type;
    if (!g_font) {
        const char *path = getenv("HARNESS_FONT");
        FILE *f = fopen(path ? path : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "rb");
        if (!f) return 1;
        fseek(f, 0, SEEK_END); g_font_size = ftell(f); fseek(f, 0, SEEK_SET);
        g_font = malloc(g_font_size); fread(g_font, 1, g_font_size, f); fclose(f);
    }
    font->address = g_font; font->size = (u32)g_font_size; font->type = 0; font->offset = 0;
    return 0;
}
Result swkbdCreate(SwkbdConfig *c, s32 m) { (void)c; (void)m; return 1; }
void swkbdConfigMakePresetDefault(SwkbdConfig *c) { (void)c; }
void swkbdConfigSetGuideText(SwkbdConfig *c, const char *s) { (void)c; (void)s; }
void swkbdConfigSetStringLenMax(SwkbdConfig *c, u32 n) { (void)c; (void)n; }
void swkbdConfigSetPasswordFlag(SwkbdConfig *c, u8 f) { (void)c; (void)f; }
void swkbdConfigSetInitialText(SwkbdConfig *c, const char *s) { (void)c; (void)s; }
void swkbdConfigSetHeaderText(SwkbdConfig *c, const char *s) { (void)c; (void)s; }
void swkbdConfigSetOkButtonText(SwkbdConfig *c, const char *s) { (void)c; (void)s; }
Result swkbdShow(SwkbdConfig *c, char *o, size_t n) { (void)c; (void)o; (void)n; return 1; }
void swkbdClose(SwkbdConfig *c) { (void)c; }

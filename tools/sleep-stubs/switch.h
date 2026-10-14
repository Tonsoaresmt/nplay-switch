#pragma once
// Only official libnx types used by lifecycle, with a fake applet dispatcher.
typedef enum { AppletHookType_OnFocusState, AppletHookType_OnOperationMode,
    AppletHookType_OnPerformanceMode, AppletHookType_OnExitRequest,
    AppletHookType_OnResume } AppletHookType;
typedef enum { AppletFocusState_InFocus=1, AppletFocusState_OutOfFocus,
    AppletFocusState_Background } AppletFocusState;
typedef void (*AppletHookFn)(AppletHookType, void *);
typedef struct { void *opaque; } AppletHookCookie;
void appletHook(AppletHookCookie *, AppletHookFn, void *);
void appletUnhook(AppletHookCookie *);
AppletFocusState appletGetFocusState(void);

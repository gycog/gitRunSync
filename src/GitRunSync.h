#ifndef GITRUNSYNC_H
#define GITRUNSYNC_H

#include <stdbool.h>

#define GRS_MAX_PATH 4096

typedef enum {
    CONSOLE_MINIMIZE_TASKBAR = 0,
    CONSOLE_MINIMIZE_TRAY = 1,
    CONSOLE_MINIMIZE_NONE = 2,
} ConsoleMinimizeMode;

typedef struct {
    const char* runExePath;
    const char* runExeArg;
    int createShortcut;  /* -s, --create-shortcut */
} Args;

typedef struct {
    char runStoreApp[256];
    char runExePath[GRS_MAX_PATH];
    char runExeArg[512];
    char repoDir[GRS_MAX_PATH];
    char consoleMinimize[32];
    bool autoCleanOnConflict;
    bool showWindow;  /* 0=静默(默认), 1=显示窗口 */
    char webhookUrl[512];
    char webhookSecret[128];
} Config;

#endif

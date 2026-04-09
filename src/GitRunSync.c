/*
 * GitRunSync
 *
 * 跨平台版本：内部统一使用 char* (UTF-8)，平台层负责本地 API 适配
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>
#ifndef _WIN32
#include <strings.h> /* strcasecmp */
#endif

#include "platform.h"
#include "toml_config.h"
#include "GitRunSync.h"
#include "error_codes.h"

#ifndef GITRUNSYNC_VERSION
#define GITRUNSYNC_VERSION "1.0.0"
#endif

/* ============================================================
 * 日志系统
 * ============================================================ */
static FILE* g_logFile = NULL;
static int g_silentMode = 0;

void log_init(const char* logPath, int silent) {
    g_silentMode = silent;
    if (logPath && logPath[0]) {
        g_logFile = fopen(logPath, "a");
    }
}

void log_close(void) {
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = NULL;
    }
}

void log_write(const char* fmt, ...) {
    va_list args;

    /* 如果不在静默模式，输出到控制台 */
    if (!g_silentMode) {
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        fflush(stdout);
    }

    /* 写入日志文件 */
    if (g_logFile) {
        /* 添加时间戳 */
        time_t now = time(NULL);
        struct tm* lt = localtime(&now);
        fprintf(g_logFile, "[%04d-%02d-%02d %02d:%02d:%02d] ",
                lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                lt->tm_hour, lt->tm_min, lt->tm_sec);

        va_start(args, fmt);
        vfprintf(g_logFile, fmt, args);
        va_end(args);
        fflush(g_logFile);
    }
}

void log_error(const char* fmt, ...) {
    va_list args;

    /* 错误始终输出到 stderr（除非静默模式） */
    if (!g_silentMode) {
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        fflush(stderr);
    }

    /* 写入日志文件 */
    if (g_logFile) {
        time_t now = time(NULL);
        struct tm* lt = localtime(&now);
        fprintf(g_logFile, "[%04d-%02d-%02d %02d:%02d:%02d] [ERROR] ",
                lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                lt->tm_hour, lt->tm_min, lt->tm_sec);

        va_start(args, fmt);
        vfprintf(g_logFile, fmt, args);
        va_end(args);
        fflush(g_logFile);
    }
}

static int str_casecmp(const char* a, const char* b) {
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

// ------------------------------------------------------------
// 通用辅助函数
// ------------------------------------------------------------
static void trim_inplace(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    if (i > 0) {
        memmove(s, s + i, n - i + 1);
        n -= i;
    }
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[n - 1] = 0;
        n--;
    }
}

// ------------------------------------------------------------
// 输入验证函数
// ------------------------------------------------------------

/* 检查字符串是否包含 shell 危险字符 */
static int contains_shell_metachar(const char* s) {
    if (!s) return 0;
    for (const char* p = s; *p; p++) {
        switch (*p) {
            case ';': case '&': case '|': case '$':
            case '`': case '<': case '>': case '(':
            case ')': case '{': case '}': case '[':
            case ']': case '*': case '?': case '~':
            case '!': case '"': case '\'':
                return 1;
#ifndef _WIN32
            /* 非 Windows 平台，反斜杠视为危险字符 */
            case '\\':
                return 1;
#endif
        }
    }
    return 0;
}

/* 验证路径是否安全 */
static int validate_path(const char* path, const char* field_name) {
    if (!path || !path[0]) return 1;  /* 空路径允许 */

    /* 检查长度限制 */
    if (strlen(path) >= GRS_MAX_PATH) {
        fprintf(stderr, "错误：%s 路径过长（最大 %d 字符）\n", field_name, GRS_MAX_PATH - 1);
        return 0;
    }

    /* 检查危险字符 */
    if (contains_shell_metachar(path)) {
        fprintf(stderr, "错误：%s 包含非法字符（不允许 shell 元字符）\n", field_name);
        return 0;
    }

    /* 检查路径遍历 */
    if (strstr(path, "..") != NULL) {
        fprintf(stderr, "错误：%s 包含路径遍历序列（..）\n", field_name);
        return 0;
    }

    return 1;
}

/* 验证参数是否安全 */
static int validate_arg(const char* arg, const char* field_name) {
    if (!arg || !arg[0]) return 1;  /* 空参数允许 */

    /* 检查长度限制 */
    if (strlen(arg) >= 512) {
        fprintf(stderr, "错误：%s 参数过长（最大 511 字符）\n", field_name);
        return 0;
    }

    /* 参数可以包含更多字符，但仍限制一些危险字符 */
    for (const char* p = arg; *p; p++) {
        if (*p == '\n' || *p == '\r') {
            fprintf(stderr, "错误：%s 包含非法换行符\n", field_name);
            return 0;
        }
    }

    return 1;
}

static char* dup_str(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* out = (char*)calloc(n + 1, 1);
    if (out) strcpy(out, s);
    return out;
}

static const char* path_basename_ptr(const char* p) {
    if (!p) return "";
    const char* l1 = strrchr(p, '\\');
    const char* l2 = strrchr(p, '/');
    const char* last = l1;
    if (l2 && (!last || l2 > last)) last = l2;
    return last ? (last + 1) : p;
}

static void copy_tail_ellipsis(char* out, size_t cap, const char* s, size_t tailChars) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!s || !s[0]) return;
    size_t len = strlen(s);
    if (tailChars < 8) tailChars = 8;
    if (len <= tailChars) {
        strncpy(out, s, cap - 1);
        out[cap - 1] = 0;
        return;
    }
    const char* tail = s + (len - (tailChars - 3));
    snprintf(out, cap, "...%s", tail);
}

static void get_repo_display_name(char* out, size_t cap, const char* repoDir) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!repoDir || !repoDir[0]) return;
    char tmp[GRS_MAX_PATH];
    strncpy(tmp, repoDir, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    size_t n = strlen(tmp);
    while (n > 0 && (tmp[n - 1] == '\\' || tmp[n - 1] == '/')) {
        tmp[n - 1] = 0;
        n--;
    }
    if (n == 0) return;
    const char* base = path_basename_ptr(tmp);
    if (!base || !base[0]) base = tmp;
    strncpy(out, base, cap - 1);
    out[cap - 1] = 0;
}

static int month_str_to_int3(const char* m3) {
    if (!m3) return 0;
    if (strncmp(m3, "Jan", 3) == 0) return 1;
    if (strncmp(m3, "Feb", 3) == 0) return 2;
    if (strncmp(m3, "Mar", 3) == 0) return 3;
    if (strncmp(m3, "Apr", 3) == 0) return 4;
    if (strncmp(m3, "May", 3) == 0) return 5;
    if (strncmp(m3, "Jun", 3) == 0) return 6;
    if (strncmp(m3, "Jul", 3) == 0) return 7;
    if (strncmp(m3, "Aug", 3) == 0) return 8;
    if (strncmp(m3, "Sep", 3) == 0) return 9;
    if (strncmp(m3, "Oct", 3) == 0) return 10;
    if (strncmp(m3, "Nov", 3) == 0) return 11;
    if (strncmp(m3, "Dec", 3) == 0) return 12;
    return 0;
}

static char g_build_stamp[16] = "";

static void ensure_build_stamp(void) {
    if (g_build_stamp[0]) return;
    const char* d = __DATE__;
    const char* t = __TIME__;
    int mm = month_str_to_int3(d);
    int dd = 0, yyyy = 0, hh = 0, mi = 0;
    sscanf(d, "%*s %d %d", &dd, &yyyy);
    sscanf(t, "%d:%d", &hh, &mi);
    int yy = yyyy % 100;
    snprintf(g_build_stamp, sizeof(g_build_stamp), "%02d%02d%02d_%02d%02d", yy, mm, dd, hh, mi);
}

static void format_uptime(char* out, size_t cap) {
    unsigned long long now = plat_get_tick_ms();
    unsigned long long secs = now / 1000ULL;
    unsigned long long days = secs / 86400ULL;
    unsigned long long hours = (secs % 86400ULL) / 3600ULL;
    unsigned long long mins = (secs % 3600ULL) / 60ULL;
    unsigned long long s = secs % 60ULL;
    if (days > 0) {
        snprintf(out, cap, "%llud %02llu:%02llu:%02llu", days, hours, mins, s);
    } else {
        snprintf(out, cap, "%02llu:%02llu:%02llu", hours, mins, s);
    }
}

static ConsoleMinimizeMode parse_console_minimize_mode(const char* s) {
    if (!s || !s[0]) return CONSOLE_MINIMIZE_TRAY;
    if (str_casecmp(s, "taskbar") == 0) return CONSOLE_MINIMIZE_TASKBAR;
    if (str_casecmp(s, "tray") == 0 || str_casecmp(s, "systray") == 0) return CONSOLE_MINIMIZE_TRAY;
    if (str_casecmp(s, "none") == 0 || str_casecmp(s, "no") == 0 || str_casecmp(s, "off") == 0) return CONSOLE_MINIMIZE_NONE;
    return CONSOLE_MINIMIZE_TRAY;
}

static void usage(void) {
    printf("用法：\n"
           "  GitRunSync [RunExePath] [RunExeArg] [--run <exe>] [--arg <arg>]\n"
           "  GitRunSync -s | --create-shortcut\n"
           "\n"
           "选项：\n"
           "  -s, --create-shortcut  创建桌面快捷方式（Linux 下创建 .desktop 文件）\n"
           "  --run <exe>            指定要启动的程序路径\n"
           "  --arg <arg>            传递给程序的参数\n"
           "  -h, --help             显示此帮助信息\n"
           "\n"
           "说明：\n"
           "- 程序会自动从当前目录向上查找 Git 仓库根目录\n"
#ifdef _WIN32
           "- RunStoreApp：优先启动微软应用商店已安装应用（关键字或 AUMID，留空则使用 RunExePath）\n"
           "- RunExePath/--run：要启动的程序路径（例如 C:\\Windows\\System32\\ping.exe）\n"
#else
           "- RunStoreApp：优先启动应用（留空则使用 RunExePath）\n"
           "- RunExePath/--run：要启动的程序路径（例如 /usr/bin/ping）\n"
#endif
           "- RunExeArg/--arg：传递给启动程序的一段参数（可选）\n"
           "- AutoCleanOnConflict：值为 1 时，git pull 遇到冲突自动执行 clean\n");
}

/* 规范化路径：统一分隔符，移除连续的 / 和 \ */
static void normalize_path_inplace(char* path) {
    if (!path || !path[0]) return;

    /* 统一使用 / 作为分隔符 */
    for (char* p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    /* 移除连续的 / */
    char* dst = path;
    char* src = path;
    int last_was_slash = 0;
    while (*src) {
        if (*src == '/') {
            if (!last_was_slash) {
                *dst++ = *src;
                last_was_slash = 1;
            }
        } else {
            *dst++ = *src;
            last_was_slash = 0;
        }
        src++;
    }
    *dst = '\0';

    /* 移除末尾的 / (除非是根目录) */
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
}

/* 检查路径是否包含路径遍历序列 */
static int contains_path_traversal(const char* path) {
    if (!path) return 0;

    /* 检查 /../ 或开头的 ../ */
    const char* p = path;
    while ((p = strstr(p, "..")) != NULL) {
        /* 检查是否是独立的 .. (前后都是 / 或边界) */
        int before_ok = (p == path) || (p > path && (*(p - 1) == '/' || *(p - 1) == '\\'));
        int after_ok = (p[2] == '\0') || (p[2] == '/' || p[2] == '\\');
        if (before_ok && after_ok) {
            return 1;
        }
        p += 2;
    }
    return 0;
}

static char* find_git_repo_root(void) {
    char* cwd = plat_getcwd();
    if (!cwd) {
        plat_print_last_error("获取当前目录失败");
        return NULL;
    }

    /* 检查原始路径是否安全 */
    if (contains_path_traversal(cwd)) {
        fprintf(stderr, "错误：当前路径包含路径遍历序列\n");
        free(cwd);
        return NULL;
    }

    char current[GRS_MAX_PATH];
    strncpy(current, cwd, sizeof(current) - 1);
    current[sizeof(current) - 1] = 0;
    free(cwd);

    /* 规范化路径 */
    normalize_path_inplace(current);

    /* 检查规范化后的路径长度 */
    if (strlen(current) == 0) {
        fprintf(stderr, "错误：当前路径为空\n");
        return NULL;
    }

    /* 限制搜索深度，防止死循环 */
    int max_depth = 100;
    int depth = 0;

    for (;;) {
        if (++depth > max_depth) {
            fprintf(stderr, "错误：搜索 .git 目录超过最大深度\n");
            return NULL;
        }

        char git_dir[GRS_MAX_PATH];
        int n = snprintf(git_dir, sizeof(git_dir), "%s/.git", current);
        if (n < 0 || (size_t)n >= sizeof(git_dir)) {
            fprintf(stderr, "错误：路径过长\n");
            return NULL;
        }

        if (plat_dir_exists(git_dir)) {
            /* 验证找到的目录安全性 */
            if (contains_path_traversal(current)) {
                fprintf(stderr, "错误：找到的仓库路径包含非法序列\n");
                return NULL;
            }
            return strdup(current);
        }

        /* 获取父目录 */
        char* last = strrchr(current, '/');
        if (!last) break;

        *last = '\0';

        /* 检查是否到达根目录 */
        if (current[0] == '\0' || (current[0] == '/' && current[1] == '\0')) {
            break;
        }
    }
    return NULL;
}

/* 处理创建桌面快捷方式请求
 * 从 Git 仓库名提取快捷方式文件名，创建桌面和应用菜单快捷方式
 */
static int handle_create_shortcut(void) {
    char* gitRepoRoot = find_git_repo_root();
    if (!gitRepoRoot) {
        fprintf(stderr, "错误：未找到 Git 仓库根目录。请在 Git 仓库内运行此命令。\n");
        return GRS_ERR_NO_GIT_REPO;
    }

    /* 从路径提取仓库名 */
    const char* repo_name = plat_path_basename(gitRepoRoot);
    if (!repo_name || !repo_name[0]) {
        fprintf(stderr, "错误：无法提取仓库名称\n");
        free(gitRepoRoot);
        return GRS_ERR_INVALID_PATH;
    }

    /* 获取当前可执行文件完整路径 */
    char* exe_dir = plat_get_exe_dir();
    if (!exe_dir) {
        fprintf(stderr, "错误：无法获取程序路径\n");
        free(gitRepoRoot);
        return GRS_ERR_NOT_FOUND;
    }

    char exec_path[GRS_MAX_PATH];
#ifdef _WIN32
    snprintf(exec_path, sizeof(exec_path), "%s\\GitRunSync.exe", exe_dir);
#else
    snprintf(exec_path, sizeof(exec_path), "%s/GitRunSync-Linux", exe_dir);
#endif

    if (!plat_file_exists(exec_path)) {
        /* 尝试当前可执行文件的实际路径 */
        free(exe_dir);
        exe_dir = plat_get_exe_dir();
        snprintf(exec_path, sizeof(exec_path), "%s", exe_dir ? exe_dir : "GitRunSync");
        if (exe_dir) free(exe_dir);
        exe_dir = strdup(exec_path);
    }

    /* 查找图标：配置指定 > icons/ (多格式) > res/ (多格式) > NULL */
    char icon_path[GRS_MAX_PATH] = {0};
    char tmp_path[GRS_MAX_PATH];
    
    /* 支持的图标格式，按优先级排序 */
    const char* icon_exts[] = {".png", ".svg", ".xpm", ".ico", NULL};
    const char* icon_dirs[] = {"icons", "res"};
    
    for (int d = 0; d < 2 && !icon_path[0]; d++) {
        for (int e = 0; icon_exts[e] && !icon_path[0]; e++) {
            snprintf(tmp_path, sizeof(tmp_path), "%s/%s/%s%s", 
                     exe_dir, icon_dirs[d], repo_name, icon_exts[e]);
            if (plat_file_exists(tmp_path)) {
                strncpy(icon_path, tmp_path, sizeof(icon_path) - 1);
            }
        }
    }

    printf("正在创建快捷方式：%s\n", repo_name);
    printf("  程序路径：%s\n", exec_path);
    if (icon_path[0]) {
        printf("  图标路径：%s\n", icon_path);
    }

    int ret = plat_create_desktop_shortcut(repo_name, exec_path, exe_dir,
                                            icon_path[0] ? icon_path : NULL);

    free(exe_dir);
    free(gitRepoRoot);

    if (ret == 0) {
        printf("✓ 快捷方式创建成功！\n");
        return GRS_OK;
    } else {
        fprintf(stderr, "✗ 快捷方式创建失败（错误码：%d）\n", ret);
        return GRS_ERR_SHORTCUT_CREATE_FAILED;
    }
}

static char* resolve_default_run_exe(const char* repoDir, const char* exeDir) {
#ifdef _WIN32
    const char* candidates[] = {
        "ping.exe",
        "C:\\Windows\\System32\\ping.exe",
        NULL
    };
#else
    const char* candidates[] = {
        "ping",
        "/usr/bin/ping",
        "/bin/ping",
        NULL
    };
#endif
    for (int i = 0; candidates[i]; i++) {
        const char* cand = candidates[i];
        if (cand[0] == '/' || (cand[1] == ':')) {
            if (plat_file_exists(cand)) return strdup(cand);
            continue;
        }
        if (repoDir && repoDir[0]) {
            char* p = plat_path_join(repoDir, cand);
            if (p && plat_file_exists(p)) return p;
            free(p);
        }
        if (exeDir && exeDir[0]) {
            char* p = plat_path_join(exeDir, cand);
            if (p && plat_file_exists(p)) return p;
            free(p);
        }
    }
    return NULL;
}

// ------------------------------------------------------------
// 配置模块
// ------------------------------------------------------------
static void init_default_config(Config* cfg) {
    memset(cfg, 0, sizeof(Config));
    strncpy(cfg->runStoreApp, "calc", sizeof(cfg->runStoreApp) - 1);
#ifdef _WIN32
    strncpy(cfg->runExePath, "C:\\Windows\\System32\\ping.exe", sizeof(cfg->runExePath) - 1);
    strncpy(cfg->runExeArg, "127.0.0.1 -n 10", sizeof(cfg->runExeArg) - 1);
#else
    strncpy(cfg->runExePath, "/usr/bin/ping", sizeof(cfg->runExePath) - 1);
    strncpy(cfg->runExeArg, "-c 10 127.0.0.1", sizeof(cfg->runExeArg) - 1);
#endif
    strncpy(cfg->consoleMinimize, "tray", sizeof(cfg->consoleMinimize) - 1);
    cfg->autoCleanOnConflict = false;
    cfg->showWindow = false;  /* 默认静默，0=无窗口 */
}

static int is_config_valid(const Config* cfg) {
    if (!cfg) return 0;
    return (cfg->runExePath[0] == '\0' || plat_file_exists(cfg->runExePath));
}

static char* get_config_file_path(void) {
    char* exeDir = plat_get_exe_dir();
    char* stem = plat_get_exe_stem();
    if (!exeDir || !stem) { free(exeDir); free(stem); return NULL; }
    size_t len = strlen(stem) + 6;
    char* tomlName = (char*)calloc(len, 1);
    snprintf(tomlName, len, "%s.toml", stem);
    char* cfgPath = plat_path_join(exeDir, tomlName);
    free(exeDir); free(stem); free(tomlName);
    return cfgPath;
}

static int write_default_config_template(const Config* cfg, const char* cfgPath) {
    if (!cfg || !cfgPath || !cfgPath[0]) return 0;
    FILE* f = fopen(cfgPath, "w");
    if (!f) return 0;
    fprintf(f,
        "# GitRunSync Configuration (TOML format)\n"
        "# Generated automatically - edit with care\n"
        "\n"
        "[gitsync]\n"
        "# Git repository directory (auto-detect if empty)\n"
        "repo_dir = \"%s\"\n"
        "\n"
        "# Windows: Microsoft Store app keyword or AUMID (with '!')\n"
        "# Linux: .desktop file name (e.g., \"org.gnome.Calculator\")\n"
        "# Priority: RunStoreApp > RunExePath > default\n"
        "run_store_app = \"%s\"\n"
        "\n"
        "# Fallback program path (used when RunStoreApp fails or is empty)\n"
        "run_exe_path = \"%s\"\n"
        "\n"
        "# Arguments passed to the program\n"
        "run_exe_arg = \"%s\"\n"
        "\n"
        "# Auto clean on git conflict: true/false\n"
        "auto_clean_on_conflict = %s\n"
        "\n"
        "[webhook]\n"
        "# Webhook URL for notifications (empty = disabled)\n"
        "url = \"%s\"\n"
        "\n"
        "# Webhook secret for signature verification\n"
        "secret = \"%s\"\n"
        "\n"
        "[ui]\n"
        "# Console minimize mode: tray/taskbar/none\n"
        "console_minimize = \"%s\"\n"
        "\n"
        "# Show window: true = display, false = silent (default)\n"
        "show_window = %s\n",
        cfg->repoDir[0] ? cfg->repoDir : "",
        cfg->runStoreApp[0] ? cfg->runStoreApp : "",
        cfg->runExePath[0] ? cfg->runExePath : "",
        cfg->runExeArg[0] ? cfg->runExeArg : "",
        cfg->autoCleanOnConflict ? "true" : "false",
        cfg->webhookUrl[0] ? cfg->webhookUrl : "",
        cfg->webhookSecret[0] ? cfg->webhookSecret : "",
        cfg->consoleMinimize[0] ? cfg->consoleMinimize : "tray",
        cfg->showWindow ? "true" : "false"
    );
    fclose(f);
    return 1;
}

static int read_config(Config* cfg, const char* cfgPath) {
    if (!cfg || !cfgPath) return 0;

    /* 临时缓冲区用于读取 */
    char tmpRepoDir[GRS_MAX_PATH];
    char tmpRunStoreApp[256];
    char tmpRunExePath[GRS_MAX_PATH];
    char tmpRunExeArg[512];
    char tmpConsoleMinimize[32];
    char tmpWebhookUrl[512];
    char tmpWebhookSecret[128];
    char tmpAutoClean[16];
    char tmpShowWindow[16];

    /* TOML 格式使用 snake_case 键名 */
    toml_read_string(cfgPath, "gitsync", "repo_dir", tmpRepoDir, sizeof(tmpRepoDir), "");
    toml_read_string(cfgPath, "gitsync", "run_store_app", tmpRunStoreApp, sizeof(tmpRunStoreApp), cfg->runStoreApp);
    toml_read_string(cfgPath, "gitsync", "run_exe_path", tmpRunExePath, sizeof(tmpRunExePath), cfg->runExePath);
    toml_read_string(cfgPath, "gitsync", "run_exe_arg", tmpRunExeArg, sizeof(tmpRunExeArg), cfg->runExeArg);
    toml_read_string(cfgPath, "gitsync", "auto_clean_on_conflict", tmpAutoClean, sizeof(tmpAutoClean), cfg->autoCleanOnConflict ? "true" : "false");
    
    toml_read_string(cfgPath, "webhook", "url", tmpWebhookUrl, sizeof(tmpWebhookUrl), "");
    toml_read_string(cfgPath, "webhook", "secret", tmpWebhookSecret, sizeof(tmpWebhookSecret), "");
    
    toml_read_string(cfgPath, "ui", "console_minimize", tmpConsoleMinimize, sizeof(tmpConsoleMinimize), cfg->consoleMinimize);
    toml_read_string(cfgPath, "ui", "show_window", tmpShowWindow, sizeof(tmpShowWindow), cfg->showWindow ? "true" : "false");

    trim_inplace(tmpRunExePath);
    trim_inplace(tmpRunExeArg);
    trim_inplace(tmpRunStoreApp);
    trim_inplace(tmpWebhookUrl);
    trim_inplace(tmpWebhookSecret);

    /* 验证输入安全性 */
    if (!validate_path(tmpRepoDir, "repo_dir")) return 0;
    if (!validate_path(tmpRunExePath, "run_exe_path")) return 0;
    if (!validate_arg(tmpRunExeArg, "run_exe_arg")) return 0;
    if (!validate_arg(tmpRunStoreApp, "run_store_app")) return 0;
    if (!validate_arg(tmpWebhookUrl, "url")) return 0;
    if (!validate_arg(tmpWebhookSecret, "secret")) return 0;

    /* 验证通过后复制到配置结构 */
    strncpy(cfg->repoDir, tmpRepoDir, sizeof(cfg->repoDir) - 1);
    cfg->repoDir[sizeof(cfg->repoDir) - 1] = '\0';
    strncpy(cfg->runStoreApp, tmpRunStoreApp, sizeof(cfg->runStoreApp) - 1);
    cfg->runStoreApp[sizeof(cfg->runStoreApp) - 1] = '\0';
    strncpy(cfg->runExePath, tmpRunExePath, sizeof(cfg->runExePath) - 1);
    cfg->runExePath[sizeof(cfg->runExePath) - 1] = '\0';
    strncpy(cfg->runExeArg, tmpRunExeArg, sizeof(cfg->runExeArg) - 1);
    cfg->runExeArg[sizeof(cfg->runExeArg) - 1] = '\0';
    strncpy(cfg->consoleMinimize, tmpConsoleMinimize, sizeof(cfg->consoleMinimize) - 1);
    cfg->consoleMinimize[sizeof(cfg->consoleMinimize) - 1] = '\0';
    strncpy(cfg->webhookUrl, tmpWebhookUrl, sizeof(cfg->webhookUrl) - 1);
    cfg->webhookUrl[sizeof(cfg->webhookUrl) - 1] = '\0';
    strncpy(cfg->webhookSecret, tmpWebhookSecret, sizeof(cfg->webhookSecret) - 1);
    cfg->webhookSecret[sizeof(cfg->webhookSecret) - 1] = '\0';

    cfg->autoCleanOnConflict = (strcmp(tmpAutoClean, "true") == 0 || strcmp(tmpAutoClean, "1") == 0);
    cfg->showWindow = (strcmp(tmpShowWindow, "true") == 0 || strcmp(tmpShowWindow, "1") == 0);

    return 1;
}

static int write_config(const Config* cfg, const char* cfgPath) {
    if (!cfg || !cfgPath) return 0;
    /* TOML 格式：使用 snake_case 键名 */
    toml_write_string(cfgPath, "gitsync", "repo_dir", cfg->repoDir[0] ? cfg->repoDir : "");
    toml_write_string(cfgPath, "gitsync", "run_store_app", cfg->runStoreApp[0] ? cfg->runStoreApp : "");
    toml_write_string(cfgPath, "gitsync", "run_exe_path", cfg->runExePath[0] ? cfg->runExePath : "");
    toml_write_string(cfgPath, "gitsync", "run_exe_arg", cfg->runExeArg[0] ? cfg->runExeArg : "");
    toml_write_bool(cfgPath, "gitsync", "auto_clean_on_conflict", cfg->autoCleanOnConflict ? 1 : 0);
    toml_write_string(cfgPath, "webhook", "url", cfg->webhookUrl[0] ? cfg->webhookUrl : "");
    toml_write_string(cfgPath, "webhook", "secret", cfg->webhookSecret[0] ? cfg->webhookSecret : "");
    toml_write_string(cfgPath, "ui", "console_minimize", cfg->consoleMinimize[0] ? cfg->consoleMinimize : "tray");
    toml_write_bool(cfgPath, "ui", "show_window", cfg->showWindow ? 1 : 0);
    return 1;
}

static int generate_default_config(const char* cfgPath) {
    Config cfg;
    init_default_config(&cfg);
    if (!cfg.runExePath[0]) {
        char* exeDirForSearch = plat_get_exe_dir();
        char* defaultExe = resolve_default_run_exe(NULL, exeDirForSearch);
        if (defaultExe) {
            strncpy(cfg.runExePath, defaultExe, sizeof(cfg.runExePath) - 1);
            free(defaultExe);
        }
        free(exeDirForSearch);
    }
    return write_default_config_template(&cfg, cfgPath) && read_config(&cfg, cfgPath);
}

static int handle_config_file(Config* cfg) {
    if (!cfg) return 0;
    char* cfgPath = get_config_file_path();
    if (!cfgPath) return 0;
    int success = 0;
    if (plat_file_exists(cfgPath)) {
        if (read_config(cfg, cfgPath)) {
            success = 1;
        } else {
            printf("配置文件无效，将重新生成...\n");
            plat_remove_file(cfgPath);
            success = generate_default_config(cfgPath) && read_config(cfg, cfgPath);
        }
    } else {
        printf("配置文件不存在，将生成默认配置...\n");
        success = generate_default_config(cfgPath) && read_config(cfg, cfgPath);
    }
    free(cfgPath);
    return success;
}

// ------------------------------------------------------------
// 参数解析
// ------------------------------------------------------------
static int parse_args(int argc, char** argv, Args* out) {
    memset(out, 0, sizeof(*out));
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        }
        if (strcmp(argv[i], "--create-shortcut") == 0 || strcmp(argv[i], "-s") == 0) {
            out->createShortcut = 1;
            continue;
        }
        if (strcmp(argv[i], "--run") == 0 && i + 1 < argc) {
            out->runExePath = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--arg") == 0 && i + 1 < argc) {
            out->runExeArg = argv[++i];
            continue;
        }
        if (!out->runExePath) {
            out->runExePath = argv[i];
        } else if (!out->runExeArg) {
            out->runExeArg = argv[i];
        } else {
            fprintf(stderr, "参数过多：%s\n", argv[i]);
            usage();
            return 0;
        }
    }
    return 1;
}

static void apply_args_to_config(const Args* args, Config* cfg) {
    if (!args || !cfg) return;
    if (args->runExePath && args->runExePath[0]) {
        if (strlen(args->runExePath) >= sizeof(cfg->runExePath)) {
            fprintf(stderr, "警告：--run 参数过长，已忽略\n");
        } else if (!validate_path(args->runExePath, "--run")) {
            fprintf(stderr, "警告：--run 参数包含非法字符，已忽略\n");
        } else {
            strncpy(cfg->runExePath, args->runExePath, sizeof(cfg->runExePath) - 1);
            cfg->runExePath[sizeof(cfg->runExePath) - 1] = '\0';
        }
    }
    if (args->runExeArg && args->runExeArg[0]) {
        if (strlen(args->runExeArg) >= sizeof(cfg->runExeArg)) {
            fprintf(stderr, "警告：--arg 参数过长，已忽略\n");
        } else if (!validate_arg(args->runExeArg, "--arg")) {
            fprintf(stderr, "警告：--arg 参数包含非法字符，已忽略\n");
        } else {
            strncpy(cfg->runExeArg, args->runExeArg, sizeof(cfg->runExeArg) - 1);
            cfg->runExeArg[sizeof(cfg->runExeArg) - 1] = '\0';
        }
    }
}

// ------------------------------------------------------------
// Git 流程
// ------------------------------------------------------------
static void get_commit_message(char* out, size_t cap) {
    time_t t = time(NULL);
    struct tm* lt = localtime(&t);
    snprintf(out, cap, "%04d%02d%02d_%02d%02d%02d",
             lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
             lt->tm_hour, lt->tm_min, lt->tm_sec);
}

static int git_pre_sync(const char* repoDir, bool autoCleanOnConflict) {
    printf("\nExecuting: git pull\n");
    unsigned int pullCode = plat_exec(repoDir, "git pull");
    printf("\nExecuting: git status --porcelain=v1\n");
    ExecResult status = plat_exec_capture(repoDir, "git status --porcelain=v1");
    bool hasStatusOutput = (status.output && status.output_len > 0);
    if (hasStatusOutput || pullCode != 0) {
        if (hasStatusOutput) printf("%s\n", status.output);
        printf("\nNote: Git output is abnormal or there are uncommitted changes.\n");
        if (autoCleanOnConflict) {
            printf("AutoCleanOnConflict enabled, executing clean...\n");
            plat_exec(repoDir, "git add -A");
            plat_exec(repoDir, "git stash");
            plat_exec(repoDir, "git reset --hard");
            plat_exec(repoDir, "git pull");
        } else {
            printf("Continuing without clean (AutoCleanOnConflict=0)...\n");
        }
    } else {
        printf("Git status is normal, continuing.\n");
    }
    plat_free_exec_result(&status);
    return 1;
}

static void git_commit_push(const char* repoDir) {
    printf("\nStart committing and pushing changes...\n");
    plat_exec(repoDir, "git add -A");
    char msg[64];
    get_commit_message(msg, sizeof(msg));
    char commitCmd[256];
    snprintf(commitCmd, sizeof(commitCmd), "git commit -m \"%s\"", msg);
    unsigned int commitCode = plat_exec(repoDir, commitCmd);
    if (commitCode != 0) {
        printf("Note: git commit failed (maybe no changes to commit), will continue pull/push.\n");
    }
    printf("\nExecuting: git pull\n");
    unsigned int pull2 = plat_exec(repoDir, "git pull");
    printf("\nExecuting: git push -u\n");
    unsigned int pushCode = plat_exec(repoDir, "git push -u");
    if (pull2 != 0 || pushCode != 0) {
        printf("\nWarning: Git pull/push failed, see logs for details.\n");
    }
}

// ------------------------------------------------------------
// 主入口
// ------------------------------------------------------------
int main(int argc, char** argv) {
    plat_set_utf8_console();

    /* 先解析参数，检查是否需要创建快捷方式（这是独立功能，不需要单实例锁） */
    Args args;
    if (!parse_args(argc, argv, &args)) {
        return GRS_ERR_INVALID_ARGS;
    }

    /* 处理创建快捷方式请求 */
    if (args.createShortcut) {
        return handle_create_shortcut();
    }

    void* instanceLock = plat_single_instance_lock();
    if (!instanceLock) {
        fprintf(stderr, "程序已在运行中，同一时刻只能运行一个实例。\n");
        return GRS_ERR_ALREADY_RUNNING;
    }

    Config cfg;
    if (!handle_config_file(&cfg)) {
        fprintf(stderr, "错误：无法处理配置文件\n");
        plat_single_instance_unlock(instanceLock);
        return GRS_ERR_CONFIG_ERROR;
    }
    apply_args_to_config(&args, &cfg);

    /* 初始化日志系统 */
    char* exeDir = plat_get_exe_dir();
    char logPath[GRS_MAX_PATH];
    snprintf(logPath, sizeof(logPath), "%s/GitRunSync.log", exeDir ? exeDir : ".");
    log_init(logPath, !cfg.showWindow);  /* showWindow=0 时静默，启用日志 */
    free(exeDir);

    /* 非显示窗口模式：隐藏控制台（Windows）或重定向输出（Linux） */
    if (!cfg.showWindow) {
        plat_set_silent_mode(1);
    }

    char* gitRepoRoot = find_git_repo_root();
    if (!gitRepoRoot) {
        log_error("错误：未找到Git仓库根目录。请确保在Git仓库内运行此程序。\n");
        plat_single_instance_unlock(instanceLock);
        return GRS_ERR_NO_GIT_REPO;
    }

    log_write("仓库目录：%s\n", gitRepoRoot);
    log_write("AutoCleanOnConflict：%s\n", cfg.autoCleanOnConflict ? "是" : "否");

    /* 仅在显示窗口模式下启用托盘功能 */
    ConsoleMinimizeMode minimizeMode = CONSOLE_MINIMIZE_NONE;
    if (cfg.showWindow) {
        minimizeMode = parse_console_minimize_mode(cfg.consoleMinimize);
        plat_console_minimize_hook((int)minimizeMode);
    }

    char* resolvedRunExe = NULL;
    const char* runExePath = cfg.runExePath;
    if (!runExePath || !runExePath[0]) {
        char* exeDirForSearch = plat_get_exe_dir();
        resolvedRunExe = resolve_default_run_exe(gitRepoRoot, exeDirForSearch);
        free(exeDirForSearch);
        runExePath = resolvedRunExe;
    }

    if (cfg.runStoreApp[0]) {
        log_write("商店应用（优先）：%s\n", cfg.runStoreApp);
    } else if (runExePath && runExePath[0]) {
        log_write("启动程序：%s\n", runExePath);
        if (cfg.runExeArg[0]) {
            log_write("程序参数：%s\n", cfg.runExeArg);
        }
    }

    if (!git_pre_sync(gitRepoRoot, cfg.autoCleanOnConflict)) {
        free(resolvedRunExe);
        free(gitRepoRoot);
        plat_single_instance_unlock(instanceLock);
        log_close();
        return GRS_ERR_GIT_PULL_FAILED;
    }

    log_write("\n将启动程序并等待其退出，退出后自动提交并推送。\n\n");

    unsigned int runCode = 0;
    int launched = 0;

    if (cfg.runStoreApp[0]) {
        log_write("优先尝试启动应用：%s\n", cfg.runStoreApp);
        runCode = plat_run_store_app_and_wait(cfg.runStoreApp, (int)minimizeMode, gitRepoRoot);
        if (runCode == 0) {
            launched = 1;
        } else {
            log_error("提示：应用启动失败（code=%u），将回退到 RunExePath。\n", runCode);
        }
    }

    if (!launched) {
        if (!runExePath || !runExePath[0]) {
            log_error("错误：未指定要启动的程序，也未在默认位置找到可用的可执行文件。\n");
            log_error("你可以编辑与本程序同名的 tom 配置文件（键：RunStoreApp/RunExePath）或传参：--run \"/usr/bin/ping\"\n");
            free(resolvedRunExe);
            free(gitRepoRoot);
            plat_single_instance_unlock(instanceLock);
            log_close();
            return GRS_ERR_NO_RUN_TARGET;
        }
        log_write("通过 RunExePath 启动：%s\n", runExePath);
        runCode = plat_run_program_and_wait(runExePath, cfg.runExeArg, (int)minimizeMode, gitRepoRoot);
    }

    if (runCode != 0) {
        log_error("提示：被启动程序退出码=%u（仍将继续尝试提交推送）。\n", runCode);
    }

    git_commit_push(gitRepoRoot);

    /* 发送 Webhook 通知（如果配置了） */
    if (cfg.webhookUrl[0]) {
        plat_webhook_send(cfg.webhookUrl, cfg.webhookSecret, "sync_completed",
                         gitRepoRoot, runCode == 0 ? "success" : "partial");
    }

    free(resolvedRunExe);
    free(gitRepoRoot);
    plat_single_instance_unlock(instanceLock);
    log_close();
    
    /* 成功时倒计时退出，失败时等待按键 */
    int exit_code = GRS_OK;
    plat_wait_or_exit(1, 5);
    return exit_code;
}

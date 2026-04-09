#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0601  // Windows 7
#define WINVER 0x0601
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>     /* SHGetSpecialFolderPath, CSIDL */
#include <shlwapi.h>
#include <objbase.h>    /* COM 接口 */
#include <objidl.h>     /* IPersistFile */
#include <wchar.h>
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <wctype.h>
#include <locale.h>
#include <fcntl.h>
#include <io.h>
#include <time.h>

#include "platform.h"
#include "GitRunSync.h"

#ifndef GITRUNSYNC_VERSION_W
#define GITRUNSYNC_VERSION_W L"1.0.0"
#endif

#ifndef GITRUNSYNC_VERSION
#define GITRUNSYNC_VERSION "1.0.0"
#endif

#ifndef _countof
#define _countof(a) (sizeof(a)/sizeof((a)[0]))
#endif

/* ============================================================
 * UTF-8 <-> UTF-16 辅助
 * ============================================================ */
static wchar_t* utf8_to_utf16(const char* s) {
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t* w = (wchar_t*)calloc(n, sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char* utf16_to_utf8(const wchar_t* s) {
    if (!s) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char* c = (char*)calloc(n, sizeof(char));
    if (c) WideCharToMultiByte(CP_UTF8, 0, s, -1, c, n, NULL, NULL);
    return c;
}

static wchar_t* dup_wstr(const wchar_t* s) {
    if (!s) return NULL;
    size_t n = wcslen(s);
    wchar_t* out = (wchar_t*)calloc(n + 1, sizeof(wchar_t));
    if (out) wcscpy(out, s);
    return out;
}

static void trim_wstr_inplace(wchar_t* s) {
    if (!s) return;
    size_t n = wcslen(s);
    size_t i = 0;
    while (i < n && iswspace((wint_t)s[i])) i++;
    if (i > 0) {
        memmove(s, s + i, (n - i + 1) * sizeof(wchar_t));
        n -= i;
    }
    while (n > 0 && iswspace((wint_t)s[n - 1])) {
        s[n - 1] = 0;
        n--;
    }
}

static wchar_t* ps_escape_single_quotes(const wchar_t* s) {
    if (!s) return NULL;
    size_t n = wcslen(s);
    wchar_t* out = (wchar_t*)calloc(n * 2 + 1, sizeof(wchar_t));
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == L'\'') {
            out[j++] = L'\'';
            out[j++] = L'\'';
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = 0;
    return out;
}

static const wchar_t* path_basename_ptr_w(const wchar_t* p) {
    if (!p) return L"";
    const wchar_t* last1 = wcsrchr(p, L'\\');
    const wchar_t* last2 = wcsrchr(p, L'/');
    const wchar_t* last = last1;
    if (last2 && (!last || last2 > last)) last = last2;
    return last ? (last + 1) : p;
}

static void get_repo_display_name_w(wchar_t* out, size_t cap, const wchar_t* repoDir) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!repoDir || !repoDir[0]) return;
    wchar_t tmp[GRS_MAX_PATH * 4];
    wcsncpy_s(tmp, _countof(tmp), repoDir, _TRUNCATE);
    size_t n = wcslen(tmp);
    while (n > 0 && (tmp[n - 1] == L'\\' || tmp[n - 1] == L'/')) {
        tmp[n - 1] = 0;
        n--;
    }
    if (n == 0) return;
    const wchar_t* base = path_basename_ptr_w(tmp);
    if (!base || !base[0]) base = tmp;
    wcsncpy_s(out, cap, base, _TRUNCATE);
}

/* ============================================================
 * 控制台与错误
 * ============================================================ */
void plat_set_utf8_console(void) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF-8");
    /*
     * 注意：不启用 _O_U16TEXT 模式，因为程序中使用的是普通 printf
     * 而不是 wprintf。启用 _O_U16TEXT 会导致 printf 无输出。
     */
}

void plat_print_last_error(const char* prefix) {
    wchar_t* wp = utf8_to_utf16(prefix);
    DWORD err = GetLastError();
    wchar_t* msg = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&msg, 0, NULL);
    if (msg) {
        fwprintf(stderr, L"%ls：%ls（错误码=%lu）\n", wp ? wp : L"", msg, err);
        LocalFree(msg);
    } else {
        fwprintf(stderr, L"%ls（错误码=%lu）\n", wp ? wp : L"", err);
    }
    free(wp);
}

/* ============================================================
 * 路径与文件
 * ============================================================ */
static int file_exists_w(const wchar_t* p) {
    DWORD attr = GetFileAttributesW(p);
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static int dir_exists_w(const wchar_t* p) {
    DWORD attr = GetFileAttributesW(p);
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

char* plat_get_exe_dir(void) {
    wchar_t path[GRS_MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, path, (DWORD)_countof(path));
    if (n == 0 || n >= _countof(path)) return NULL;
    PathRemoveFileSpecW(path);
    return utf16_to_utf8(path);
}

char* plat_get_exe_stem(void) {
    wchar_t path[GRS_MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, path, (DWORD)_countof(path));
    if (n == 0 || n >= _countof(path)) return NULL;
    const wchar_t* base = path_basename_ptr_w(path);
    wchar_t buf[GRS_MAX_PATH * 2];
    wcscpy_s(buf, _countof(buf), base);
    PathRemoveExtensionW(buf);
    return utf16_to_utf8(buf);
}

char* plat_getcwd(void) {
    wchar_t cwd[GRS_MAX_PATH * 2];
    if (!GetCurrentDirectoryW(_countof(cwd), cwd)) return NULL;
    return utf16_to_utf8(cwd);
}

int plat_chdir(const char* dir) {
    wchar_t* w = utf8_to_utf16(dir);
    if (!w) return -1;
    int r = SetCurrentDirectoryW(w) ? 0 : -1;
    free(w);
    return r;
}

char* plat_path_join(const char* a, const char* b) {
    if (!a || !b) return NULL;
    wchar_t* wa = utf8_to_utf16(a);
    wchar_t* wb = utf8_to_utf16(b);
    if (!wa || !wb) { free(wa); free(wb); return NULL; }
    wchar_t buf[GRS_MAX_PATH * 4];
    buf[0] = 0;
    wcscpy_s(buf, _countof(buf), wa);
    PathAppendW(buf, wb);
    free(wa); free(wb);
    return utf16_to_utf8(buf);
}

const char* plat_path_basename(const char* p) {
    if (!p) return "";
    const char* l1 = strrchr(p, '\\');
    const char* l2 = strrchr(p, '/');
    const char* last = l1;
    if (l2 && (!last || l2 > last)) last = l2;
    return last ? (last + 1) : p;
}

int plat_file_exists(const char* p) {
    wchar_t* w = utf8_to_utf16(p);
    if (!w) return 0;
    int r = file_exists_w(w);
    free(w);
    return r;
}

int plat_dir_exists(const char* p) {
    wchar_t* w = utf8_to_utf16(p);
    if (!w) return 0;
    int r = dir_exists_w(w);
    free(w);
    return r;
}

int plat_remove_file(const char* p) {
    wchar_t* w = utf8_to_utf16(p);
    if (!w) return -1;
    BOOL ok = DeleteFileW(w);
    free(w);
    return ok ? 0 : -1;
}

/* ============================================================
 * 单实例
 * ============================================================ */
void* plat_single_instance_lock(void) {
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\GitRunSync_mutex");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return NULL;
    }
    HANDLE* ph = (HANDLE*)malloc(sizeof(HANDLE));
    if (!ph) { CloseHandle(hMutex); return NULL; }
    *ph = hMutex;
    return ph;
}

void plat_single_instance_unlock(void* handle) {
    if (!handle) return;
    CloseHandle(*(HANDLE*)handle);
    free(handle);
}

/* ============================================================
 * 时间 / 休眠 / 消息泵
 * ============================================================ */
static ULONGLONG g_start_tick = 0;

unsigned long long plat_get_tick_ms(void) {
    if (g_start_tick == 0) g_start_tick = GetTickCount64();
    return (unsigned long long)(GetTickCount64() - g_start_tick);
}

void plat_sleep_ms(int ms) {
    if (ms <= 0) return;
    Sleep((DWORD)ms);
}

void plat_pump_messages(int timeout_ms) {
    MSG msg;
    DWORD end = GetTickCount64() + (ULONGLONG)timeout_ms;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if ((int)(end - GetTickCount64()) <= 0) break;
    }
}

void plat_sleep_with_pump(int ms) {
    if (ms <= 0) return;
    ULONGLONG end = GetTickCount64() + (ULONGLONG)ms;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        if (now >= end) break;
        DWORD remain = (DWORD)(end - now);
        if (remain > 50) remain = 50;
        plat_pump_messages((int)remain);
    }
}

/* ============================================================
 * 交互
 * ============================================================ */
void plat_wait_any_key(const char* prompt) {
    if (prompt) {
        wchar_t* wp = utf8_to_utf16(prompt);
        if (wp) wprintf(L"%ls", wp);
        free(wp);
    }
    fflush(stdout);
    _getch();
}

void plat_wait_or_exit(int success, int timeout_seconds) {
    if (success) {
        printf("\n操作成功，%d 秒后自动退出...", timeout_seconds);
        fflush(stdout);
        for (int i = timeout_seconds; i > 0; i--) {
            printf("\r操作成功，%d 秒后自动退出...", i);
            fflush(stdout);
            Sleep(1000);
        }
        printf("\n");
    } else {
        printf("\n按任意键退出...");
        fflush(stdout);
        _getch();
    }
}

/* ============================================================
 * 进程执行（内部宽字符版本）
 * ============================================================ */
typedef struct {
    DWORD exit_code;
    wchar_t* output;
    size_t output_len;
} ExecResultW;

static void free_exec_result_w(ExecResultW* r) {
    if (!r) return;
    free(r->output);
    r->output = NULL;
    r->output_len = 0;
}

static DWORD do_exec_in_dir_w(const wchar_t* repo_dir, const wchar_t* command_line) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    wchar_t* cmd = dup_wstr(command_line);
    if (!cmd) return 1;

    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, repo_dir, &si, &pi);
    if (!ok) {
        fwprintf(stderr, L"创建子进程失败\n");
        free(cmd);
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    free(cmd);
    return code;
}

static ExecResultW do_exec_capture_in_dir_w(const wchar_t* repo_dir, const wchar_t* command_line) {
    ExecResultW r;
    ZeroMemory(&r, sizeof(r));
    r.exit_code = 1;

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        fwprintf(stderr, L"创建管道失败\n");
        return r;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    wchar_t* cmd = dup_wstr(command_line);
    if (!cmd) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return r;
    }

    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, repo_dir, &si, &pi);
    CloseHandle(hWrite);

    if (!ok) {
        fwprintf(stderr, L"创建子进程失败\n");
        CloseHandle(hRead);
        free(cmd);
        return r;
    }

    BYTE tmp[4096];
    BYTE* bytes = NULL;
    size_t cap = 0, len = 0;
    for (;;) {
        DWORD n = 0;
        BOOL read_ok = ReadFile(hRead, tmp, (DWORD)sizeof(tmp), &n, NULL);
        if (!read_ok || n == 0) break;
        if (len + n + 1 > cap) {
            size_t newcap = (cap == 0) ? 8192 : cap * 2;
            while (newcap < len + n + 1) newcap *= 2;
            BYTE* nb = (BYTE*)realloc(bytes, newcap);
            if (!nb) break;
            bytes = nb;
            cap = newcap;
        }
        memcpy(bytes + len, tmp, n);
        len += n;
    }
    if (bytes) bytes[len] = 0;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hRead);
    free(cmd);

    r.exit_code = code;
    if (bytes && len > 0) {
        int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCCH)bytes, (int)len, NULL, 0);
        UINT cp = CP_UTF8;
        if (wlen == 0) {
            cp = CP_ACP;
            wlen = MultiByteToWideChar(cp, 0, (LPCCH)bytes, (int)len, NULL, 0);
        }
        if (wlen > 0) {
            wchar_t* w = (wchar_t*)calloc((size_t)wlen + 1, sizeof(wchar_t));
            if (w) {
                MultiByteToWideChar(cp, 0, (LPCCH)bytes, (int)len, w, wlen);
                r.output = w;
                r.output_len = (size_t)wlen;
            }
        }
    }
    free(bytes);
    return r;
}

unsigned int plat_exec(const char* work_dir, const char* command_line) {
    wchar_t* wdir = utf8_to_utf16(work_dir);
    wchar_t* wcmd = utf8_to_utf16(command_line);
    DWORD code = 1;
    if (wcmd) {
        code = do_exec_in_dir_w(wdir, wcmd);
    }
    free(wdir);
    free(wcmd);
    return (unsigned int)code;
}

ExecResult plat_exec_capture(const char* work_dir, const char* command_line) {
    ExecResult r;
    r.exit_code = 127;
    r.output = NULL;
    r.output_len = 0;

    wchar_t* wdir = utf8_to_utf16(work_dir);
    wchar_t* wcmd = utf8_to_utf16(command_line);
    if (!wcmd) {
        free(wdir);
        return r;
    }

    ExecResultW rw = do_exec_capture_in_dir_w(wdir, wcmd);
    free(wdir);
    free(wcmd);

    r.exit_code = (unsigned int)rw.exit_code;
    if (rw.output && rw.output_len > 0) {
        int n = WideCharToMultiByte(CP_UTF8, 0, rw.output, (int)rw.output_len, NULL, 0, NULL, NULL);
        if (n > 0) {
            char* c = (char*)calloc((size_t)n + 1, sizeof(char));
            if (c) {
                WideCharToMultiByte(CP_UTF8, 0, rw.output, (int)rw.output_len, c, n, NULL, NULL);
                r.output = c;
                r.output_len = (size_t)n;
            }
        }
    }
    free(rw.output);
    return r;
}

void plat_free_exec_result(ExecResult* r) {
    if (!r) return;
    free(r->output);
    r->output = NULL;
    r->output_len = 0;
}

/* ============================================================
 * 程序类型检测 / 启动
 * ============================================================ */
int plat_is_console_program(const char* exe_path) {
    wchar_t* w = utf8_to_utf16(exe_path);
    if (!w) return 0;

    HANDLE hFile = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(w);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    int is_console = 0;
    DWORD read_bytes;

    IMAGE_DOS_HEADER dos_header;
    if (!ReadFile(hFile, &dos_header, sizeof(dos_header), &read_bytes, NULL) || read_bytes != sizeof(dos_header)) {
        CloseHandle(hFile);
        return 0;
    }

    if (dos_header.e_magic != IMAGE_DOS_SIGNATURE) {
        CloseHandle(hFile);
        return 0;
    }

    SetFilePointer(hFile, dos_header.e_lfanew, NULL, FILE_BEGIN);
    IMAGE_NT_HEADERS nt_headers;
    if (!ReadFile(hFile, &nt_headers, sizeof(nt_headers), &read_bytes, NULL) || read_bytes != sizeof(nt_headers)) {
        CloseHandle(hFile);
        return 0;
    }

    if (nt_headers.Signature != IMAGE_NT_SIGNATURE) {
        CloseHandle(hFile);
        return 0;
    }

    if (nt_headers.OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI) {
        is_console = 1;
    }

    CloseHandle(hFile);
    return is_console;
}

unsigned int plat_run_program_and_wait(const char* exe_path, const char* exe_arg,
                                         int minimize_mode, const char* repo_dir) {
    if (!exe_path || !exe_path[0]) {
        fwprintf(stderr, L"错误：未指定要启动的程序路径。\n");
        return 2;
    }
    wchar_t* wpath = utf8_to_utf16(exe_path);
    if (!wpath) return 1;
    if (!file_exists_w(wpath)) {
        fwprintf(stderr, L"错误：要启动的程序不存在：%ls\n", wpath);
        free(wpath);
        return 2;
    }

    wchar_t* warg = utf8_to_utf16(exe_arg);
    wchar_t cmd[GRS_MAX_PATH * 8];
    if (warg && warg[0]) {
        swprintf(cmd, _countof(cmd), L"\"%ls\" %ls", wpath, warg);
    } else {
        swprintf(cmd, _countof(cmd), L"\"%ls\"", wpath);
    }

    wchar_t* cmdBuf = dup_wstr(cmd);
    if (!cmdBuf) { free(wpath); free(warg); return 1; }

    fwprintf(stdout, L"启动程序：%ls\n", cmd);

    DWORD creation_flags = CREATE_NO_WINDOW;  /* 始终使用无窗口模式，避免闪烁 */

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    BOOL ok = CreateProcessW(NULL, cmdBuf, NULL, NULL, FALSE, creation_flags, NULL, NULL, &si, &pi);
    free(cmdBuf);
    free(wpath);
    free(warg);

    if (!ok) {
        fwprintf(stderr, L"启动程序失败\n");
        return 1;
    }

    plat_tray_update(repo_dir, exe_path);

    plat_sleep_with_pump(1500);
    plat_console_minimize_begin(minimize_mode);

    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) break;
        if (r == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            continue;
        }
        break;
    }

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    plat_console_minimize_end(minimize_mode);
    return (unsigned int)code;
}

/* ============================================================
 * Windows Store 应用启动
 * ============================================================ */
static void tray_update_tip(const wchar_t* repoDir, const wchar_t* procName);

static BOOL parse_aumid_exe_line_w(const wchar_t* line, wchar_t* outAumid, size_t outAumidCap,
                                    wchar_t* outExeName, size_t outExeCap) {
    if (!line || !outAumid || outAumidCap == 0 || !outExeName || outExeCap == 0) return FALSE;
    outAumid[0] = 0;
    outExeName[0] = 0;

    wchar_t buf[1024];
    wcsncpy_s(buf, _countof(buf), line, _TRUNCATE);
    trim_wstr_inplace(buf);
    if (!buf[0]) return FALSE;

    wchar_t* sep = wcschr(buf, L'|');
    if (sep) {
        *sep = 0;
        wchar_t* a = buf;
        wchar_t* e = sep + 1;
        trim_wstr_inplace(a);
        trim_wstr_inplace(e);
        if (a[0]) wcsncpy_s(outAumid, outAumidCap, a, _TRUNCATE);
        if (e[0]) wcsncpy_s(outExeName, outExeCap, path_basename_ptr_w(e), _TRUNCATE);
        return outAumid[0] != 0;
    }

    wcsncpy_s(outAumid, outAumidCap, buf, _TRUNCATE);
    return outAumid[0] != 0;
}

static BOOL get_store_aumid_and_exe_from_keyword_w(const wchar_t* workDir, const wchar_t* keyword,
                                                    wchar_t* outAumid, size_t outAumidCap,
                                                    wchar_t* outExeName, size_t outExeCap) {
    outAumid[0] = 0;
    outExeName[0] = 0;
    if (!keyword || !keyword[0]) return FALSE;

    wchar_t* k = ps_escape_single_quotes(keyword);
    if (!k) return FALSE;

    wchar_t psCmd[4096];
    swprintf(psCmd, _countof(psCmd),
        L"powershell.exe -NoProfile -NonInteractive -Command "
        L"\""
        L"$k='%ls';"
        L"$p=Get-AppxPackage | ? { $_.Name -match $k -or $_.PackageFamilyName -match $k -or $_.PublisherDisplayName -match $k -or $_.Description -match $k } | Select-Object -First 1;"
        L"if($p){"
        L"[xml]$m=Get-AppxPackageManifest -Package $p.PackageFullName;"
        L"$a=$m.Package.Applications.Application | ? { $_.Id -and $_.Executable } | Select-Object -First 1;"
        L"if(-not $a){$a=$m.Package.Applications.Application[0]};"
        L"if($a -and $a.Id){$exe=$a.Executable; Write-Output ($p.PackageFamilyName + '!' + $a.Id + '|' + $exe)}"
        L"}"
        L"\"",
        k
    );
    free(k);

    ExecResultW r = do_exec_capture_in_dir_w(workDir, psCmd);
    if (r.exit_code != 0 || !r.output || r.output_len == 0) {
        free_exec_result_w(&r);
        return FALSE;
    }
    trim_wstr_inplace(r.output);
    BOOL ok = parse_aumid_exe_line_w(r.output, outAumid, outAumidCap, outExeName, outExeCap);
    free_exec_result_w(&r);
    return ok;
}

static BOOL get_store_exe_from_aumid_w(const wchar_t* workDir, const wchar_t* aumid,
                                        wchar_t* outExeName, size_t outExeCap) {
    outExeName[0] = 0;
    if (!aumid || !aumid[0]) return FALSE;
    const wchar_t* bang = wcschr(aumid, L'!');
    if (!bang) return FALSE;

    wchar_t pf[256];
    wchar_t appId[256];
    pf[0] = 0;
    appId[0] = 0;
    wcsncpy_s(pf, _countof(pf), aumid, (size_t)(bang - aumid));
    wcsncpy_s(appId, _countof(appId), bang + 1, _TRUNCATE);
    trim_wstr_inplace(pf);
    trim_wstr_inplace(appId);
    if (!pf[0] || !appId[0]) return FALSE;

    wchar_t* pfEsc = ps_escape_single_quotes(pf);
    wchar_t* idEsc = ps_escape_single_quotes(appId);
    if (!pfEsc || !idEsc) {
        free(pfEsc);
        free(idEsc);
        return FALSE;
    }

    wchar_t psCmd[4096];
    swprintf(psCmd, _countof(psCmd),
        L"powershell.exe -NoProfile -NonInteractive -Command "
        L"\""
        L"$pf='%ls';$id='%ls';"
        L"$p=Get-AppxPackage | ? { $_.PackageFamilyName -eq $pf } | Select-Object -First 1;"
        L"if($p){"
        L"[xml]$m=Get-AppxPackageManifest -Package $p.PackageFullName;"
        L"$a=$m.Package.Applications.Application | ? { $_.Id -eq $id } | Select-Object -First 1;"
        L"if(-not $a){$a=$m.Package.Applications.Application | ? { $_.Id -and $_.Executable } | Select-Object -First 1};"
        L"if($a -and $a.Executable){Write-Output $a.Executable}"
        L"}"
        L"\"",
        pfEsc, idEsc
    );
    free(pfEsc);
    free(idEsc);

    ExecResultW r = do_exec_capture_in_dir_w(workDir, psCmd);
    if (r.exit_code != 0 || !r.output || r.output_len == 0) {
        free_exec_result_w(&r);
        return FALSE;
    }
    trim_wstr_inplace(r.output);
    if (!r.output[0]) {
        free_exec_result_w(&r);
        return FALSE;
    }
    wcsncpy_s(outExeName, outExeCap, path_basename_ptr_w(r.output), _TRUNCATE);
    free_exec_result_w(&r);
    return outExeName[0] != 0;
}

static BOOL any_process_exists_by_name_w(const wchar_t* exeName) {
    if (!exeName || !exeName[0]) return FALSE;
    const wchar_t* base = path_basename_ptr_w(exeName);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return FALSE;

    PROCESSENTRY32W pe;
    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);

    BOOL found = FALSE;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, base) == 0) {
                found = TRUE;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return found;
}

static void wait_for_process_exit_by_name_w(const wchar_t* exeName) {
    if (!exeName || !exeName[0]) return;

    BOOL started = FALSE;
    for (int i = 0; i < 60; i++) {
        if (any_process_exists_by_name_w(exeName)) {
            started = TRUE;
            break;
        }
        plat_sleep_with_pump(500);
    }

    if (!started) {
        fwprintf(stderr, L"警告：30 秒内未检测到进程：%ls，后续将不等待直接继续。\n", exeName);
        return;
    }

    for (;;) {
        if (!any_process_exists_by_name_w(exeName)) break;
        plat_sleep_with_pump(1000);
    }
}

unsigned int plat_run_store_app_and_wait(const char* keyword_or_aumid,
                                          int minimize_mode, const char* repo_dir) {
    wchar_t* key = utf8_to_utf16(keyword_or_aumid);
    if (!key || !key[0]) { free(key); return 2; }

    wchar_t workDir[GRS_MAX_PATH * 4];
    GetCurrentDirectoryW((DWORD)_countof(workDir), workDir);

    wchar_t aumid[512];
    wchar_t exeName[256];
    aumid[0] = 0;
    exeName[0] = 0;

    if (wcschr(key, L'!')) {
        wcsncpy_s(aumid, _countof(aumid), key, _TRUNCATE);
        trim_wstr_inplace(aumid);
        (void)get_store_exe_from_aumid_w(workDir, aumid, exeName, _countof(exeName));
    } else {
        if (!get_store_aumid_and_exe_from_keyword_w(workDir, key, aumid, _countof(aumid), exeName, _countof(exeName))) {
            free(key);
            return 2;
        }
    }
    free(key);

    if (!aumid[0]) return 2;

    wchar_t cmd[1024];
    swprintf(cmd, _countof(cmd), L"explorer.exe shell:AppsFolder\\%ls", aumid);
    fwprintf(stdout, L"通过 AUMID 启动：%ls\n", cmd);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    wchar_t* cmdBuf = dup_wstr(cmd);
    if (!cmdBuf) return 1;
    BOOL ok = CreateProcessW(NULL, cmdBuf, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(cmdBuf);

    if (!ok) {
        fwprintf(stderr, L"启动商店应用失败\n");
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    wchar_t* wrepo = utf8_to_utf16(repo_dir);
    if (exeName[0]) {
        tray_update_tip(wrepo, exeName);
    } else {
        wchar_t* fallback = utf8_to_utf16(keyword_or_aumid);
        tray_update_tip(wrepo, fallback);
        free(fallback);
    }
    free(wrepo);

    plat_sleep_with_pump(5000);
    plat_console_minimize_begin(minimize_mode);

    if (exeName[0]) {
        fwprintf(stdout, L"等待进程退出：%ls\n", exeName);
        wait_for_process_exit_by_name_w(exeName);
    } else {
        fwprintf(stderr, L"警告：未能获取商店应用可执行文件名，无法可靠等待退出，将直接继续。\n");
    }

    plat_console_minimize_end(minimize_mode);
    return 0;
}

/* ============================================================
 * 托盘 / 最小化
 * ============================================================ */
#ifndef GITRUNSYNC_VERSION_W
#define GITRUNSYNC_VERSION_W L"1.0.0"
#endif

#define WM_GITRUNSYNC_TRAYICON (WM_APP + 41)
#define GITRUNSYNC_TRAY_TIMER_ID 1
#define GITRUNSYNC_TRAY_TIMER_MS 1000
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (WM_USER + 1)
#endif

static HWND g_console_hwnd = NULL;
static HWND g_tray_hwnd = NULL;
static BOOL g_tray_added = FALSE;
static NOTIFYICONDATAW g_nid;
static wchar_t g_tray_tip[256] = L"GitRunSync";
static wchar_t g_tip_proc[48] = L"-";
static wchar_t g_tip_repo[48] = L"-";
static wchar_t g_build_stamp[16] = L"";
static ConsoleMinimizeMode g_console_minimize_mode = CONSOLE_MINIMIZE_TRAY;
static WNDPROC g_console_prev_wndproc = NULL;
static BOOL g_console_in_hook = FALSE;

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

static void ensure_build_stamp(void) {
    if (g_build_stamp[0]) return;
    const char* d = __DATE__;
    const char* t = __TIME__;
    int mm = month_str_to_int3(d);
    int dd = 0, yyyy = 0, hh = 0, mi = 0;
    if (d) {
        dd = atoi(d + 4);
        yyyy = atoi(d + 7);
    }
    if (t) {
        hh = atoi(t + 0);
        mi = atoi(t + 3);
    }
    int yy = yyyy % 100;
    swprintf(g_build_stamp, _countof(g_build_stamp), L"%02d%02d%02d_%02d%02d", yy, mm, dd, hh, mi);
}

static void format_uptime(wchar_t* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (g_start_tick == 0) {
        wcsncpy_s(out, cap, L"00:00:00", _TRUNCATE);
        return;
    }
    ULONGLONG secs = (GetTickCount64() - g_start_tick) / 1000ULL;
    ULONGLONG days = secs / 86400ULL;
    secs %= 86400ULL;
    ULONGLONG hours = secs / 3600ULL;
    secs %= 3600ULL;
    ULONGLONG mins = secs / 60ULL;
    ULONGLONG s = secs % 60ULL;
    if (days > 0) {
        swprintf(out, cap, L"%llud %02llu:%02llu:%02llu", days, hours, mins, s);
    } else {
        swprintf(out, cap, L"%02llu:%02llu:%02llu", hours, mins, s);
    }
}

static void tray_refresh_tip(void) {
    ensure_build_stamp();
    wchar_t up[32];
    format_uptime(up, _countof(up));

    const wchar_t* p = (g_tip_proc[0] ? g_tip_proc : L"-");
    const wchar_t* r = (g_tip_repo[0] ? g_tip_repo : L"-");

    swprintf(g_tray_tip, _countof(g_tray_tip),
        L"GitRunSync %ls %ls\r\n进程: %ls\r\n仓库: %ls\r\n\r\n运行: %ls",
        GITRUNSYNC_VERSION_W, g_build_stamp, p, r, up);
}

static void tray_update_tip(const wchar_t* repoDir, const wchar_t* procName) {
    wchar_t repoPart[72];
    get_repo_display_name_w(repoPart, _countof(repoPart), repoDir);
    wcsncpy_s(g_tip_repo, _countof(g_tip_repo), repoPart[0] ? repoPart : L"-", _TRUNCATE);

    wchar_t procPart[64];
    wcsncpy_s(procPart, _countof(procPart), procName ? path_basename_ptr_w(procName) : L"", _TRUNCATE);
    wcsncpy_s(g_tip_proc, _countof(g_tip_proc), procPart[0] ? procPart : L"-", _TRUNCATE);

    tray_refresh_tip();

    if (g_tray_added) {
        NOTIFYICONDATAW nid2 = g_nid;
        nid2.uFlags = NIF_TIP;
#ifdef NIF_SHOWTIP
        nid2.uFlags |= NIF_SHOWTIP;
#endif
        wcsncpy_s(nid2.szTip, _countof(nid2.szTip), g_tray_tip, _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &nid2);
    }
}

static void tray_remove_icon(void) {
    if (g_tray_added) {
        BOOL ok = TRUE;
        if (g_tray_hwnd) {
            if (!KillTimer(g_tray_hwnd, GITRUNSYNC_TRAY_TIMER_ID)) {
                fwprintf(stderr, L"警告：停止托盘定时器失败\n");
            }
        }
        if (!Shell_NotifyIconW(NIM_DELETE, &g_nid)) {
            DWORD err = GetLastError();
            fwprintf(stderr, L"警告：删除托盘图标失败 (错误码: %lu)\n", err);
            ok = FALSE;
        }
        g_tray_added = FALSE;
        ZeroMemory(&g_nid, sizeof(g_nid));
        (void)ok; /* 忽略非致命错误 */
    }
}

static void tray_restore_console(void) {
    tray_remove_icon();
    if (g_console_hwnd) {
        ShowWindow(g_console_hwnd, SW_RESTORE);
        SetForegroundWindow(g_console_hwnd);
    }
}

static LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    if (msg == WM_GITRUNSYNC_TRAYICON) {
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK ||
            lParam == NIN_SELECT || lParam == NIN_KEYSELECT) {
            tray_restore_console();
        }
        return 0;
    }
    if (msg == WM_DESTROY) {
        tray_remove_icon();
        return 0;
    }
    if (msg == WM_TIMER) {
        if (wParam == GITRUNSYNC_TRAY_TIMER_ID) {
            tray_refresh_tip();
            if (g_tray_added) {
                NOTIFYICONDATAW nid2 = g_nid;
                nid2.uFlags = NIF_TIP;
#ifdef NIF_SHOWTIP
                nid2.uFlags |= NIF_SHOWTIP;
#endif
                wcsncpy_s(nid2.szTip, _countof(nid2.szTip), g_tray_tip, _TRUNCATE);
                if (!Shell_NotifyIconW(NIM_MODIFY, &nid2)) {
                    fwprintf(stderr, L"警告：更新托盘提示失败\n");
                }
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL tray_ensure_window_created(void) {
    if (g_tray_hwnd) return TRUE;
    HINSTANCE hInst = GetModuleHandleW(NULL);
    const wchar_t* cls = L"GitRunSyncTrayWindowClass";

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = tray_wndproc;
    wc.hInstance = hInst;
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    g_tray_hwnd = CreateWindowExW(
        0, cls, L"",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, hInst, NULL
    );
    return g_tray_hwnd != NULL;
}

static HICON tray_load_small_icon(void) {
    HINSTANCE hInst = GetModuleHandleW(NULL);
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    HICON hIcon = (HICON)LoadImageW(hInst, L"IDI_ICON1", IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (!hIcon) {
        hIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    return hIcon;
}

static BOOL tray_add_icon(const wchar_t* tip) {
    if (!tray_ensure_window_created()) return FALSE;

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_tray_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
#ifdef NIF_SHOWTIP
    g_nid.uFlags |= NIF_SHOWTIP;
#endif
    g_nid.uCallbackMessage = WM_GITRUNSYNC_TRAYICON;
    g_nid.hIcon = tray_load_small_icon();
    if (tip && tip[0]) {
        wcsncpy_s(g_tray_tip, _countof(g_tray_tip), tip, _TRUNCATE);
    } else {
        tray_refresh_tip();
    }
    const wchar_t* finalTip = g_tray_tip;
    if (finalTip && finalTip[0]) {
        wcsncpy_s(g_nid.szTip, _countof(g_nid.szTip), finalTip, _TRUNCATE);
    } else {
        wcscpy_s(g_nid.szTip, _countof(g_nid.szTip), L"GitRunSync");
    }

    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        DWORD err = GetLastError();
        fwprintf(stderr, L"警告：添加托盘图标失败 (错误码: %lu)\n", err);
        return FALSE;
    }
    g_tray_added = TRUE;
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &g_nid)) {
        fwprintf(stderr, L"警告：设置托盘图标版本失败\n");
    }
    if (!SetTimer(g_tray_hwnd, GITRUNSYNC_TRAY_TIMER_ID, GITRUNSYNC_TRAY_TIMER_MS, NULL)) {
        fwprintf(stderr, L"警告：设置托盘定时器失败\n");
    }
    return TRUE;
}

static LRESULT CALLBACK console_hook_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_console_prev_wndproc) {
        if (!g_console_in_hook && g_console_minimize_mode == CONSOLE_MINIMIZE_TRAY) {
            if (msg == WM_SIZE && wParam == SIZE_MINIMIZED) {
                g_console_in_hook = TRUE;
                LRESULT r = CallWindowProcW(g_console_prev_wndproc, hwnd, msg, wParam, lParam);
                g_console_hwnd = hwnd;
                if (!g_tray_added) {
                    (void)tray_add_icon(NULL);
                }
                ShowWindow(hwnd, SW_HIDE);
                g_console_in_hook = FALSE;
                return r;
            }
        }
        return CallWindowProcW(g_console_prev_wndproc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void plat_console_minimize_hook(int mode) {
    g_console_minimize_mode = (ConsoleMinimizeMode)mode;
    HWND hwnd = GetConsoleWindow();
    if (!hwnd) return;
    g_console_hwnd = hwnd;

    if (mode == CONSOLE_MINIMIZE_TRAY) {
        if (!g_console_prev_wndproc) {
            g_console_prev_wndproc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)console_hook_wndproc);
        }
    } else {
        if (g_console_prev_wndproc) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)g_console_prev_wndproc);
            g_console_prev_wndproc = NULL;
        }
    }
}

void plat_console_minimize_begin(int mode) {
    HWND hwnd = GetConsoleWindow();
    if (!hwnd) return;

    if (mode == CONSOLE_MINIMIZE_NONE) return;

    if (mode == CONSOLE_MINIMIZE_TASKBAR) {
        ShowWindow(hwnd, SW_MINIMIZE);
        return;
    }

    g_console_hwnd = hwnd;
    if (tray_add_icon(NULL)) {
        ShowWindow(hwnd, SW_HIDE);
    } else {
        ShowWindow(hwnd, SW_MINIMIZE);
    }
}

void plat_console_minimize_end(int mode) {
    HWND hwnd = GetConsoleWindow();

    if (mode == CONSOLE_MINIMIZE_TRAY) {
        tray_remove_icon();
    }

    if (mode == CONSOLE_MINIMIZE_NONE) return;
    if (hwnd) {
        ShowWindow(hwnd, SW_RESTORE);
    }
}

void plat_tray_update(const char* repo_dir, const char* proc_name) {
    wchar_t* wrepo = utf8_to_utf16(repo_dir);
    wchar_t* wproc = utf8_to_utf16(proc_name);
    tray_update_tip(wrepo, wproc);
    free(wrepo);
    free(wproc);
}

void plat_tray_remove(void) {
    tray_remove_icon();
}

/* ============================================================
 * 静默模式 / 控制台管理
 * ============================================================ */
static int g_silent_mode = 0;
static BOOL g_console_created = FALSE;

void plat_set_silent_mode(int silent) {
    g_silent_mode = silent;
    if (!silent) {
        /* 需要显示窗口：创建控制台 */
        if (!g_console_created) {
            AllocConsole();
            /* 重定向标准输出/错误到控制台 */
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
            freopen_s(&fp, "CONIN$", "r", stdin);
            g_console_created = TRUE;
        }
        /* 显示控制台窗口 */
        HWND hwnd = GetConsoleWindow();
        if (hwnd) {
            ShowWindow(hwnd, SW_SHOW);
        }
    }
    /* silent=1 时什么都不做，保持无窗口（因为程序是 Windows 子系统） */
}

/* ============================================================
 * Webhook 推送（使用 WinHTTP）
 * ============================================================ */
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

/* HMAC-SHA256 计算（简化版，使用 Windows CryptoAPI） */
#include <wincrypt.h>

static void base64_encode(const BYTE* data, DWORD len, char* out, size_t outSize) {
    static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    DWORD i, j;
    for (i = 0, j = 0; i < len && j + 4 < outSize; i += 3) {
        BYTE b1 = data[i];
        BYTE b2 = (i + 1 < len) ? data[i + 1] : 0;
        BYTE b3 = (i + 2 < len) ? data[i + 2] : 0;
        out[j++] = base64[(b1 >> 2) & 0x3F];
        out[j++] = base64[((b1 << 4) | (b2 >> 4)) & 0x3F];
        out[j++] = (i + 1 < len) ? base64[((b2 << 2) | (b3 >> 6)) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? base64[b3 & 0x3F] : '=';
    }
    out[j] = '\0';
}

static void compute_hmac_sha256(const char* key, const char* message, BYTE* outHash) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHmacHash = 0;
    BYTE keyBytes[64] = {0};
    size_t keyLen = strlen(key);
    if (keyLen > 64) keyLen = 64;
    memcpy(keyBytes, key, keyLen);

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return;
    }

    /* 简化的 HMAC 实现：如果密钥太长，直接哈希 */
    if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptHashData(hHash, (BYTE*)message, (DWORD)strlen(message), 0);
        DWORD hashLen = 32;
        CryptGetHashParam(hHash, HP_HASHVAL, outHash, &hashLen, 0);
        CryptDestroyHash(hHash);
    }

    CryptReleaseContext(hProv, 0);
}

int plat_webhook_send(const char* url, const char* secret, const char* event,
                      const char* repo, const char* status) {
    if (!url || !url[0]) return -1;

    /* 解析 URL */
    wchar_t* wurl = utf8_to_utf16(url);
    if (!wurl) return -1;

    URL_COMPONENTSW urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {0};
    wchar_t urlPath[512] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = _countof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = _countof(urlPath);
    urlComp.dwSchemeLength = 1;

    if (!WinHttpCrackUrl(wurl, 0, 0, &urlComp)) {
        free(wurl);
        return -1;
    }
    free(wurl);

    /* 构建 JSON 数据 */
    time_t now = time(NULL);
    struct tm* gmt = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmt);

    char jsonBody[1024];
    snprintf(jsonBody, sizeof(jsonBody),
        "{"
        "\"repo\":\"%s\","
        "\"event\":\"%s\","
        "\"status\":\"%s\","
        "\"timestamp\":\"%s\","
        "\"version\":\"%s\","
        "\"platform\":\"windows\""
        "}",
        repo ? repo : "unknown",
        event ? event : "unknown",
        status ? status : "unknown",
        timestamp,
        GITRUNSYNC_VERSION
    );

    /* 计算签名 */
    char signature[128] = {0};
    if (secret && secret[0]) {
        BYTE hash[32];
        compute_hmac_sha256(secret, jsonBody, hash);
        char hashB64[64];
        base64_encode(hash, 32, hashB64, sizeof(hashB64));
        snprintf(signature, sizeof(signature), "sha256=%s", hashB64);
    }

    /* 发送 HTTP POST */
    HINTERNET hSession = WinHttpOpen(L"GitRunSync/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return -1;

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath, NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    /* 设置请求头 */
    WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json", (ULONG)-1, WINHTTP_ADDREQ_FLAG_ADD);
    if (signature[0]) {
        wchar_t* wsig = utf8_to_utf16(signature);
        if (wsig) {
            wchar_t header[256];
            swprintf(header, _countof(header), L"X-Signature: %s", wsig);
            WinHttpAddRequestHeaders(hRequest, header, (ULONG)-1, WINHTTP_ADDREQ_FLAG_ADD);
            free(wsig);
        }
    }

    /* 发送请求 */
    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  jsonBody, (DWORD)strlen(jsonBody), (DWORD)strlen(jsonBody), 0);
    if (ok) {
        ok = WinHttpReceiveResponse(hRequest, NULL);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return ok ? 0 : -1;
}

/* ------------------------------------------------------------
 * 创建桌面快捷方式 (Windows 版本)
 * ------------------------------------------------------------ */

/* CLSID_ShellLink 定义 */
static const CLSID CLSID_ShellLink_Win = {
    0x00021401, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};

/* IID_IShellLinkA 定义 */
static const IID IID_IShellLinkA_Win = {
    0x000214EE, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};

/* IID_IPersistFile 定义 */
static const IID IID_IPersistFile_Win = {
    0x0000010B, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};

/* 简化版的快捷方式创建（不使用完整的 COM 头文件）
 * 通过动态加载 shell32.dll 实现 */
typedef HRESULT (WINAPI *CoInitializeEx_t)(LPVOID, DWORD);
typedef HRESULT (WINAPI *CoCreateInstance_t)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
typedef void (WINAPI *CoUninitialize_t)(void);

int plat_create_desktop_shortcut(const char* repo_name, const char* exec_path,
                                  const char* work_dir, const char* icon_path) {
    if (!repo_name || !repo_name[0] || !exec_path || !exec_path[0]) {
        return -1;
    }
    (void)work_dir;  /* Windows 版本暂未使用 */

    /* 加载 ole32.dll */
    HMODULE hOle32 = LoadLibraryA("ole32.dll");
    if (!hOle32) {
        fprintf(stderr, "无法加载 ole32.dll\n");
        return -1;
    }

    CoInitializeEx_t pCoInitializeEx = (CoInitializeEx_t)GetProcAddress(hOle32, "CoInitializeEx");
    CoCreateInstance_t pCoCreateInstance = (CoCreateInstance_t)GetProcAddress(hOle32, "CoCreateInstance");
    CoUninitialize_t pCoUninitialize = (CoUninitialize_t)GetProcAddress(hOle32, "CoUninitialize");

    if (!pCoInitializeEx || !pCoCreateInstance || !pCoUninitialize) {
        fprintf(stderr, "无法获取 COM 函数\n");
        FreeLibrary(hOle32);
        return -1;
    }

    /* 初始化 COM */
    HRESULT hr = pCoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "COM 初始化失败: 0x%08X\n", (unsigned int)hr);
        FreeLibrary(hOle32);
        return -1;
    }

    /* IShellLink 接口定义（简化版） */
    typedef struct IShellLinkAVtbl {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void* This, REFIID riid, void** ppv);
        ULONG (STDMETHODCALLTYPE *AddRef)(void* This);
        ULONG (STDMETHODCALLTYPE *Release)(void* This);
        HRESULT (STDMETHODCALLTYPE *GetPath)(void* This, char* pszFile, int cchMaxPath, void* pfd, DWORD fFlags);
        HRESULT (STDMETHODCALLTYPE *GetIDList)(void* This, void** ppidl);
        HRESULT (STDMETHODCALLTYPE *SetIDList)(void* This, void* pidl);
        HRESULT (STDMETHODCALLTYPE *GetDescription)(void* This, char* pszName, int cchMaxName);
        HRESULT (STDMETHODCALLTYPE *SetDescription)(void* This, const char* pszName);
        HRESULT (STDMETHODCALLTYPE *GetWorkingDirectory)(void* This, char* pszDir, int cchMaxPath);
        HRESULT (STDMETHODCALLTYPE *SetWorkingDirectory)(void* This, const char* pszDir);
        HRESULT (STDMETHODCALLTYPE *GetArguments)(void* This, char* pszArgs, int cchMaxPath);
        HRESULT (STDMETHODCALLTYPE *SetArguments)(void* This, const char* pszArgs);
        HRESULT (STDMETHODCALLTYPE *GetHotkey)(void* This, WORD* pwHotkey);
        HRESULT (STDMETHODCALLTYPE *SetHotkey)(void* This, WORD wHotkey);
        HRESULT (STDMETHODCALLTYPE *GetShowCmd)(void* This, int* piShowCmd);
        HRESULT (STDMETHODCALLTYPE *SetShowCmd)(void* This, int iShowCmd);
        HRESULT (STDMETHODCALLTYPE *GetIconLocation)(void* This, char* pszIconPath, int cchIconPath, int* piIcon);
        HRESULT (STDMETHODCALLTYPE *SetIconLocation)(void* This, const char* pszIconPath, int iIcon);
        HRESULT (STDMETHODCALLTYPE *SetRelativePath)(void* This, const char* pszPathRel, DWORD dwReserved);
        HRESULT (STDMETHODCALLTYPE *Resolve)(void* This, HWND hwnd, DWORD fFlags);
        HRESULT (STDMETHODCALLTYPE *SetPath)(void* This, const char* pszFile);
    } IShellLinkAVtbl;

    typedef struct IShellLinkA {
        IShellLinkAVtbl* lpVtbl;
    } IShellLinkA;

    /* IPersistFile 接口定义（简化版） */
    typedef struct IPersistFileVtbl {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void* This, REFIID riid, void** ppv);
        ULONG (STDMETHODCALLTYPE *AddRef)(void* This);
        ULONG (STDMETHODCALLTYPE *Release)(void* This);
        HRESULT (STDMETHODCALLTYPE *GetClassID)(void* This, CLSID* pClassID);
        HRESULT (STDMETHODCALLTYPE *IsDirty)(void* This);
        HRESULT (STDMETHODCALLTYPE *Load)(void* This, const WCHAR* pszFileName, DWORD dwMode);
        HRESULT (STDMETHODCALLTYPE *Save)(void* This, const WCHAR* pszFileName, BOOL fRemember);
        HRESULT (STDMETHODCALLTYPE *SaveCompleted)(void* This, const WCHAR* pszFileName);
        HRESULT (STDMETHODCALLTYPE *GetCurFile)(void* This, WCHAR** ppszFileName);
    } IPersistFileVtbl;

    typedef struct IPersistFile {
        IPersistFileVtbl* lpVtbl;
    } IPersistFile;

    int result = 0;
    IShellLinkA* pShellLink = NULL;
    IPersistFile* pPersistFile = NULL;

    /* 创建 ShellLink 实例 */
    hr = pCoCreateInstance(&CLSID_ShellLink_Win, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IShellLinkA_Win, (void**)&pShellLink);
    if (FAILED(hr) || !pShellLink) {
        fprintf(stderr, "创建 ShellLink 失败: 0x%08X\n", (unsigned int)hr);
        pCoUninitialize();
        FreeLibrary(hOle32);
        return -1;
    }

    /* 设置目标路径 */
    pShellLink->lpVtbl->SetPath(pShellLink, exec_path);

    /* 设置工作目录（程序所在目录） */
    char work_directory[MAX_PATH];
    strncpy(work_directory, exec_path, MAX_PATH - 1);
    work_directory[MAX_PATH - 1] = '\0';
    char* last_slash = strrchr(work_directory, '\\');
    if (last_slash) {
        *last_slash = '\0';
        pShellLink->lpVtbl->SetWorkingDirectory(pShellLink, work_directory);
    }

    /* 设置描述 */
    char description[256];
    snprintf(description, sizeof(description), "GitRunSync - %s", repo_name);
    pShellLink->lpVtbl->SetDescription(pShellLink, description);

    /* 设置图标 */
    if (icon_path && icon_path[0]) {
        pShellLink->lpVtbl->SetIconLocation(pShellLink, icon_path, 0);
    }

    /* 获取 IPersistFile 接口 */
    hr = pShellLink->lpVtbl->QueryInterface(pShellLink, &IID_IPersistFile_Win, (void**)&pPersistFile);
    if (FAILED(hr) || !pPersistFile) {
        fprintf(stderr, "获取 IPersistFile 失败: 0x%08X\n", (unsigned int)hr);
        pShellLink->lpVtbl->Release(pShellLink);
        pCoUninitialize();
        FreeLibrary(hOle32);
        return -1;
    }

    /* 获取桌面路径 */
    char desktop_path[MAX_PATH];
    if (!SHGetSpecialFolderPathA(NULL, desktop_path, CSIDL_DESKTOP, FALSE)) {
        /* 回退到用户目录下的 Desktop */
        char* user_profile = getenv("USERPROFILE");
        if (user_profile) {
            snprintf(desktop_path, sizeof(desktop_path), "%s\\Desktop", user_profile);
        } else {
            fprintf(stderr, "无法获取桌面路径\n");
            result = -1;
            goto cleanup;
        }
    }

    /* 清理仓库名中的特殊字符 */
    char safe_name[256];
    strncpy(safe_name, repo_name, sizeof(safe_name) - 1);
    safe_name[sizeof(safe_name) - 1] = '\0';
    for (char* p = safe_name; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' || 
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|' || *p == ' ') {
            *p = '_';
        }
    }

    /* 保存桌面快捷方式 */
    char lnk_path[MAX_PATH];
    snprintf(lnk_path, sizeof(lnk_path), "%s\\%s.lnk", desktop_path, safe_name);

    /* 转换为宽字符 */
    WCHAR w_lnk_path[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, lnk_path, -1, w_lnk_path, MAX_PATH);

    hr = pPersistFile->lpVtbl->Save(pPersistFile, w_lnk_path, TRUE);
    if (FAILED(hr)) {
        fprintf(stderr, "保存桌面快捷方式失败: 0x%08X\n", (unsigned int)hr);
        result = -1;
        goto cleanup;
    }
    printf("  桌面快捷方式: %s\n", lnk_path);

    /* 获取开始菜单路径 */
    char start_menu_path[MAX_PATH];
    if (SHGetSpecialFolderPathA(NULL, start_menu_path, CSIDL_PROGRAMS, TRUE)) {
        /* 创建 GitRunSync 子目录 */
        char grs_menu_path[MAX_PATH];
        snprintf(grs_menu_path, sizeof(grs_menu_path), "%s\\GitRunSync", start_menu_path);
        CreateDirectoryA(grs_menu_path, NULL);

        /* 保存开始菜单快捷方式 */
        snprintf(lnk_path, sizeof(lnk_path), "%s\\%s.lnk", grs_menu_path, safe_name);
        MultiByteToWideChar(CP_UTF8, 0, lnk_path, -1, w_lnk_path, MAX_PATH);

        /* 重新获取 IPersistFile 接口来保存第二个快捷方式 */
        pPersistFile->lpVtbl->Release(pPersistFile);
        hr = pShellLink->lpVtbl->QueryInterface(pShellLink, &IID_IPersistFile_Win, (void**)&pPersistFile);
        if (SUCCEEDED(hr) && pPersistFile) {
            hr = pPersistFile->lpVtbl->Save(pPersistFile, w_lnk_path, TRUE);
            if (SUCCEEDED(hr)) {
                printf("  开始菜单快捷方式: %s\n", lnk_path);
            }
        }
    }

cleanup:
    if (pPersistFile) pPersistFile->lpVtbl->Release(pPersistFile);
    if (pShellLink) pShellLink->lpVtbl->Release(pShellLink);
    pCoUninitialize();
    FreeLibrary(hOle32);

    return result;
}

#define _GNU_SOURCE

#include "platform.h"
#include "GitRunSync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <locale.h>
#include <termios.h>
#include <stdint.h>
#include <ctype.h>
#include <dirent.h>

/* ------------------------------------------------------------
 * 控制台与编码
 * ------------------------------------------------------------ */
void plat_set_utf8_console(void) {
    setlocale(LC_ALL, "C.UTF-8");
}

void plat_print_last_error(const char* prefix) {
    fprintf(stderr, "%s: %s\n", prefix, strerror(errno));
}

/* ------------------------------------------------------------
 * 路径与文件
 * ------------------------------------------------------------ */
char* plat_get_exe_dir(void) {
    char buf[GRS_MAX_PATH];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return NULL;
    buf[n] = '\0';
    char* last = strrchr(buf, '/');
    if (last) *last = '\0';
    return strdup(buf);
}

char* plat_get_exe_stem(void) {
    char buf[GRS_MAX_PATH];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return NULL;
    buf[n] = '\0';
    const char* base = strrchr(buf, '/');
    base = base ? base + 1 : buf;
    char* out = strdup(base);
    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    return out;
}

char* plat_getcwd(void) {
    char buf[GRS_MAX_PATH];
    if (!getcwd(buf, sizeof(buf))) return NULL;
    return strdup(buf);
}

int plat_chdir(const char* dir) {
    return chdir(dir);
}

char* plat_path_join(const char* a, const char* b) {
    if (!a || !b) return NULL;
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char* out = (char*)malloc(la + lb + 2);
    if (!out) return NULL;
    if (la > 0 && (a[la - 1] == '/' || a[la - 1] == '\\')) {
        snprintf(out, la + lb + 2, "%s%s", a, b);
    } else {
        snprintf(out, la + lb + 2, "%s/%s", a, b);
    }
    return out;
}

const char* plat_path_basename(const char* p) {
    if (!p) return "";
    const char* last = strrchr(p, '/');
    if (!last) last = strrchr(p, '\\');
    return last ? last + 1 : p;
}

int plat_file_exists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

int plat_dir_exists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

int plat_remove_file(const char* p) {
    return unlink(p);
}

/* ------------------------------------------------------------
 * 单实例（文件锁 - 使用可执行文件路径作为唯一标识）
 * ------------------------------------------------------------ */

/* 简单的字符串哈希函数 */
static unsigned int str_hash(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  /* hash * 33 + c */
    }
    return hash;
}

/* 获取安全的锁文件目录 */
static int get_lock_dir(char* out, size_t out_size) {
    /* 优先使用 XDG_RUNTIME_DIR（通常位于 /run/user/<uid>） */
    const char* xdg_runtime = getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime) {
        int n = snprintf(out, out_size, "%s", xdg_runtime);
        if (n > 0 && (size_t)n < out_size) return 1;
    }

    /* 回退到 /tmp，使用 uid 区分用户 */
    int n = snprintf(out, out_size, "/tmp/.GitRunSync_%d", (int)getuid());
    if (n <= 0 || (size_t)n >= out_size) return 0;

    /* 确保目录存在且权限正确 */
    struct stat st;
    if (stat(out, &st) != 0) {
        /* 目录不存在，创建它 */
        if (mkdir(out, 0700) != 0 && errno != EEXIST) {
            return 0;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        /* 存在但不是目录 */
        return 0;
    }

    /* 确保目录权限正确（防止其他用户访问） */
    chmod(out, 0700);
    return 1;
}

void* plat_single_instance_lock(void) {
    /* 获取当前可执行文件的完整路径 */
    char exe_path[GRS_MAX_PATH];
    ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (n <= 0) {
        /* 无法获取可执行文件路径，回退到程序名 */
        const char* fallback = "GitRunSync";
        strncpy(exe_path, fallback, sizeof(exe_path) - 1);
        exe_path[sizeof(exe_path) - 1] = '\0';
    } else {
        exe_path[n] = '\0';
    }

    /* 使用可执行文件路径计算确定性哈希，确保同一程序使用相同的锁文件 */
    unsigned int hash = str_hash(exe_path);

    char lock_dir[GRS_MAX_PATH];
    if (!get_lock_dir(lock_dir, sizeof(lock_dir))) {
        return NULL;
    }

    /* 使用固定名称：基于可执行文件路径哈希，确保同一程序的所有实例使用相同锁文件 */
    char path[GRS_MAX_PATH];
    int len = snprintf(path, sizeof(path), "%s/grs_lock_%08x", lock_dir, hash);
    if (len <= 0 || (size_t)len >= sizeof(path)) {
        return NULL;
    }

    /* 使用 O_CLOEXEC 防止子进程继承 */
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        return NULL;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    /* 尝试获取排他锁，非阻塞 */
    if (fcntl(fd, F_SETLK, &fl) == -1) {
        /* 锁定失败 = 另一个实例持有锁 */
        close(fd);
        return NULL;
    }

    /* 分配句柄结构，包含 fd 和路径用于清理 */
    typedef struct {
        int fd;
        char path[GRS_MAX_PATH];
    } LockHandle;

    LockHandle* handle = (LockHandle*)malloc(sizeof(LockHandle));
    if (!handle) {
        close(fd);
        return NULL;
    }

    handle->fd = fd;
    strncpy(handle->path, path, sizeof(handle->path) - 1);
    handle->path[sizeof(handle->path) - 1] = '\0';

    return handle;
}

void plat_single_instance_unlock(void* handle) {
    if (!handle) return;

    typedef struct {
        int fd;
        char path[GRS_MAX_PATH];
    } LockHandle;

    LockHandle* h = (LockHandle*)handle;
    close(h->fd);
    /* 清理锁文件 */
    unlink(h->path);
    free(h);
}

/* ------------------------------------------------------------
 * 时间 / 休眠
 * ------------------------------------------------------------ */
unsigned long long plat_get_tick_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)ts.tv_nsec / 1000000ULL;
}

void plat_sleep_ms(int ms) {
    if (ms <= 0) return;
    usleep((useconds_t)ms * 1000);
}

void plat_pump_messages(int timeout_ms) {
    (void)timeout_ms;
    /* Linux 控制台程序无托盘消息泵 */
}

void plat_sleep_with_pump(int ms) {
    plat_sleep_ms(ms);
}

/* ------------------------------------------------------------
 * 交互
 * ------------------------------------------------------------ */
void plat_wait_any_key(const char* prompt) {
    if (prompt) printf("%s", prompt);
    fflush(stdout);
    struct termios old, newt;
    if (tcgetattr(STDIN_FILENO, &old) == 0) {
        newt = old;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    }
    getchar();
    if (tcgetattr(STDIN_FILENO, &old) == 0) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
    }
}

void plat_wait_or_exit(int success, int timeout_seconds) {
    /* 检查是否有控制终端（从桌面启动时可能没有） */
    int has_tty = isatty(STDOUT_FILENO) && isatty(STDIN_FILENO);

    if (success) {
        if (has_tty) {
            printf("\n操作成功，%d 秒后自动退出...", timeout_seconds);
            fflush(stdout);
            for (int i = timeout_seconds; i > 0; i--) {
                printf("\r操作成功，%d 秒后自动退出...", i);
                fflush(stdout);
                sleep(1);
            }
            printf("\n");
        } else {
            /* 无终端时直接等待指定时间后退出 */
            sleep(timeout_seconds);
        }
    } else {
        if (has_tty) {
            printf("\n按任意键退出...");
            fflush(stdout);
            struct termios old, newt;
            if (tcgetattr(STDIN_FILENO, &old) == 0) {
                newt = old;
                newt.c_lflag &= ~(ICANON | ECHO);
                tcsetattr(STDIN_FILENO, TCSANOW, &newt);
            }
            getchar();
            if (tcgetattr(STDIN_FILENO, &old) == 0) {
                tcsetattr(STDIN_FILENO, TCSANOW, &old);
            }
        } else {
            /* 无终端时等待 10 秒后退出，给用户时间查看日志 */
            sleep(10);
        }
    }
}

/* ------------------------------------------------------------
 * 安全的命令参数解析（支持引号内的空格）
 * ------------------------------------------------------------ */
static int parse_command_args(const char* cmd, char** argv, int max_args) {
    if (!cmd || !argv || max_args <= 0) return 0;

    char* buf = strdup(cmd);
    if (!buf) return 0;

    int argc = 0;
    char* p = buf;

    while (*p && argc < max_args - 1) {
        /* 跳过前导空白 */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        char* arg_start = p;
        char* arg_end = NULL;

        if (*p == '"' || *p == '\'') {
            /* 引号包围的参数 */
            char quote = *p++;
            arg_start = p;
            while (*p && *p != quote) p++;
            arg_end = p;
            if (*p == quote) p++;
        } else {
            /* 普通参数 */
            while (*p && !isspace((unsigned char)*p)) p++;
            arg_end = p;
        }

        /* 复制参数（限制长度防止溢出） */
        size_t len = arg_end - arg_start;
        if (len > 0 && len < 4096) {
            argv[argc] = (char*)malloc(len + 1);
            if (argv[argc]) {
                memcpy(argv[argc], arg_start, len);
                argv[argc][len] = '\0';
                argc++;
            }
        }
    }

    argv[argc] = NULL;
    free(buf);
    return argc;
}

static void free_argv(char** argv, int argc) {
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
}

/* ------------------------------------------------------------
 * 执行命令
 * ------------------------------------------------------------ */
static ExecResult do_exec(const char* work_dir, const char* cmd, int capture) {
    ExecResult res;
    res.exit_code = 127;
    res.output = NULL;
    res.output_len = 0;

    int pipefd[2];
    if (capture) {
        if (pipe(pipefd) == -1) return res;
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (capture) { close(pipefd[0]); close(pipefd[1]); }
        return res;
    }
    if (pid == 0) {
        if (capture) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
        }
        if (plat_chdir(work_dir) != 0) _exit(127);

        /* 使用 execvp 替代 system，避免 shell 注入 */
        char* argv[64];
        int argc = parse_command_args(cmd, argv, 64);
        if (argc > 0) {
            execvp(argv[0], argv);
        }
        free_argv(argv, argc);
        _exit(127);
    }

    if (capture) {
        close(pipefd[1]);
        size_t cap = 4096, len = 0;
        char* buf = (char*)malloc(cap);
        ssize_t r;
        while (buf && (r = read(pipefd[0], buf + len, cap - len - 1)) > 0) {
            len += (size_t)r;
            if (len + 4096 >= cap) {
                cap += 4096;
                /* 限制最大输出 1MB，防止内存耗尽 */
                if (cap > 1024 * 1024) {
                    cap = 1024 * 1024;
                    break;
                }
                char* nb = (char*)realloc(buf, cap);
                if (!nb) break;
                buf = nb;
            }
        }
        if (buf) buf[len] = '\0';
        close(pipefd[0]);
        res.output = buf;
        res.output_len = len;
    }

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) res.exit_code = (unsigned int)WEXITSTATUS(status);
    return res;
}

unsigned int plat_exec(const char* work_dir, const char* command_line) {
    ExecResult r = do_exec(work_dir, command_line, 0);
    plat_free_exec_result(&r);
    return r.exit_code;
}

ExecResult plat_exec_capture(const char* work_dir, const char* command_line) {
    return do_exec(work_dir, command_line, 1);
}

void plat_free_exec_result(ExecResult* r) {
    if (!r) return;
    free(r->output);
    r->output = NULL;
    r->output_len = 0;
}

/* ------------------------------------------------------------
 * 启动外部程序并等待（使用 execvp 避免 shell 注入）
 * ------------------------------------------------------------ */
unsigned int plat_run_program_and_wait(const char* exe_path, const char* exe_arg,
                                         int minimize_mode, const char* repo_dir) {
    (void)minimize_mode;
    (void)repo_dir;
    if (!exe_path || !exe_path[0]) return 1;

    /* 验证 exe_path 不包含危险字符 */
    for (const char* p = exe_path; *p; p++) {
        if (*p == ';' || *p == '&' || *p == '|' || *p == '$' ||
            *p == '`' || *p == '<' || *p == '>') {
            fprintf(stderr, "错误：程序路径包含非法字符\n");
            return 1;
        }
    }

    pid_t pid = fork();
    if (pid < 0) return 1;

    if (pid == 0) {
        /* 子进程：解析参数并执行 */
        char* argv[64] = {0};
        int argc = 0;

        /* 程序路径 */
        argv[argc++] = strdup(exe_path);

        /* 解析参数 */
        if (exe_arg && exe_arg[0]) {
            char* arg_buf = strdup(exe_arg);
            if (arg_buf) {
                char* p = arg_buf;
                while (*p && argc < 63) {
                    while (*p && isspace((unsigned char)*p)) p++;
                    if (!*p) break;

                    char* arg_start = p;
                    char* arg_end = NULL;

                    if (*p == '"' || *p == '\'') {
                        char quote = *p++;
                        arg_start = p;
                        while (*p && *p != quote) p++;
                        arg_end = p;
                        if (*p == quote) p++;
                    } else {
                        while (*p && !isspace((unsigned char)*p)) p++;
                        arg_end = p;
                    }

                    size_t len = arg_end - arg_start;
                    if (len > 0 && len < 4096) {
                        argv[argc] = (char*)malloc(len + 1);
                        if (argv[argc]) {
                            memcpy(argv[argc], arg_start, len);
                            argv[argc][len] = '\0';
                            argc++;
                        }
                    }
                }
                free(arg_buf);
            }
        }

        argv[argc] = NULL;
        execvp(exe_path, argv);

        /* 执行失败 */
        for (int i = 0; i < argc; i++) free(argv[i]);
        _exit(127);
    }

    /* 父进程：等待子进程 */
    int status;
    if (waitpid(pid, &status, 0) < 0) return 1;
    if (WIFEXITED(status)) return (unsigned int)WEXITSTATUS(status);
    return 1;
}

/* ------------------------------------------------------------
 * Linux 应用发现与启动
 * 通过搜索 .desktop 文件查找并启动应用
 * ------------------------------------------------------------ */

#define MAX_DESKTOP_DIRS 6
#define MAX_DESKTOP_FILES 1024

static char* trim_whitespace(char* s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

/* 移除 Exec 字段中的占位符 (%f, %F, %u, %U, %i, %c, %k 等) */
static void clean_exec_field(char* exec) {
    if (!exec) return;
    char* src = exec;
    char* dst = exec;
    while (*src) {
        if (*src == '%') {
            src++;
            if (*src) {
                switch (*src) {
                    case 'f': case 'F':
                    case 'u': case 'U':
                    case 'i': case 'c': case 'k':
                    case 'd': case 'D':
                    case 'n': case 'N':
                    case 'v': case 'm':
                        src++;
                        continue;
                    default:
                        break;
                }
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';
    trim_whitespace(exec);
}

/* 从 .desktop 文件提取 Exec 值 */
static char* parse_desktop_exec(const char* desktop_path, char* name_out, size_t name_cap) {
    FILE* f = fopen(desktop_path, "r");
    if (!f) return NULL;

    char line[4096];
    char* exec_value = NULL;
    int in_desktop_entry = 0;

    while (fgets(line, sizeof(line), f)) {
        char* trimmed = trim_whitespace(line);
        
        if (strcmp(trimmed, "[Desktop Entry]") == 0) {
            in_desktop_entry = 1;
            continue;
        }
        
        if (trimmed[0] == '[') {
            in_desktop_entry = 0;
            continue;
        }
        
        if (!in_desktop_entry) continue;
        
        if (strncmp(trimmed, "Name=", 5) == 0 && name_out && name_cap > 0) {
            strncpy(name_out, trimmed + 5, name_cap - 1);
            name_out[name_cap - 1] = '\0';
        }
        
        if (strncmp(trimmed, "Exec=", 5) == 0) {
            free(exec_value);
            exec_value = strdup(trimmed + 5);
        }
        
        if (strncmp(trimmed, "NoDisplay=", 10) == 0) {
            if (strcmp(trimmed + 10, "true") == 0 || strcmp(trimmed + 10, "True") == 0) {
                free(exec_value);
                exec_value = NULL;
                fclose(f);
                return NULL;
            }
        }
    }
    
    fclose(f);
    
    if (exec_value) {
        clean_exec_field(exec_value);
    }
    
    return exec_value;
}

/* 检查文件名是否包含关键词（区分大小写子字符串匹配） */
static int filename_contains_keyword(const char* filename, const char* keyword) {
    if (!filename || !keyword) return 0;
    return strstr(filename, keyword) != NULL;
}

/* 检查字符串是否包含关键词 */
static int string_contains_keyword(const char* str, const char* keyword) {
    if (!str || !keyword) return 0;
    return strstr(str, keyword) != NULL;
}

/* 在目录中搜索匹配的应用 */
static char* search_desktop_in_dir(const char* dir_path, const char* keyword, char* name_out, size_t name_cap) {
    DIR* dir = opendir(dir_path);
    if (!dir) return NULL;

    struct dirent* entry;
    char* best_match = NULL;
    char best_name[256] = {0};

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG && entry->d_type != DT_LNK) continue;
        
        const char* filename = entry->d_name;
        size_t len = strlen(filename);
        if (len < 8 || strcmp(filename + len - 8, ".desktop") != 0) continue;
        
        char desktop_path[GRS_MAX_PATH];
        snprintf(desktop_path, sizeof(desktop_path), "%s/%s", dir_path, filename);
        
        char name[256] = {0};
        char* exec = parse_desktop_exec(desktop_path, name, sizeof(name));
        if (!exec) continue;
        
        /* 优先匹配 Name 字段 */
        int name_match = string_contains_keyword(name, keyword);
        int exec_match = string_contains_keyword(exec, keyword);
        int file_match = filename_contains_keyword(filename, keyword);
        
        if (name_match || exec_match || file_match) {
            if (!best_match || name_match) {
                free(best_match);
                best_match = exec;
                strncpy(best_name, name, sizeof(best_name) - 1);
                if (name_out && name_cap > 0 && name[0]) {
                    strncpy(name_out, name, name_cap - 1);
                    name_out[name_cap - 1] = '\0';
                }
                if (name_match) break;  /* Name 精确匹配，直接返回 */
            } else {
                free(exec);
            }
        } else {
            free(exec);
        }
    }
    
    closedir(dir);
    return best_match;
}

/* 在目录中搜索文件名匹配的可执行文件 */
static char* search_executable_in_dir(const char* dir_path, const char* keyword) {
    DIR* dir = opendir(dir_path);
    if (!dir) return NULL;

    struct dirent* entry;
    char* best_match = NULL;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG && entry->d_type != DT_LNK) continue;
        
        const char* filename = entry->d_name;
        
        if (filename_contains_keyword(filename, keyword)) {
            char full_path[GRS_MAX_PATH];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, filename);
            
            /* 检查是否可执行 */
            if (access(full_path, X_OK) == 0) {
                free(best_match);
                best_match = strdup(full_path);
                break;
            }
        }
    }
    
    closedir(dir);
    return best_match;
}

/* 检查字符串是否包含子字符串（区分大小写） */
static int str_contains(const char* haystack, const char* needle) {
    if (!haystack || !needle) return 0;
    return strstr(haystack, needle) != NULL;
}

/* 主搜索函数：按顺序搜索多个目录 */
static char* find_linux_app(const char* keyword, char* name_out, size_t name_cap) {
    if (!keyword || !keyword[0]) return NULL;
    
    char* home = getenv("HOME");
    
    /* 步骤1：优先搜索 ~/applications 目录（AppImage 通常放在这里） */
    if (home) {
        char user_apps[GRS_MAX_PATH];
        snprintf(user_apps, sizeof(user_apps), "%s/applications", home);
        char* exec = search_executable_in_dir(user_apps, keyword);
        if (exec) {
            if (name_out && name_cap > 0) {
                strncpy(name_out, keyword, name_cap - 1);
                name_out[name_cap - 1] = '\0';
            }
            return exec;
        }
    }
    
    /* 步骤2：搜索 .desktop 文件 */
    const char* desktop_dirs[MAX_DESKTOP_DIRS] = {
        "/usr/share/applications",
        "/usr/local/share/applications",
        NULL,  /* ~/.local/share/applications - 占位符 */
        "/var/lib/snapd/desktop/applications",
        NULL
    };
    
    /* 设置用户目录 */
    if (home) {
        static char user_local_apps[GRS_MAX_PATH];
        snprintf(user_local_apps, sizeof(user_local_apps), "%s/.local/share/applications", home);
        desktop_dirs[2] = user_local_apps;
    }
    
    char* exe_dir = plat_get_exe_dir();
    
    for (int i = 0; i < MAX_DESKTOP_DIRS && desktop_dirs[i]; i++) {
        char* exec = search_desktop_in_dir(desktop_dirs[i], keyword, name_out, name_cap);
        if (exec) {
            /* 如果 .desktop 文件指向 GitRunSync 自身，跳过它 */
            if (exe_dir && str_contains(exec, exe_dir) && str_contains(exec, "GitRunSync")) {
                free(exec);
                exec = NULL;
                continue;
            }
            free(exe_dir);
            return exec;
        }
    }
    
    free(exe_dir);
    return NULL;
}

unsigned int plat_run_store_app_and_wait(const char* keyword_or_aumid,
                                          int minimize_mode, const char* repo_dir) {
    (void)minimize_mode;
    (void)repo_dir;
    
    if (!keyword_or_aumid || !keyword_or_aumid[0]) return 1;
    
    char app_name[256] = {0};
    char* exec_cmd = find_linux_app(keyword_or_aumid, app_name, sizeof(app_name));
    
    if (!exec_cmd) {
        fprintf(stderr, "提示：未找到匹配 \"%s\" 的应用\n", keyword_or_aumid);
        return 1;
    }
    
    printf("找到应用：%s\n", app_name[0] ? app_name : keyword_or_aumid);
    printf("执行命令：%s\n", exec_cmd);
    
    /* 解析并执行命令 */
    char* argv[64] = {0};
    int argc = 0;
    char* cmd_copy = strdup(exec_cmd);
    if (!cmd_copy) {
        free(exec_cmd);
        return 1;
    }
    
    char* p = cmd_copy;
    while (*p && argc < 63) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        
        char* arg_start = p;
        char* arg_end = NULL;
        
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            arg_start = p;
            while (*p && *p != quote) p++;
            arg_end = p;
            if (*p == quote) p++;
        } else {
            while (*p && !isspace((unsigned char)*p)) p++;
            arg_end = p;
        }
        
        size_t len = arg_end - arg_start;
        if (len > 0 && len < 4096) {
            argv[argc] = (char*)malloc(len + 1);
            if (argv[argc]) {
                memcpy(argv[argc], arg_start, len);
                argv[argc][len] = '\0';
                argc++;
            }
        }
    }
    
    free(cmd_copy);
    
    if (argc == 0) {
        free(exec_cmd);
        return 1;
    }
    
    /* 启动进程 */
    pid_t pid = fork();
    if (pid < 0) {
        free(exec_cmd);
        for (int i = 0; i < argc; i++) free(argv[i]);
        return 1;
    }
    
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    
    /* 等待进程结束 */
    int status;
    waitpid(pid, &status, 0);
    
    free(exec_cmd);
    for (int i = 0; i < argc; i++) free(argv[i]);
    
    if (WIFEXITED(status)) return (unsigned int)WEXITSTATUS(status);
    return 1;
}

int plat_is_console_program(const char* exe_path) {
    (void)exe_path;
    return 1;
}

/* ------------------------------------------------------------
 * 托盘 / 最小化（Linux 空实现）
 * ------------------------------------------------------------ */
void plat_console_minimize_hook(int mode) { (void)mode; }
void plat_console_minimize_begin(int mode) { (void)mode; }
void plat_console_minimize_end(int mode) { (void)mode; }
void plat_tray_update(const char* repo_dir, const char* proc_name) {
    (void)repo_dir; (void)proc_name;
}
void plat_tray_remove(void) {}

/* ------------------------------------------------------------
 * 静默模式
 * ------------------------------------------------------------ */
static int g_silent_mode = 0;

void plat_set_silent_mode(int silent) {
    g_silent_mode = silent;
    if (silent) {
        /* 静默模式：关闭标准输出/错误 */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
    }
}

/* ------------------------------------------------------------
 * Webhook 推送（使用 socket）
 * ------------------------------------------------------------ */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

static void base64_encode(const unsigned char* data, int len, char* out, int outSize) {
    static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i, j;
    for (i = 0, j = 0; i < len && j + 4 < outSize; i += 3) {
        unsigned char b1 = data[i];
        unsigned char b2 = (i + 1 < len) ? data[i + 1] : 0;
        unsigned char b3 = (i + 2 < len) ? data[i + 2] : 0;
        out[j++] = base64[(b1 >> 2) & 0x3F];
        out[j++] = base64[((b1 << 4) | (b2 >> 4)) & 0x3F];
        out[j++] = (i + 1 < len) ? base64[((b2 << 2) | (b3 >> 6)) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? base64[b3 & 0x3F] : '=';
    }
    out[j] = '\0';
}

int plat_webhook_send(const char* url, const char* secret, const char* event,
                      const char* repo, const char* status) {
    if (!url || !url[0]) return -1;

    /* 解析 URL */
    char hostname[256] = {0};
    char path[512] = {0};
    int port = 80;
    int use_ssl = 0;

    if (strncmp(url, "https://", 8) == 0) {
        use_ssl = 1;
        port = 443;
        sscanf(url + 8, "%255[^/]%s", hostname, path);
    } else if (strncmp(url, "http://", 7) == 0) {
        sscanf(url + 7, "%255[^/]%s", hostname, path);
    } else {
        return -1;
    }

    /* 检查端口号 */
    char* portPtr = strchr(hostname, ':');
    if (portPtr) {
        *portPtr = '\0';
        port = atoi(portPtr + 1);
    }

    if (!path[0]) strcpy(path, "/");

    /* 构建 JSON */
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
        "\"platform\":\"linux\""
        "}",
        repo ? repo : "unknown",
        event ? event : "unknown",
        status ? status : "unknown",
        timestamp,
        "1.0.0"
    );

    /* 计算签名 */
    char signature[128] = {0};
    if (secret && secret[0]) {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen = 0;
        HMAC(EVP_sha256(), secret, strlen(secret),
             (unsigned char*)jsonBody, strlen(jsonBody),
             hash, &hashLen);
        char hashB64[64];
        base64_encode(hash, hashLen, hashB64, sizeof(hashB64));
        snprintf(signature, sizeof(signature), "sha256=%s", hashB64);
    }

    /* 创建 socket */
    struct hostent* server = gethostbyname(hostname);
    if (!server) return -1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in servAddr;
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(port);
    memcpy(&servAddr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
        close(sock);
        return -1;
    }

    /* 构建 HTTP 请求 */
    char request[2048];
    int reqLen = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "%s%s%s"
        "\r\n"
        "%s",
        path,
        hostname,
        strlen(jsonBody),
        signature[0] ? "X-Signature: " : "",
        signature[0] ? signature : "",
        signature[0] ? "\r\n" : "",
        jsonBody
    );

    /* 发送请求 */
    int sent = send(sock, request, reqLen, 0);
    close(sock);

    return (sent == reqLen) ? 0 : -1;
}

/* ------------------------------------------------------------
 * 创建桌面快捷方式
 * ------------------------------------------------------------ */
int plat_create_desktop_shortcut(const char* repo_name, const char* exec_path,
                                  const char* work_dir, const char* icon_path) {
    if (!repo_name || !repo_name[0] || !exec_path || !exec_path[0]) {
        return -1;
    }

    int ret = 0;
    char* home = getenv("HOME");
    if (!home) return -1;

    /* 清理仓库名中的特殊字符，生成安全的文件名 */
    char safe_name[256];
    strncpy(safe_name, repo_name, sizeof(safe_name) - 1);
    safe_name[sizeof(safe_name) - 1] = '\0';
    
    /* 将不安全字符替换为下划线 */
    for (char* p = safe_name; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ' ' || *p == '\t') {
            *p = '_';
        }
    }

    /* 构建 .desktop 文件内容
     * Exec 使用 bash -c 格式，确保在正确的工作目录运行
     */
    char desktop_content[2048];
    int len;
    
    if (work_dir && work_dir[0]) {
        /* 有工作目录：使用 bash -c "cd dir && ./program" */
        len = snprintf(desktop_content, sizeof(desktop_content),
            "[Desktop Entry]\n"
            "Name=%s\n"
            "Comment=GitRunSync - %s\n"
            "Exec=bash -c \"cd %s && %s\"\n"
            "Type=Application\n"
            "Terminal=false\n"
            "Categories=Office;Development;\n"
            "StartupNotify=true\n",
            repo_name,
            repo_name,
            work_dir,
            exec_path
        );
    } else {
        /* 无工作目录：直接执行 */
        len = snprintf(desktop_content, sizeof(desktop_content),
            "[Desktop Entry]\n"
            "Name=%s\n"
            "Comment=GitRunSync - %s\n"
            "Exec=%s\n"
            "Type=Application\n"
            "Terminal=false\n"
            "Categories=Office;Development;\n"
            "StartupNotify=true\n",
            repo_name,
            repo_name,
            exec_path
        );
    }

    /* 添加图标（如果提供） */
    if (icon_path && icon_path[0]) {
        snprintf(desktop_content + len, sizeof(desktop_content) - len,
            "Icon=%s\n", icon_path);
    }

    /* 1. 创建桌面快捷方式 */
    char desktop_path[GRS_MAX_PATH];
    snprintf(desktop_path, sizeof(desktop_path), "%s/Desktop/%s.desktop", home, safe_name);
    
    FILE* f = fopen(desktop_path, "w");
    if (!f) {
        /* Desktop 目录可能不存在，尝试创建 */
        char desktop_dir[GRS_MAX_PATH];
        snprintf(desktop_dir, sizeof(desktop_dir), "%s/Desktop", home);
        mkdir(desktop_dir, 0755);
        
        f = fopen(desktop_path, "w");
        if (!f) {
            fprintf(stderr, "无法创建桌面快捷方式：%s\n", desktop_path);
            ret = -1;
        }
    }
    
    if (f) {
        fprintf(f, "%s", desktop_content);
        fclose(f);
        chmod(desktop_path, 0755);
        printf("  桌面快捷方式：%s\n", desktop_path);
    }

    /* 2. 创建应用菜单快捷方式 */
    char apps_dir[GRS_MAX_PATH];
    snprintf(apps_dir, sizeof(apps_dir), "%s/.local/share/applications", home);
    mkdir(apps_dir, 0755); /* 确保目录存在 */
    
    char apps_path[GRS_MAX_PATH];
    snprintf(apps_path, sizeof(apps_path), "%s/%s.desktop", apps_dir, safe_name);
    
    f = fopen(apps_path, "w");
    if (f) {
        fprintf(f, "%s", desktop_content);
        fclose(f);
        chmod(apps_path, 0755);
        printf("  应用菜单快捷方式：%s\n", apps_path);
        
        /* 更新应用菜单缓存 */
        char update_cmd[GRS_MAX_PATH + 50];
        snprintf(update_cmd, sizeof(update_cmd), "update-desktop-database \"%s\" 2>/dev/null", apps_dir);
        system(update_cmd);
    } else {
        fprintf(stderr, "无法创建应用菜单快捷方式：%s\n", apps_path);
        if (ret == 0) ret = -2; /* 桌面成功但菜单失败 */
    }

    return ret;
}

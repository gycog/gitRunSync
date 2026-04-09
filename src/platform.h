#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------
 * 跨平台抽象层接口
 * 内部统一使用 char* (UTF-8)，各平台实现自行处理本地编码转换
 * ------------------------------------------------------------ */

/* 命令执行结果 */
typedef struct {
    unsigned int exit_code;
    char* output;
    size_t output_len;
} ExecResult;

/* 控制台与编码 */
void plat_set_utf8_console(void);

/* 错误输出 */
void plat_print_last_error(const char* prefix);

/* 进程执行（不捕获输出，返回退出码） */
unsigned int plat_exec(const char* work_dir, const char* command_line);

/* 进程执行（捕获 stdout/stderr） */
ExecResult plat_exec_capture(const char* work_dir, const char* command_line);

/* 释放 ExecResult 内部资源 */
void plat_free_exec_result(ExecResult* r);

/* 启动普通程序并等待退出。
 * minimize_mode: 0=taskbar, 1=tray, 2=none（仅 Windows 生效） */
unsigned int plat_run_program_and_wait(const char* exe_path, const char* exe_arg,
                                         int minimize_mode, const char* repo_dir);

/* 启动 Windows Store 应用并等待退出（Windows 专属；Linux 直接返回失败） */
unsigned int plat_run_store_app_and_wait(const char* keyword_or_aumid,
                                          int minimize_mode, const char* repo_dir);

/* 判断 PE 文件是否为控制台子系统（Windows 专属；Linux 默认真） */
int plat_is_console_program(const char* exe_path);

/* 路径与文件 */
char* plat_get_exe_dir(void);
char* plat_get_exe_stem(void);
char* plat_getcwd(void);
int plat_chdir(const char* dir);
char* plat_path_join(const char* a, const char* b);
const char* plat_path_basename(const char* p);
int plat_file_exists(const char* p);
int plat_dir_exists(const char* p);
int plat_remove_file(const char* p);

/* 单实例 */
void* plat_single_instance_lock(void);
void plat_single_instance_unlock(void* handle);

/* 时间 */
unsigned long long plat_get_tick_ms(void);
void plat_sleep_ms(int ms);

/* 消息泵（Windows 托盘使用；Linux 空实现） */
void plat_pump_messages(int timeout_ms);
void plat_sleep_with_pump(int ms);

/* 交互 */
void plat_wait_any_key(const char* prompt);

/* 倒计时退出（成功时自动退出，失败时等待按键） */
void plat_wait_or_exit(int success, int timeout_seconds);

/* 托盘 / 最小化（Windows 专属；Linux 空实现） */
void plat_console_minimize_hook(int mode);
void plat_console_minimize_begin(int mode);
void plat_console_minimize_end(int mode);
void plat_tray_update(const char* repo_dir, const char* proc_name);
void plat_tray_remove(void);

/* 静默模式 */
void plat_set_silent_mode(int silent);

/* Webhook 推送 */
int plat_webhook_send(const char* url, const char* secret, const char* event,
                      const char* repo, const char* status);

/* 创建桌面快捷方式
 * repo_name: Git 仓库名称（用作快捷方式文件名）
 * exec_path: 可执行文件路径
 * work_dir:  工作目录（用于 Exec 行）
 * icon_path: 图标路径（可为 NULL）
 * 返回: 0=成功, 非0=失败 */
int plat_create_desktop_shortcut(const char* repo_name, const char* exec_path,
                                  const char* work_dir, const char* icon_path);

#ifdef __cplusplus
}
#endif

#endif

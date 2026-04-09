/*
 * GitRunSync 统一错误码定义
 */

#ifndef GRS_ERROR_CODES_H
#define GRS_ERROR_CODES_H

#ifdef __cplusplus
extern "C" {
#endif

/* 成功 */
#define GRS_OK                          0

/* 通用错误 (1-9) */
#define GRS_ERR_GENERIC                 1
#define GRS_ERR_INVALID_PARAM           2
#define GRS_ERR_OUT_OF_MEMORY           3
#define GRS_ERR_NOT_FOUND               4
#define GRS_ERR_ACCESS_DENIED           5
#define GRS_ERR_TIMEOUT                 6

/* 程序状态错误 (10-19) */
#define GRS_ERR_ALREADY_RUNNING         10
#define GRS_ERR_INVALID_ARGS            11
#define GRS_ERR_CONFIG_ERROR            12
#define GRS_ERR_NO_GIT_REPO             13
#define GRS_ERR_NO_RUN_TARGET           14

/* 文件/IO 错误 (20-29) */
#define GRS_ERR_FILE_NOT_FOUND          20
#define GRS_ERR_FILE_ACCESS             21
#define GRS_ERR_PATH_TOO_LONG           22
#define GRS_ERR_INVALID_PATH            23

/* 执行错误 (30-39) */
#define GRS_ERR_EXEC_FAILED             30
#define GRS_ERR_CHILD_CRASH             31
#define GRS_ERR_CHILD_TIMEOUT           32

/* 平台特定错误 (40-49) */
#define GRS_ERR_PLATFORM_INIT           40
#define GRS_ERR_CONSOLE_ERROR           41
#define GRS_ERR_TRAY_ERROR              42

/* Git 操作错误 (50-59) */
#define GRS_ERR_GIT_PULL_FAILED         50
#define GRS_ERR_GIT_COMMIT_FAILED       51
#define GRS_ERR_GIT_PUSH_FAILED         52

/* 快捷方式错误 (60-69) */
#define GRS_ERR_SHORTCUT_CREATE_FAILED  60

/* 退出码转换宏 */
#define GRS_EXIT_FROM_CHILD_CODE(code)  ((code) & 0xFF)

#ifdef __cplusplus
}
#endif

#endif /* GRS_ERROR_CODES_H */

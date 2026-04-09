#ifndef GRS_INI_H
#define GRS_INI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 读取 INI 键值；成功返回 1，失败返回 0 并写入 default_val */
int ini_read(const char* path, const char* section, const char* key,
             char* out, size_t out_cap, const char* default_val);

/* 读取整数 */
int ini_read_int(const char* path, const char* section, const char* key, int default_val);

/* 写入/更新 INI 键值；成功返回 1 */
int ini_write(const char* path, const char* section, const char* key, const char* value);

#ifdef __cplusplus
}
#endif

#endif

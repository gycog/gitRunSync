#ifndef TOML_CONFIG_H
#define TOML_CONFIG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TOML 配置读取 - 简化版，支持基本键值对 */
int toml_read_string(const char* path, const char* section, const char* key,
                     char* out, size_t out_cap, const char* default_val);

int toml_read_int(const char* path, const char* section, const char* key,
                  int default_val);

int toml_read_bool(const char* path, const char* section, const char* key,
                   int default_val);

/* 写入配置 */
int toml_write_string(const char* path, const char* section, const char* key,
                      const char* val);

int toml_write_int(const char* path, const char* section, const char* key,
                   int val);

int toml_write_bool(const char* path, const char* section, const char* key,
                    int val);

#ifdef __cplusplus
}
#endif

#endif

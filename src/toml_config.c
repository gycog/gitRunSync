/*
 * TOML Configuration Parser
 * 简化版 TOML 配置读写实现
 * 支持：[section]、key = "value"、key = 123、key = true/false
 */

#include "toml_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_TOML_LINES 256
#define MAX_TOML_LINE_LEN 4096

/* 去除字符串两端空白 */
static char* trim_inplace(char* s) {
    if (!s) return s;
    char* start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (*start == 0) {
        s[0] = 0;
        return s;
    }
    char* end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    end[1] = 0;
    if (start != s) memmove(s, start, strlen(start) + 1);
    return s;
}

/* 检查是否为空白行 */
static int is_blank_line(const char* s) {
    while (*s) {
        if (!isspace((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

/* 解析 TOML 值：去除引号并返回字符串 */
static void parse_toml_string(const char* raw, char* out, size_t cap) {
    if (!raw || !out || cap == 0) return;
    
    const char* p = raw;
    /* 跳过前置空白 */
    while (*p && isspace((unsigned char)*p)) p++;
    
    if (*p == '"') {
        /* 字符串值：提取引号内的内容 */
        p++; /* 跳过开引号 */
        size_t i = 0;
        while (*p && *p != '"' && i < cap - 1) {
            if (*p == '\\' && *(p + 1)) {
                /* 处理转义字符 */
                p++;
                switch (*p) {
                    case 'n': out[i++] = '\n'; break;
                    case 't': out[i++] = '\t'; break;
                    case 'r': out[i++] = '\r'; break;
                    case '\\': out[i++] = '\\'; break;
                    case '"': out[i++] = '"'; break;
                    default: out[i++] = *p; break;
                }
            } else {
                out[i++] = *p;
            }
            p++;
        }
        out[i] = '\0';
    } else {
        /* 非字符串值（数字、布尔等）：直接复制 */
        strncpy(out, trim_inplace((char*)p), cap - 1);
        out[cap - 1] = '\0';
        /* 移除尾部空白 */
        size_t len = strlen(out);
        while (len > 0 && isspace((unsigned char)out[len - 1])) {
            out[--len] = '\0';
        }
    }
}

/* 读取 TOML 字符串值 */
int toml_read_string(const char* path, const char* section, const char* key,
                     char* out, size_t out_cap, const char* default_val) {
    FILE* f = fopen(path, "r");
    if (!f) {
        if (out && out_cap > 0) {
            strncpy(out, default_val ? default_val : "", out_cap - 1);
            out[out_cap - 1] = '\0';
        }
        return 0;
    }

    char line[MAX_TOML_LINE_LEN];
    int in_section = 0;
    int found = 0;

    while (fgets(line, sizeof(line), f)) {
        char* trimmed = trim_inplace(line);
        
        if (trimmed[0] == '[') {
            /* Section 标题 */
            char* close = strchr(trimmed, ']');
            if (close) {
                *close = '\0';
                in_section = (strcmp(trimmed + 1, section) == 0);
            }
        } else if (in_section && trimmed[0] != '#' && trimmed[0] != '\0') {
            /* 键值对 */
            char* eq = strchr(trimmed, '=');
            if (eq) {
                *eq = '\0';
                char* key_part = trim_inplace(trimmed);
                if (strcmp(key_part, key) == 0) {
                    parse_toml_string(eq + 1, out, out_cap);
                    found = 1;
                    break;
                }
            }
        }
    }
    fclose(f);

    if (!found && out && out_cap > 0) {
        strncpy(out, default_val ? default_val : "", out_cap - 1);
        out[out_cap - 1] = '\0';
    }
    return found;
}

/* 读取 TOML 整数值 */
int toml_read_int(const char* path, const char* section, const char* key,
                  int default_val) {
    char buf[64];
    if (toml_read_string(path, section, key, buf, sizeof(buf), NULL)) {
        return (int)strtol(buf, NULL, 10);
    }
    return default_val;
}

/* 读取 TOML 布尔值 */
int toml_read_bool(const char* path, const char* section, const char* key,
                   int default_val) {
    char buf[32];
    if (toml_read_string(path, section, key, buf, sizeof(buf), NULL)) {
        /* 支持多种格式 */
        if (strcmp(buf, "true") == 0 || strcmp(buf, "True") == 0 ||
            strcmp(buf, "TRUE") == 0 || strcmp(buf, "1") == 0) {
            return 1;
        }
        if (strcmp(buf, "false") == 0 || strcmp(buf, "False") == 0 ||
            strcmp(buf, "FALSE") == 0 || strcmp(buf, "0") == 0) {
            return 0;
        }
    }
    return default_val;
}

/* 写入 TOML 字符串值 */
int toml_write_string(const char* path, const char* section, const char* key,
                      const char* val) {
    char lines[MAX_TOML_LINES][MAX_TOML_LINE_LEN];
    int count = 0;

    /* 读取现有文件 */
    FILE* f = fopen(path, "r");
    if (f) {
        while (count < MAX_TOML_LINES && fgets(lines[count], sizeof(lines[count]), f)) {
            count++;
        }
        fclose(f);
    }

    /* 查找 section 和 key */
    int section_idx = -1;
    int key_idx = -1;
    int next_section_after = -1;

    for (int i = 0; i < count; i++) {
        char tmp[MAX_TOML_LINE_LEN];
        strncpy(tmp, lines[i], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char* trimmed = trim_inplace(tmp);
        
        if (trimmed[0] == '[') {
            char* close = strchr(trimmed, ']');
            if (close) {
                *close = '\0';
                if (strcmp(trimmed + 1, section) == 0) {
                    section_idx = i;
                } else if (section_idx != -1 && next_section_after == -1) {
                    next_section_after = i;
                }
            }
        } else if (section_idx != -1 && key_idx == -1 &&
                   trimmed[0] != '#' && trimmed[0] != '\0') {
            char* eq = strchr(trimmed, '=');
            if (eq) {
                *eq = '\0';
                if (strcmp(trim_inplace(trimmed), key) == 0) {
                    key_idx = i;
                    break;
                }
            }
        }
    }

    /* 格式化 TOML 值（字符串用引号） */
    char formatted_val[MAX_TOML_LINE_LEN];
    if (val && val[0]) {
        snprintf(formatted_val, sizeof(formatted_val), "\"%s\"", val);
    } else {
        formatted_val[0] = '\"';
        formatted_val[1] = '\"';
        formatted_val[2] = '\0';
    }

    /* 更新或插入 */
    if (section_idx != -1 && key_idx != -1) {
        /* 替换已有行 */
        snprintf(lines[key_idx], sizeof(lines[key_idx]), "%s = %s\n", key, formatted_val);
    } else if (section_idx != -1) {
        /* 在 section 末尾插入 */
        int insert_pos = (next_section_after != -1) ? next_section_after : count;
        if (count >= MAX_TOML_LINES) return 0;
        for (int i = count; i > insert_pos; i--) {
            strncpy(lines[i], lines[i - 1], sizeof(lines[i]) - 1);
            lines[i][sizeof(lines[i]) - 1] = '\0';
        }
        snprintf(lines[insert_pos], sizeof(lines[insert_pos]), "%s = %s\n", key, formatted_val);
        count++;
    } else {
        /* 添加新 section 和 key */
        if (count > 0 && !is_blank_line(lines[count - 1])) {
            if (count >= MAX_TOML_LINES) return 0;
            lines[count][0] = '\n';
            lines[count][1] = '\0';
            count++;
        }
        if (count >= MAX_TOML_LINES) return 0;
        snprintf(lines[count], sizeof(lines[count]), "[%s]\n", section);
        count++;
        if (count >= MAX_TOML_LINES) return 0;
        snprintf(lines[count], sizeof(lines[count]), "%s = %s\n", key, formatted_val);
        count++;
    }

    /* 写回文件 */
    f = fopen(path, "w");
    if (!f) return 0;
    for (int i = 0; i < count; i++) {
        fputs(lines[i], f);
    }
    fclose(f);
    return 1;
}

/* 写入 TOML 整数值 */
int toml_write_int(const char* path, const char* section, const char* key,
                   int val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", val);
    
    /* 整数不需要引号，直接格式化写入 */
    char lines[MAX_TOML_LINES][MAX_TOML_LINE_LEN];
    int count = 0;

    FILE* f = fopen(path, "r");
    if (f) {
        while (count < MAX_TOML_LINES && fgets(lines[count], sizeof(lines[count]), f)) {
            count++;
        }
        fclose(f);
    }

    int section_idx = -1;
    int key_idx = -1;
    int next_section_after = -1;

    for (int i = 0; i < count; i++) {
        char tmp[MAX_TOML_LINE_LEN];
        strncpy(tmp, lines[i], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char* trimmed = trim_inplace(tmp);
        
        if (trimmed[0] == '[') {
            char* close = strchr(trimmed, ']');
            if (close) {
                *close = '\0';
                if (strcmp(trimmed + 1, section) == 0) {
                    section_idx = i;
                } else if (section_idx != -1 && next_section_after == -1) {
                    next_section_after = i;
                }
            }
        } else if (section_idx != -1 && key_idx == -1 &&
                   trimmed[0] != '#' && trimmed[0] != '\0') {
            char* eq = strchr(trimmed, '=');
            if (eq) {
                *eq = '\0';
                if (strcmp(trim_inplace(trimmed), key) == 0) {
                    key_idx = i;
                    break;
                }
            }
        }
    }

    if (section_idx != -1 && key_idx != -1) {
        snprintf(lines[key_idx], sizeof(lines[key_idx]), "%s = %d\n", key, val);
    } else if (section_idx != -1) {
        int insert_pos = (next_section_after != -1) ? next_section_after : count;
        if (count >= MAX_TOML_LINES) return 0;
        for (int i = count; i > insert_pos; i--) {
            strncpy(lines[i], lines[i - 1], sizeof(lines[i]) - 1);
            lines[i][sizeof(lines[i]) - 1] = '\0';
        }
        snprintf(lines[insert_pos], sizeof(lines[insert_pos]), "%s = %d\n", key, val);
        count++;
    } else {
        if (count > 0 && !is_blank_line(lines[count - 1])) {
            if (count >= MAX_TOML_LINES) return 0;
            lines[count][0] = '\n';
            lines[count][1] = '\0';
            count++;
        }
        if (count >= MAX_TOML_LINES) return 0;
        snprintf(lines[count], sizeof(lines[count]), "[%s]\n", section);
        count++;
        if (count >= MAX_TOML_LINES) return 0;
        snprintf(lines[count], sizeof(lines[count]), "%s = %d\n", key, val);
        count++;
    }

    f = fopen(path, "w");
    if (!f) return 0;
    for (int i = 0; i < count; i++) {
        fputs(lines[i], f);
    }
    fclose(f);
    return 1;
}

/* 写入 TOML 布尔值 */
int toml_write_bool(const char* path, const char* section, const char* key,
                    int val) {
    /* 布尔值使用 true/false，不需要引号 */
    char lines[MAX_TOML_LINES][MAX_TOML_LINE_LEN];
    int count = 0;

    FILE* f = fopen(path, "r");
    if (f) {
        while (count < MAX_TOML_LINES && fgets(lines[count], sizeof(lines[count]), f)) {
            count++;
        }
        fclose(f);
    }

    int section_idx = -1;
    int key_idx = -1;
    int next_section_after = -1;

    for (int i = 0; i < count; i++) {
        char tmp[MAX_TOML_LINE_LEN];
        strncpy(tmp, lines[i], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char* trimmed = trim_inplace(tmp);
        
        if (trimmed[0] == '[') {
            char* close = strchr(trimmed, ']');
            if (close) {
                *close = '\0';
                if (strcmp(trimmed + 1, section) == 0) {
                    section_idx = i;
                } else if (section_idx != -1 && next_section_after == -1) {
                    next_section_after = i;
                }
            }
        } else if (section_idx != -1 && key_idx == -1 &&
                   trimmed[0] != '#' && trimmed[0] != '\0') {
            char* eq = strchr(trimmed, '=');
            if (eq) {
                *eq = '\0';
                if (strcmp(trim_inplace(trimmed), key) == 0) {
                    key_idx = i;
                    break;
                }
            }
        }
    }

    const char* bool_str = val ? "true" : "false";
    
    if (section_idx != -1 && key_idx != -1) {
        snprintf(lines[key_idx], sizeof(lines[key_idx]), "%s = %s\n", key, bool_str);
    } else if (section_idx != -1) {
        int insert_pos = (next_section_after != -1) ? next_section_after : count;
        if (count >= MAX_TOML_LINES) return 0;
        for (int i = count; i > insert_pos; i--) {
            strncpy(lines[i], lines[i - 1], sizeof(lines[i]) - 1);
            lines[i][sizeof(lines[i]) - 1] = '\0';
        }
        snprintf(lines[insert_pos], sizeof(lines[insert_pos]), "%s = %s\n", key, bool_str);
        count++;
    } else {
        if (count > 0 && !is_blank_line(lines[count - 1])) {
            if (count >= MAX_TOML_LINES) return 0;
            lines[count][0] = '\n';
            lines[count][1] = '\0';
            count++;
        }
        if (count >= MAX_TOML_LINES) return 0;
        snprintf(lines[count], sizeof(lines[count]), "[%s]\n", section);
        count++;
        if (count >= MAX_TOML_LINES) return 0;
        snprintf(lines[count], sizeof(lines[count]), "%s = %s\n", key, bool_str);
        count++;
    }

    f = fopen(path, "w");
    if (!f) return 0;
    for (int i = 0; i < count; i++) {
        fputs(lines[i], f);
    }
    fclose(f);
    return 1;
}

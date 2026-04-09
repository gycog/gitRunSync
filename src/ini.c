#include "ini.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_INI_LINES 256
#define MAX_INI_LINE_LEN 4096

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

static int is_blank_line(const char* s) {
    while (*s) {
        if (!isspace((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

int ini_read(const char* path, const char* section, const char* key,
             char* out, size_t out_cap, const char* default_val) {
    FILE* f = fopen(path, "r");
    if (!f) {
        if (out && out_cap > 0) {
            strncpy(out, default_val ? default_val : "", out_cap - 1);
            out[out_cap - 1] = 0;
        }
        return 0;
    }

    char line[MAX_INI_LINE_LEN];
    int in_section = 0;
    size_t sec_len = strlen(section);
    int found = 0;

    while (fgets(line, sizeof(line), f)) {
        trim_inplace(line);
        if (line[0] == '[') {
            char* close = strchr(line, ']');
            if (close) {
                *close = 0;
                in_section = (strcmp(line + 1, section) == 0);
            }
        } else if (in_section && line[0] != ';' && line[0] != '#') {
            char* eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                if (strcmp(trim_inplace(line), key) == 0) {
                    strncpy(out, trim_inplace(eq + 1), out_cap - 1);
                    out[out_cap - 1] = 0;
                    found = 1;
                    break;
                }
            }
        }
    }
    fclose(f);

    if (!found && out && out_cap > 0) {
        strncpy(out, default_val ? default_val : "", out_cap - 1);
        out[out_cap - 1] = 0;
    }
    return found;
}

int ini_read_int(const char* path, const char* section, const char* key, int default_val) {
    char buf[64];
    if (ini_read(path, section, key, buf, sizeof(buf), NULL)) {
        return (int)strtol(buf, NULL, 10);
    }
    return default_val;
}

int ini_write(const char* path, const char* section, const char* key, const char* value) {
    char lines[MAX_INI_LINES][MAX_INI_LINE_LEN];
    int count = 0;

    FILE* f = fopen(path, "r");
    if (f) {
        while (count < MAX_INI_LINES && fgets(lines[count], sizeof(lines[count]), f)) {
            count++;
        }
        fclose(f);
    }

    int section_idx = -1;
    int key_idx = -1;
    int next_section_after = -1;

    for (int i = 0; i < count; i++) {
        char tmp[MAX_INI_LINE_LEN];
        strncpy(tmp, lines[i], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
        trim_inplace(tmp);
        if (tmp[0] == '[') {
            char* close = strchr(tmp, ']');
            if (close) {
                *close = 0;
                if (strcmp(tmp + 1, section) == 0) {
                    section_idx = i;
                } else if (section_idx != -1 && next_section_after == -1) {
                    next_section_after = i;
                }
            }
        } else if (section_idx != -1 && key_idx == -1 && tmp[0] != ';' && tmp[0] != '#') {
            char* eq = strchr(tmp, '=');
            if (eq) {
                *eq = 0;
                if (strcmp(trim_inplace(tmp), key) == 0) {
                    key_idx = i;
                    break;
                }
            }
        }
    }

    if (section_idx != -1 && key_idx != -1) {
        /* 替换已有行，保留前置空白（缩进）和换行风格 */
        char* raw = lines[key_idx];
        char* eq = strchr(raw, '=');
        if (eq) {
            eq[1] = 0; /* 截断到等号后面 */
            char new_line[MAX_INI_LINE_LEN];
            snprintf(new_line, sizeof(new_line), "%s%s\n", raw, value);
            strncpy(lines[key_idx], new_line, sizeof(lines[key_idx]) - 1);
            lines[key_idx][sizeof(lines[key_idx]) - 1] = 0;
        }
    } else if (section_idx != -1) {
        /* 在 section 末尾（或下一个 section 之前）插入 */
        int insert_pos = (next_section_after != -1) ? next_section_after : count;
        if (count >= MAX_INI_LINES) return 0;
        for (int i = count; i > insert_pos; i--) {
            strncpy(lines[i], lines[i - 1], sizeof(lines[i]) - 1);
            lines[i][sizeof(lines[i]) - 1] = 0;
        }
        snprintf(lines[insert_pos], sizeof(lines[insert_pos]), "%s=%s\n", key, value);
        count++;
    } else {
        /* 添加新 section 和 key */
        if (count > 0 && !is_blank_line(lines[count - 1])) {
            if (count >= MAX_INI_LINES) return 0;
            lines[count][0] = '\n';
            lines[count][1] = 0;
            count++;
        }
        if (count >= MAX_INI_LINES) return 0;
        snprintf(lines[count], sizeof(lines[count]), "[%s]\n", section);
        count++;
        if (count >= MAX_INI_LINES) return 0;
        snprintf(lines[count], sizeof(lines[count]), "%s=%s\n", key, value);
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

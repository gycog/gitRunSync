# GitRunSync

GitRunSync 是一个跨平台的 C 语言小工具，用来把“运行某个程序”和“同步当前 Git 仓库”串起来：

1. 在当前目录向上查找 Git 仓库根目录
2. 先执行一次 `git pull`
3. 启动指定应用并等待其退出
4. 自动执行 `git add -A`
5. 生成时间戳提交信息并执行 `git commit`
6. 再执行 `git pull`
7. 执行 `git push -u`

适合这种场景：打开某个编辑器、绘图工具、游戏工具链或商店应用，退出后自动把仓库改动提交并推送。

## 功能

- 跨平台支持 Windows 和 Linux
- 单实例运行，避免同一程序被重复触发
- 支持配置文件和命令行参数
- 支持优先启动应用商店应用，失败后回退到普通可执行文件
- 支持静默运行，并把输出写入日志文件
- 支持 Webhook 通知
- 支持创建桌面快捷方式

## 实际行为

- Git 仓库是从“当前工作目录”向上自动查找 `.git`
- 启动目标优先级是 `run_store_app` > `run_exe_path` > 默认 `ping`
- `git commit` 的提交信息格式为 `YYYYMMDD_HHMMSS`
- 即使被启动程序退出码非 0，程序仍会继续尝试 `commit/pull/push`
- `show_window = false` 时默认静默运行，日志写到程序目录下的 `GitRunSync.log`

## 项目结构

```text
.
├── CMakeLists.txt
├── GitRunSync.toml.example
├── src/
│   ├── GitRunSync.c
│   ├── platform_win32.c
│   ├── platform_linux.c
│   └── toml_config.c
├── res/
└── cmake/
```

## 构建

依赖：

- CMake 3.20+
- C11 编译器
- Windows: `shell32`、`shlwapi`、`winhttp`
- Linux: OpenSSL `crypto`

通用构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

输出文件：

- Windows: `build/bin/GitRunSync-Windows.exe`
- Linux: `build/bin/GitRunSync-Linux`

Windows MinGW：

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Windows MSVC：

```bash
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## 用法

```bash
GitRunSync [RunExePath] [RunExeArg]
GitRunSync --run <exe> --arg <arg>
GitRunSync -s
GitRunSync --create-shortcut
```

命令行参数：

| 参数 | 说明 |
|------|------|
| `RunExePath` | 位置参数，启动程序路径 |
| `RunExeArg` | 位置参数，启动参数 |
| `--run <exe>` | 指定启动程序路径 |
| `--arg <arg>` | 指定启动参数 |
| `-s`, `--create-shortcut` | 创建桌面快捷方式 |
| `-h`, `--help` | 显示帮助 |

示例：

```bash
# 使用配置文件中的默认行为
GitRunSync

# 指定要启动的程序
GitRunSync --run /usr/bin/code --arg "."

# Windows 示例
GitRunSync --run "C:\\Program Files\\SomeApp\\app.exe" --arg "--project demo"

# 创建快捷方式
GitRunSync -s
```

## 配置文件

程序会在可执行文件同目录下生成一个与程序同名的 TOML 文件：

- Windows 示例：`GitRunSync-Windows.toml`
- Linux 示例：`GitRunSync-Linux.toml`

配置示例：

```toml
[gitsync]
repo_dir = ""
run_store_app = "calc"
run_exe_path = "/usr/bin/ping"
run_exe_arg = "-c 10 127.0.0.1"
auto_clean_on_conflict = false

[webhook]
url = ""
secret = ""

[ui]
console_minimize = "tray"
show_window = false
```

配置项说明：

| 节 | 键 | 说明 |
|----|----|------|
| `gitsync` | `repo_dir` | 配置文件中会生成该字段，但当前主流程仍然按当前工作目录自动查找 Git 仓库 |
| `gitsync` | `run_store_app` | 优先启动的应用标识。Windows 下可用关键字或 AUMID，Linux 下会尝试匹配应用 |
| `gitsync` | `run_exe_path` | 普通可执行文件路径，作为回退目标 |
| `gitsync` | `run_exe_arg` | 启动参数 |
| `gitsync` | `auto_clean_on_conflict` | `git pull` 异常时是否自动执行清理流程 |
| `webhook` | `url` | 同步完成后发送通知的 URL |
| `webhook` | `secret` | Webhook HMAC 签名密钥 |
| `ui` | `console_minimize` | 显示窗口时的最小化策略：`tray`、`taskbar`、`none` |
| `ui` | `show_window` | 是否显示窗口，默认 `false` |

## 平台说明

### Windows

- 支持优先启动 Microsoft Store 应用
- 支持托盘最小化
- Webhook 通过 WinHTTP 发送
- 可执行文件默认名为 `GitRunSync-Windows.exe`

### Linux

- 支持通过应用名匹配 `.desktop` 应用并等待退出
- 托盘和最小化接口当前为占位实现
- 静默模式下会重定向标准输出和标准错误到 `/dev/null`
- 可执行文件默认名为 `GitRunSync-Linux`

## 快捷方式

`-s` 或 `--create-shortcut` 会根据当前所在 Git 仓库名称创建快捷方式：

- Windows: 创建桌面快捷方式
- Linux: 创建 `.desktop` 启动项

程序会优先查找图标：

1. `icons/<仓库名>.png|svg|xpm|ico`
2. `res/<仓库名>.png|svg|xpm|ico`

## 日志与静默模式

- 日志文件固定写到程序目录下的 `GitRunSync.log`
- `show_window = false` 时，控制台输出会被关闭，只保留日志
- `show_window = true` 时，会输出执行过程，并在成功后倒计时退出

## 退出码

| 退出码 | 说明 |
|--------|------|
| `0` | 成功 |
| `10` | 已有实例在运行 |
| `11` | 参数错误 |
| `12` | 配置文件错误 |
| `13` | 未找到 Git 仓库 |
| `14` | 未找到可运行目标 |
| `50` | Git 预同步失败 |
| `60` | 快捷方式创建失败 |

完整错误码定义见 `src/error_codes.h`。

## 当前限制

- Git 操作流程是固定的，当前不支持只执行部分步骤
- `repo_dir` 字段已存在，但当前版本主流程没有真正使用它切换仓库
- `auto_clean_on_conflict = true` 时会执行较激进的 Git 清理命令，使用前应明确风险
- README 中未覆盖所有平台实现细节，具体行为以源码为准

## 许可证

MIT

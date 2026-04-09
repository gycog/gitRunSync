# GitRunSync

## 项目简介

GitRunSync 是一个通用工具：**先启动指定的程序并等待其退出**，随后自动执行 Git 操作（add/commit/pull/push）。
此外支持 **优先启动微软应用商店（Appx）已安装应用**（通过关键字或 AUMID），并在可行时等待其退出。

## 功能特点

- **自动 Git 操作**：程序退出后自动执行 add/commit/pull/push
- **单实例运行**：确保同一时间只有一个实例运行
- **灵活的配置方式**：支持命令行参数和配置文件
- **智能仓库检测**：自动从当前目录向上查找 Git 仓库根目录
- **商店应用优先启动**：可用关键字或 AUMID 启动商店应用（失败则回退到普通 exe）
- **自动查找程序**：在未配置 RunExePath 时，会尝试使用系统自带程序作为兜底（如 `ping.exe`）

## 工作流程

1. **单实例检查**：确保只有一个程序实例运行
2. **参数解析**：解析命令行参数和配置文件
3. **Git Pull**：先拉取最新代码
4. **Git Status 检查**：检查仓库状态
5. **启动外部程序**：启动指定的外部程序并等待其退出
6. **Git 提交推送**：自动执行 add/commit/pull/push

## 安装方法

1. 克隆或下载项目源码
2. 使用 CMake 构建整个项目
3. 将编译生成的可执行文件放在合适的目录并按需重命名

## 使用方法

### 基本用法

```bash
GitRunSync.exe [RunExePath] [RunExeArg] [--run <exe>] [--arg <arg>]
```

### 示例

1. 基本使用（按配置文件启动；默认会优先尝试商店应用关键字 `calc`，失败则回退到 `ping.exe`）：
   ```bash
   GitRunSync.exe
   ```

2. 指定要运行的程序：
   ```bash
   GitRunSync.exe "D:\path\to\myprogram.exe" "--param1 value1"
   ```

## 命令行参数

| 参数 | 说明 |
|------|------|
| `RunExePath` | 要启动的程序路径（位置参数） |
| `RunExeArg` | 传递给启动程序的参数（位置参数） |
| `--run <exe>` | 指定要启动的程序路径（选项参数） |
| `--arg <arg>` | 指定传递给启动程序的参数（选项参数） |
| `--help` 或 `-h` | 显示帮助信息 |

## 配置文件

程序会在 **可执行文件所在目录** 生成配置文件，**文件名与 exe 同名**（例如 `GitRunSync.exe` 对应 `GitRunSync.toml`），格式如下：

```toml
[gitsync]
repo_dir = ""
run_store_app = "calc"
run_exe_path = "C:\\Windows\\System32\\ping.exe"
run_exe_arg = ""
```

### 配置项说明

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `gitsync.repo_dir` | Git 仓库目录（留空则自动查找） | 空 |
| `gitsync.run_store_app` | 商店应用关键字或 AUMID（`PackageFamilyName!AppId`）；**非空时优先启动**，失败回退 `gitsync.run_exe_path` | `calc` |
| `gitsync.run_exe_path` | 要启动的普通 exe 路径（`gitsync.run_store_app` 为空或失败时使用） | `C:\Windows\System32\ping.exe` |
| `gitsync.run_exe_arg` | 传递给启动程序的参数 | 空 |

## 自动查找程序逻辑

启动逻辑优先级如下：

1. `RunStoreApp` 非空：优先尝试启动商店应用（关键字或 AUMID）
2. 商店应用启动失败或 RunStoreApp 为空：使用 `RunExePath`
3. 若 `RunExePath` 为空：兜底查找 `ping.exe`（优先 repo 目录，其次 exe 目录，最后系统目录）

## 注意事项

1. 程序会自动最小化控制台窗口，外部程序退出后恢复
2. 支持 UTF-8 编码输出
3. 仅支持 Windows 操作系统
4. 确保 Git 已正确安装并配置环境变量
5. 首次运行会自动生成配置文件

## 退出码说明

| 退出码 | 说明 |
|--------|------|
| 0 | 成功执行 |
| 1 | 初始化失败 |
| 2 | 参数解析失败 |
| 3 | 配置文件处理失败 |
| 4 | 未找到 Git 仓库 |
| 5 | 未找到要启动的程序 |
| 6 | Git 同步前操作失败 |

## 开发说明

### 编译依赖

- Windows SDK
- C 编译器（如 MSVC、MinGW 等）
- CMake 3.20 或更高版本
- Windows 下需要链接 `shlwapi`、`shell32`、`winhttp`
- Linux 下需要 OpenSSL `crypto`

### 使用 CMake 构建（推荐）

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

默认输出目录：

- Windows: `build/bin/GitRunSync-Windows.exe`
- Linux: `build/bin/GitRunSync-Linux`

### Windows 下使用 MinGW

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Windows 下使用 MSVC

```powershell
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## 许可证

本项目采用 MIT 许可证。

## 作者

gycog

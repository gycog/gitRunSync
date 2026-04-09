cmake_minimum_required(VERSION 3.20)

# 打印编译链信息
message(STATUS "========================================")
message(STATUS "GitRunSync 编译链信息:")
message(STATUS "  CMake 版本: ${CMAKE_VERSION}")
message(STATUS "  操作系统: ${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_VERSION}")
message(STATUS "  处理器架构: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
message(STATUS "  目标平台: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  C 编译器: ${CMAKE_C_COMPILER}")
message(STATUS "  C 编译器 ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "  C 编译器版本: ${CMAKE_C_COMPILER_VERSION}")
if(CMAKE_CXX_COMPILER)
    message(STATUS "  C++ 编译器: ${CMAKE_CXX_COMPILER}")
endif()
message(STATUS "  生成器: ${CMAKE_GENERATOR}")
message(STATUS "========================================")

# 等待2秒
if(WIN32)
    execute_process(COMMAND ping -n 3 127.0.0.1 >nul)
else()
    execute_process(COMMAND sleep 2)
endif()

# 检测编译器环境
if(WIN32 AND NOT CMAKE_GENERATOR)
    # 检查环境变量或 PATH 中可用的编译器
    find_program(MINGW_GCC gcc.exe)
    find_program(VS_CL cl.exe)

    if(MINGW_GCC AND NOT VS_CL)
        message(STATUS "检测到 MinGW 环境，建议使用: cmake .. -G \"MinGW Makefiles\"")
    elseif(VS_CL)
        message(STATUS "检测到 MSVC 环境")
    else()
        message(WARNING "未在 PATH 中检测到编译器，请确保已安装 MinGW 或 Visual Studio，"
                        "或通过 -DCMAKE_C_COMPILER 指定编译器路径")
    endif()
endif()

project(GitRunSync C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(GIT_RUN_SYNC_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")

set(GITRUNSYNC_COMMON_SOURCES
    src/GitRunSync.c
    src/toml_config.c
)

if(WIN32)
    add_executable(GitRunSync
        ${GITRUNSYNC_COMMON_SOURCES}
        src/platform_win32.c
        res/resources.rc
    )
    target_compile_definitions(GitRunSync PRIVATE UNICODE _UNICODE)
    target_link_libraries(GitRunSync PRIVATE
        shell32
        shlwapi
        winhttp
    )
else()
    add_executable(GitRunSync
        ${GITRUNSYNC_COMMON_SOURCES}
        src/platform_linux.c
    )
    target_link_libraries(GitRunSync PRIVATE crypto)
endif()

# 根据平台设置输出文件名
if(WIN32)
    set(GITRUNSYNC_OUTPUT_NAME "GitRunSync-Windows")
else()
    set(GITRUNSYNC_OUTPUT_NAME "GitRunSync-Linux")
endif()

set_target_properties(GitRunSync PROPERTIES
    OUTPUT_NAME "${GITRUNSYNC_OUTPUT_NAME}"
    WIN32_EXECUTABLE TRUE  # Windows 子系统程序，无默认控制台窗口
    RUNTIME_OUTPUT_DIRECTORY "${GIT_RUN_SYNC_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${GIT_RUN_SYNC_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${GIT_RUN_SYNC_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${GIT_RUN_SYNC_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${GIT_RUN_SYNC_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${GIT_RUN_SYNC_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${GIT_RUN_SYNC_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${GIT_RUN_SYNC_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${GIT_RUN_SYNC_OUTPUT_DIR}"
)

if(MINGW)
    target_compile_options(GitRunSync PRIVATE
        -Os
        -ffunction-sections
        -fdata-sections
    )
    target_link_options(GitRunSync PRIVATE
        -mwindows              # Windows 子系统，无默认控制台
        -Wl,--entry,mainCRTStartup  # 但使用 main 作为入口点
        -Os
        -s
        -static-libgcc
        -Wl,--gc-sections
        -Wl,--strip-all
    )
elseif(MSVC)
    target_compile_options(GitRunSync PRIVATE
        /O1
        /Gy
        /DNDEBUG
    )
    target_link_options(GitRunSync PRIVATE
        /OPT:REF
        /OPT:ICF
        /INCREMENTAL:NO
        /SUBSYSTEM:WINDOWS    # Windows 子系统
        /ENTRY:mainCRTStartup  # 使用 main 作为入口点
        /LTCG
    )
else()
    target_compile_options(GitRunSync PRIVATE -Os)
    target_link_options(GitRunSync PRIVATE -s)
endif()

find_program(UPX upx)
if(UPX)
    message(STATUS "Found UPX: ${UPX}")
    add_custom_command(TARGET GitRunSync POST_BUILD
        COMMAND ${UPX} --ultra-brute $<TARGET_FILE:GitRunSync>
        COMMENT "Compressing executable with UPX..."
        VERBATIM
    )
else()
    message(WARNING "UPX not found, executable will not be compressed")
endif()

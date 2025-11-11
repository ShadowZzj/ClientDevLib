# ClientDevLib

一个功能丰富的跨平台 C++ 开发库，提供 Windows、macOS 以及通用平台的各类工具函数和封装。

## 📋 简介

ClientDevLib 是一个模块化的 C++ 工具库，集成了大量常用功能和第三方库，旨在加速应用程序开发。通过 CMake 特性开关（Feature Flags），可以按需启用所需功能，避免不必要的依赖。

## 🏗️ 目录结构

```
ClientDevLib/
├── General/                    # 跨平台通用代码
│   ├── util/                  # 通用工具类
│   └── ThirdParty/           # 第三方库（预编译）
├── Windows/                   # Windows 平台特定代码
│   └── util/                 # Windows 工具类
├── MacOS/                    # macOS 平台特定代码
│   └── util/                # macOS 工具类
├── Features.cmake            # 主特性配置文件
├── GeneralFeatures.cmake     # 通用特性配置
├── WinFeatures.cmake        # Windows 特性配置
└── MacFeatures.cmake        # macOS 特性配置
```

## 🎯 主要功能模块

### 通用功能 (General/util)

- **网络 (Network/)**
  - HTTP 客户端（基于 libcurl）
  - Socket 封装（TCP/UDP）
  - 网络适配器信息获取
  - DNS、ARP、ICMP、以太网协议助手
  - Pcap 网络包捕获接口
  - Ping 工具

- **文件操作 (File/)**
  - 文件读写、目录操作
  - 跨平台文件路径处理

- **进程管理 (Process/)**
  - 进程创建、枚举、控制
  - 线程管理
  - 线程池实现

- **加密 (Crypto/)**
  - AES 加密解密
  - MD5 哈希
  - Base64 编码解码

- **数据处理**
  - JSON 序列化/反序列化
  - 数据库配置管理（SQLite3）
  - 字符串工具 (StrUtil)

- **脚本支持 (Lua/)**
  - Lua 脚本集成
  - Lua 工具函数

- **日志 (SPDLog)**
  - 高性能日志库集成

- **系统信息 (System/)**
  - 系统信息获取
  - 用户信息

- **同步工具 (Sync/)**
  - 信号量
  - 进程同步
  - 停止通知

- **其他**
  - 证书管理
  - 服务管理
  - 版本管理
  - Git 仓库操作
  - 深度链接处理

### Windows 特定功能 (Windows/util)

- **反调试 (AntiDebug/)**
  - TLS 回调反调试
  - 常用反调试 API
  - 异常检测
  - 内存扫描
  - 线程执行检测

- **反虚拟机 (AntiVM/)**
  - VirtualBox 检测
  - VMWare 检测
  - 沙箱检测

- **DirectX 图形 (Graphics/)**
  - DirectX 11 渲染管线
  - D3D9 Hook
  - ImGui 集成（Win32 + DX9/DX11）
  - 3D 图形基础设施（相机、光照、网格、着色器等）
  - GDI+ 管理
  - 字体助手

- **Windows API 封装**
  - COM/WMI 助手
  - 注册表操作 (WinReg)
  - 窗口管理
  - 句柄管理

- **设备管理 (Device/)**
  - 硬件信息获取
  - 键盘/鼠标输入
  - Interception 驱动集成
  - 打印机工具

- **文件操作**
  - PE 文件解析
  - 资源文件操作

- **事件跟踪 (EventTrace/)**
  - ETW (Event Tracing for Windows)
  - 事件提供者管理

- **服务管理**
  - Windows 服务创建/控制

- **任务计划 (TaskScheduler/)**
  - Windows 任务计划程序集成

- **进程注入**
  - GHInjector 集成

- **系统工具**
  - 域信息
  - 控制台操作
  - Windows 更新检查

- **调试工具 (Debug/)**
  - 堆栈回溯 (StackWalker)

- **IPC (进程间通信)**
  - 管道通信

### macOS 特定功能 (MacOS/util)

- **系统工具**
  - 系统信息获取
  - 用户信息
  - Profile 配置文件管理

- **网络**
  - DNS 助手
  - 网络适配器信息
  - Pcap 网络包捕获

- **进程管理**
  - 进程迭代器
  - 进程控制

- **应用程序**
  - 深度链接
  - 软件信息

- **文件操作**
  - macOS 特定文件操作

- **证书管理**
  - Keychain 访问

- **服务管理**
  - Launch Daemon/Agent 支持

## 📦 集成的第三方库

ClientDevLib 预集成了以下第三方库（已预编译）：

| 库名          | 功能                                           | 支持平台            |
| ------------- | ---------------------------------------------- | ------------------- |
| Boost 1.75.0  | C++ 通用库（文件系统、日期时间、正则表达式等） | Windows, macOS      |
| OpenSSL       | SSL/TLS 加密                                   | Windows, macOS      |
| libcurl       | HTTP 客户端                                    | Windows, macOS      |
| SQLite3       | 嵌入式数据库                                   | Windows, macOS      |
| Lua 5.4.3     | Lua 脚本引擎                                   | Windows, macOS      |
| gRPC          | RPC 框架                                       | Windows, macOS      |
| spdlog        | 高性能日志库                                   | 通用（仅头文件）    |
| nlohmann/json | JSON 解析                                      | 通用（仅头文件）    |
| imgui         | 即时模式 GUI                                   | Windows（DX9/DX11） |
| pybind11      | Python 绑定                                    | Windows             |
| Detours       | API Hook 库                                    | Windows             |
| LIEF          | 可执行文件解析                                 | Windows             |
| Zipper/libzip | ZIP 压缩                                       | Windows, macOS      |
| zlib          | 压缩库                                         | Windows, macOS      |
| libconfig     | 配置文件解析                                   | Windows, macOS      |
| Interception  | 键鼠驱动                                       | Windows             |
| GHInjector    | DLL 注入器                                     | Windows             |
| assimp        | 3D 模型加载                                    | Windows             |
| libpcap       | 网络包捕获                                     | macOS               |
| libgit2       | Git 操作                                       | Windows, macOS      |
| Catch2        | 单元测试框架                                   | 通用                |
| CLI11         | 命令行参数解析                                 | 通用（仅头文件）    |
| cpp-httplib   | 轻量级 HTTP 库                                 | 通用（仅头文件）    |
| websocketpp   | WebSocket 库                                   | 通用（仅头文件）    |
| pugixml       | XML 解析                                       | 通用                |
| plist         | plist 文件解析                                 | 通用                |
| xorstr        | 字符串混淆                                     | 通用（仅头文件）    |

## 🚀 使用方法

### 1. 在你的项目中引入

在你的 `CMakeLists.txt` 中添加：

```cmake
# 设置 ClientDevLib 路径
set(ClientDevLib_DIR ${CMAKE_CURRENT_SOURCE_DIR}/path/to/ClientDevLib)

# 包含主配置文件
include(${ClientDevLib_DIR}/Features.cmake)

# 启用所需特性并生成工具函数
GenerateUtil(
    FEATURE_CURL          # HTTP 客户端
    FEATURE_CRYPTO        # 加密功能
    FEATURE_LUA           # Lua 脚本
    FEATURE_SQLITE3PP     # SQLite3 数据库
    # ... 其他特性
)
```

### 2. Windows 项目配置

```cmake
# 添加源文件到你的目标
add_executable(MyApp main.cpp)

# 链接库文件
target_link_libraries(MyApp
    ${WINDOWS_UTIL_LIB_FILES}
    ${GENERAL_UTIL_FILES}
)

# 添加包含目录
target_include_directories(MyApp PRIVATE 
    ${WINDOWS_UTIL_INCLUDE_DIRS}
    ${ClientDevLib_DIR}
    ${ClientDevLib_DIR}/General/ThirdParty
)

# 复制 DLL 文件到输出目录（必须在最后调用）
ClientDevLibFinalize()
```

### 3. macOS 项目配置

```cmake
# 添加源文件到你的目标
add_executable(MyApp main.mm)

# 链接库文件和系统框架
target_link_libraries(MyApp
    PRIVATE ${MACOS_UTIL_LIB_FILES}
    PRIVATE "-framework Foundation"
    PRIVATE "-framework CoreWLAN"
    PRIVATE "-framework Security"
    # ... 其他需要的框架
)

# 添加包含目录
target_include_directories(MyApp PRIVATE 
    ${MACOS_UTIL_INCLUDE_DIRS}
    ${ClientDevLib_DIR}
    ${ClientDevLib_DIR}/General/ThirdParty
)

# 复制动态库到输出目录
ClientDevLibFinalize()
```

## 🎛️ 功能特性开关

通过在 `GenerateUtil()` 函数中指定特性标志来启用功能：

### 通用特性

| 特性标志                    | 说明                                   | 依赖库                |
| --------------------------- | -------------------------------------- | --------------------- |
| `FEATURE_CURL`              | HTTP 客户端功能                        | libcurl, OpenSSL      |
| `FEATURE_CRYPTO`            | 加密功能（AES）                        | OpenSSL               |
| `FEATURE_OPENSSL`           | OpenSSL 支持                           | OpenSSL               |
| `FEATURE_LUA`               | Lua 脚本引擎                           | Lua 5.4.3             |
| `FEATURE_SQLITE3PP`         | SQLite3 数据库                         | SQLite3, sqlite3pp    |
| `FEATURE_GRPC`              | gRPC 支持                              | gRPC, OpenSSL         |
| `FEATURE_BOOST`             | Boost 库                               | Boost 1.75.0          |
| `FEATURE_PLIST`             | plist 文件解析                         | pugixml               |
| `FEATURE_PYTHON`            | Python 嵌入                            | Python 3.11           |
| `FEATURE_PYBIND11`          | Python 绑定（自动启用 FEATURE_PYTHON） | pybind11, Python 3.11 |
| `FEATURE_THIRDPARTY_ZIPPER` | ZIP 压缩                               | Zipper, zlib          |
| `FEATURE_LIBGIT2`           | Git 操作                               | libgit2               |

### Windows 特性

| 特性标志                        | 说明                                                    | 依赖库         |
| ------------------------------- | ------------------------------------------------------- | -------------- |
| `FEATURE_ANTIDEBUG`             | 反调试功能                                              | Detours        |
| `FEATURE_ANTIDEBUG_TLSCALLBACK` | TLS 回调反调试                                          | -              |
| `FEATURE_D3D9`                  | DirectX 9 Hook（自动启用 FEATURE_IMGUI_WIN32_DIRECTX9） | DirectX 9 SDK  |
| `FEATURE_DIRECTX`               | DirectX 图形功能                                        | DirectX 11 SDK |
| `FEATURE_IMGUI_WIN32_DIRECTX9`  | ImGui DX9 后端                                          | imgui          |
| `FEATURE_IMGUI_WIN32_DIRECTX11` | ImGui DX11 后端                                         | imgui          |
| `FEATURE_DETOURS`               | Detours Hook 库                                         | Detours        |
| `FEATURE_ETW`                   | 事件跟踪 (ETW)                                          | -              |
| `FEATURE_INTERCEPTION`          | Interception 驱动                                       | Interception   |
| `FEATURE_SHADERS`               | 着色器编译                                              | DirectX SDK    |
| `FEATURE_LIEF`                  | PE 文件解析                                             | LIEF           |
| `FEATURE_GHINJECTOR`            | GH 注入器                                               | GHInjector     |
| `FEATURE_THIRDPARTY_ASSIMP`     | 3D 模型加载                                             | assimp         |
| `FEATURE_OLLVM`                 | 代码混淆（需要 OLLVM 编译器）                           | OLLVM          |

### macOS 特性

| 特性标志          | 说明       | 依赖库    |
| ----------------- | ---------- | --------- |
| `FEATURE_PCAP`    | 网络包捕获 | libpcap   |
| `FEATURE_RESOLVE` | DNS 解析   | libresolv |

## 💡 代码示例

### 示例 1: HTTP 请求

```cpp
#include "General/util/Network/Http/Http.h"

int main() {
    Http http;
    std::string response;
    
    if (http.Get("https://api.example.com/data", response)) {
        std::cout << "Response: " << response << std::endl;
    }
    
    return 0;
}
```

**CMakeLists.txt 配置：**
```cmake
GenerateUtil(FEATURE_CURL FEATURE_OPENSSL)
```

### 示例 2: JSON 处理

```cpp
#include "General/ThirdParty/json.hpp"
#include <iostream>

using json = nlohmann::json;

int main() {
    json data = {
        {"name", "ClientDevLib"},
        {"version", "1.0"},
        {"features", {"curl", "lua", "crypto"}}
    };
    
    std::cout << data.dump(4) << std::endl;
    
    return 0;
}
```

### 示例 3: Lua 脚本执行

```cpp
#include "General/util/Lua/LuaUtil.h"

int main() {
    LuaUtil lua;
    lua.LoadString("print('Hello from Lua!')");
    lua.Execute();
    
    return 0;
}
```

**CMakeLists.txt 配置：**
```cmake
GenerateUtil(FEATURE_LUA)
```

### 示例 4: 进程管理 (Windows)

```cpp
#include "General/util/Process/Process.h"

int main() {
    // 枚举所有进程
    auto processes = Process::EnumProcesses();
    
    for (const auto& proc : processes) {
        std::cout << "PID: " << proc.pid 
                  << " Name: " << proc.name << std::endl;
    }
    
    // 创建新进程
    Process::Create("notepad.exe");
    
    return 0;
}
```

### 示例 5: SQLite 数据库

```cpp
#include "General/ThirdParty/sqlite3pp/sqlite3pp.h"

int main() {
    sqlite3pp::database db("test.db");
    
    db.execute("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT)");
    db.execute("INSERT INTO users (name) VALUES ('Alice')");
    
    sqlite3pp::query qry(db, "SELECT * FROM users");
    for (auto row : qry) {
        int id;
        const char* name;
        std::tie(id, name) = row.get_columns<int, const char*>(0, 1);
        std::cout << "ID: " << id << ", Name: " << name << std::endl;
    }
    
    return 0;
}
```

**CMakeLists.txt 配置：**
```cmake
GenerateUtil(FEATURE_SQLITE3PP)
```

### 示例 6: 反调试 (Windows)

```cpp
#include "Windows/util/AntiDebug/CommonApi.h"

int main() {
    if (AntiDebug::IsDebuggerPresent()) {
        std::cout << "Debugger detected!" << std::endl;
        return 1;
    }
    
    // 你的代码逻辑
    
    return 0;
}
```

**CMakeLists.txt 配置：**
```cmake
GenerateUtil(FEATURE_ANTIDEBUG)

# 注意：反调试功能需要禁用 SAFESEH
target_link_options(YourTarget PRIVATE "/SAFESEH:NO")
```

### 示例 7: ImGui 界面 (Windows)

```cpp
#include "General/ThirdParty/imgui/imgui.h"
#include "Windows/util/Graphics/ImguiManager.h"

int main() {
    ImguiManager imgui;
    imgui.Initialize(hwnd); // 传入窗口句柄
    
    while (running) {
        imgui.BeginFrame();
        
        ImGui::Begin("My Window");
        ImGui::Text("Hello from ImGui!");
        if (ImGui::Button("Click Me")) {
            // 按钮点击事件
        }
        ImGui::End();
        
        imgui.EndFrame();
    }
    
    return 0;
}
```

**CMakeLists.txt 配置：**
```cmake
GenerateUtil(FEATURE_IMGUI_WIN32_DIRECTX11)
```

## 📝 注意事项

### Windows 平台

1. **反调试功能**：使用 `FEATURE_ANTIDEBUG` 时，必须在链接选项中添加 `/SAFESEH:NO`
2. **管理员权限**：某些功能（如驱动相关）需要管理员权限运行
3. **运行时库**：默认使用静态链接的 MT/MTd 运行时
4. **架构支持**：支持 x86 和 x64 架构

### macOS 平台

1. **Objective-C++**：macOS 特定代码使用 `.mm` 扩展名
2. **系统框架**：需要链接相应的系统框架（Foundation, CoreWLAN 等）
3. **Universal Binary**：支持 x86_64 和 arm64 架构
4. **最低系统版本**：macOS 10.15+
5. **Hardened Runtime**：启用了 Hardened Runtime
6. **符号可见性**：默认隐藏符号

### 通用

1. **C++ 标准**：要求 C++17 或更高版本
2. **头文件搜索路径**：
   - `${ClientDevLib_DIR}`
   - `${ClientDevLib_DIR}/General/ThirdParty`
   - `${ClientDevLib_DIR}/General/ThirdParty/boost_1_75_0`
3. **完成配置**：必须在 CMakeLists.txt 末尾调用 `ClientDevLibFinalize()` 来复制必要的 DLL/dylib 文件

## 🛠️ 构建说明

### Windows

```bash
cd ClientDevLib/Windows
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DBUILD_TYPE=Release
cmake --build . --config RelWithDebInfo
```

### macOS

```bash
cd ClientDevLib/MacOS
mkdir build && cd build
cmake .. -DBUILD_TYPE=Release
cmake --build . --config RelWithDebInfo
```

## 📂 添加到你的项目

### 方式 1: 作为子模块

```bash
cd your-project
git submodule add <ClientDevLib-repo-url> ClientDevLib
```

### 方式 2: 直接复制

将 ClientDevLib 目录直接复制到你的项目中。

## 🔧 高级配置

### 自定义特性组合

你可以根据项目需求创建自定义特性组合：

```cmake
# 网络应用
GenerateUtil(FEATURE_CURL FEATURE_OPENSSL FEATURE_GRPC)

# 图形应用
GenerateUtil(FEATURE_DIRECTX FEATURE_IMGUI_WIN32_DIRECTX11 FEATURE_THIRDPARTY_ASSIMP)

# 系统工具
GenerateUtil(FEATURE_ANTIDEBUG FEATURE_ETW FEATURE_DETOURS)

# 脚本引擎
GenerateUtil(FEATURE_LUA FEATURE_PYTHON FEATURE_PYBIND11)
```

### OLLVM 代码混淆

如果启用了 `FEATURE_OLLVM`，会自动执行混淆配置脚本：

```cmake
GenerateUtil(FEATURE_OLLVM)
# 会自动调用 Scripts/ollvm_config.py
```

## 📖 文档

更多详细文档：

- 各个模块的头文件中包含详细的函数注释
- 查看 `ClientDevLib/Windows/test/` 和 `ClientDevLib/MacOS/test.mm` 获取更多示例

## 🤝 贡献

如需添加新功能或修复 bug，请参考现有代码风格和目录结构。

## 📄 许可证

请查看各个第三方库的许可证。

## ⚠️ 免责声明

本库包含的某些功能（如反调试、反虚拟机、代码注入等）仅供合法的软件保护和安全研究使用。使用者应当遵守所在地区的法律法规，不得用于非法目的。

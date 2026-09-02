---
kind: build_system
name: CMake + Qt6 构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - CMakePresets.json
    - resources/icons.qrc
---

### 1. 使用的系统与工具
- 构建系统：CMake 3.25+，通过 CMakeLists.txt 定义工程、目标与依赖。
- 语言标准：C++23（MSVC 使用 /std:c++latest 以启用 C++26 特性）。
- UI 框架：Qt6（Widgets、Svg），测试使用 Qt Test。
- 生成器：默认使用 Visual Studio 17 2022 (x64)，通过 CMakePresets.json 管理配置。
- 资源管理：Qt Resource System（.qrc 文件）。

### 2. 关键文件与包
- CMakeLists.txt：根构建脚本，定义项目元信息、源文件清单、链接库、测试目标。
- CMakePresets.json：预设配置，包含 configure/build preset，指定 VS 生成器、Qt 路径环境变量 QT_DIR。
- resources/icons.qrc：Qt 资源文件，打包 SVG 图标。
- tests/：单元测试源码，通过 CTest 注册两个测试目标。
- src/：所有源代码按功能模块组织（app、canvas、parametric、ui、tools、geometry）。

### 3. 架构与约定
- 单可执行体应用：所有源文件直接列在 add_executable(${PROJECT_NAME} ...) 中，未拆分为子模块或库。
- 头文件暴露：通过 target_include_directories(... PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src) 统一暴露 src/ 目录作为包含路径。
- 测试集成：每个测试是独立可执行目标，通过 add_test(NAME ... COMMAND ...) 注册到 CTest。
- Qt 自动处理：启用 AUTOMOC/AUTORCC/AUTOUIC，无需手动生成 moc/uic/rcc 代码。
- 跨平台编译开关：MSVC 下额外设置 /permissive- 以启用严格模式。

### 4. 约定与约束
- C++ 版本强制为 23（CMAKE_CXX_STANDARD_REQUIRED ON），MSVC 使用最新标准支持。
- Qt 依赖通过 find_package(Qt6 REQUIRED COMPONENTS Widgets Svg) 显式声明，测试需额外 Test 组件。
- 构建输出目录固定为 ${sourceDir}/build/out，Debug/Release 两种配置由 build preset 提供。
- 环境变量 QT_DIR 必须指向 Qt6 MSVC 安装路径（如 C:/Qt/6.11.1/msvc2022_64），否则配置失败。
- 新增源文件需手动添加到 CMakeLists.txt 的 add_executable 列表中，无自动扫描机制。
- 测试用例需同时包含被测源码和 Qt Test 依赖，目前仅覆盖 ExpressionEvaluator 与 Resolver 模块。
---
kind: dependency_management
name: CMake + Qt6 依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - CMakePresets.json
---

本项目使用 CMake 作为构建系统与依赖管理工具，通过 `find_package(Qt6 REQUIRED COMPONENTS ...)` 动态查找并链接 Qt6 框架（Widgets、Svg、Test），未采用任何包管理器（如 vcpkg、Conan）或 vendoring 策略。第三方依赖完全依赖系统环境中已安装的 Qt6 路径，由 `CMakePresets.json` 中的 `CMAKE_PREFIX_PATH=$env{QT_DIR}` 指定 Qt 安装目录（例如 `C:/Qt/6.11.1/msvc2022_64`）。项目版本由 CMake `project(GarmentCAD VERSION 0.1.0 ...)` 声明，但无独立的依赖版本锁定文件（如 `vcpkg.json`、`conanfile.txt`、`go.mod` 等）。所有源文件与测试目标均在根 `CMakeLists.txt` 中显式列出，未使用子模块或外部依赖仓库。Qt 依赖的版本由用户通过环境变量 `QT_DIR` 提供，CMake 最低版本要求为 3.25，C++ 标准为 23（MSVC 下启用 `/std:c++latest`）。
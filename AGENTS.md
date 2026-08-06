# AGENTS.md

## 项目简介

参数化服装 CAD 系统，基于 C++23 / Qt6 构建。通过参数化建模引擎驱动服装纸样的自动生成与联动更新。

## 模块边界

| 目录 | 职责 |
|------|------|
| `src/parametric` | 核心引擎：参数点、线段、Block、组、公式变量、条件引擎、Resolver |
| `src/ui` | 面板 UI：变量面板、组面板、公式卡片、条件对话框等 |
| `src/tools` | 交互工具：选择、智能画笔、捕捉引擎、工具管理器 |
| `src/canvas` | 画布渲染：场景、视图、BlockItem、原点十字线 |
| `src/geometry` | 基础几何：Vec2、单位定义 |
| `src/app` | 应用入口与演示数据 |

## 构建命令

```bash
cmake --preset default
cmake --build --preset default
```

## 验证命令

```bash
cd build/out
ctest -C Debug
```

注：default preset 的 binaryDir 是 `build/out`（非 build/default）；Visual Studio 多配置生成器必须加 `-C Debug`，否则报 "No tests were found"。

## 环境要求

| 依赖 | 要求 |
|------|------|
| Qt | 6.x（推荐 6.5+），组件：Widgets、Svg、Test |
| 编译器 | MSVC 2022（Visual Studio 17，x64） |
| CMake | ≥ 3.25 |
| C++ 标准 | C++23（MSVC 使用 `/std:c++latest`） |

### 方式一：Qt 官方安装器（推荐）

1. 从 [Qt 官网](https://www.qt.io/download-qt-installer) 下载 Qt Online Installer。
2. 安装时勾选 **MSVC 2022 64-bit** 组件。
3. 设置环境变量 `QT_DIR` 指向安装路径下的 `msvc2022_64` 目录：

```powershell
# 示例（根据实际版本号调整）
[System.Environment]::SetEnvironmentVariable("QT_DIR", "C:/Qt/6.11.1/msvc2022_64", "User")
```

4. 重新打开终端，验证：

```powershell
echo $env:QT_DIR
cmake --preset default
```

### 方式二：vcpkg manifest

若使用 vcpkg 管理依赖，可在项目根目录创建 `vcpkg.json`：

```json
{
  "dependencies": [
    { "name": "qtbase", "features": ["widgets"] },
    "qtsvg"
  ]
}
```

配置时通过 toolchain 文件传入：

```powershell
cmake --preset default -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
```

此方式无需设置 `QT_DIR`，CMake 会从 vcpkg 安装路径自动发现 Qt6。

## 关键约束

- **单位体系**：内部计算使用厘米（cm），界面显示使用毫米（mm）。
- **Block 刚体模型**：Block 是刚体变换单元，内部点相对位置固定，整体支持平移/旋转。

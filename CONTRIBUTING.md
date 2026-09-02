# CONTRIBUTING —— 提交规范与格式化

## Git 提交规范

- 提交信息用中文，格式：`<type>: <摘要>`，type ∈ {feat, fix, perf, refactor, docs, ci, test}。
- 摘要一句话说清「改了什么、为什么」，如 `fix: 根治两条既有基线红——Resolver Step 5 桥跟随者重解恢复 + 组件收敛循环补 followResettle 后沉降`。
- 本地身份：林林 <2274789227@qq.com>；远程 origin = https://github.com/zll7hj111-cmyk/garment-cad.git，主分支 main。
- 提交前自查：改动了 AGENTS.md / TROUBLESHOOTING.md / DECISIONS.md / CONVENTIONS.md 中声明的事实（常量/命令/路径/文件/用例数）时，必须同步更新对应条目（带 file:line 引用）。

## 代码格式化命令

- 本项目**无 clang-format 配置**（仓库根无 .clang-format），格式化纪律靠人工遵守：MSVC `/std:c++latest` + `/permissive-` + `/FS`；新建源文件必须 UTF-8 with BOM（Write/Edit 工具写出的无 BOM，含中文文件写完必须补 BOM）。
- 静态检查（提交前必跑，已进 ctest）：
  - `python tools/check_layering.py` —— 分层单向依赖（新增跨层 include 前先跑）
  - `python tools/check_hardcoded_colors.py` —— 硬编码颜色守卫（新增颜色前先想 token）
  - `python tools/check_test_fixtures.py` —— 测试档铁律（回归基线不许放仓库之外）
- 新增 .cpp 加入其模块库源列表（七库之一）；测试 target 源列表只放 `tests/*.cpp`，切勿加项目头文件（AUTOMOC 重复定义 LNK2005）。

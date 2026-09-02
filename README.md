# WildWind Pattern（野风帖）

参数化服装 CAD 系统 —— C++23 / Qt6。设计师在画布上绘制受约束的线段，公式变量、测量、条件与图层驱动模型自动求解与联动更新。

## 一键启动

```bat
tools\build.bat          :: 一键：vcvars64 + configure + build（Ninja Debug，binaryDir=build/out）
tools\build.bat release  :: Release（binaryDir=build/out-rel）
```

**环境变量清单**（构建前需就绪）：

| 变量 | 说明 |
|------|------|
| `QT_DIR` | Qt 6.x 安装路径（指向 msvc2022_64，方式一）；或 vcpkg manifest + toolchain（方式二，二选一） |
| `GCAD_DOC` | 仅手动测试用：真实文档路径（`test_realdoc_perf` / `test_realdoc_full`） |
| `GCAD_PROFILE` | 置 `1` 启用 PerfProbe 性能探针 |

构建脚本内部先 call vcvars64.bat；普通 PowerShell 直接跑 cmake 找不到 cl.exe 时会失败，请走脚本或 Developer PowerShell。

## 如何运行测试

```bat
cd build\out
ctest
```

- 全量 ctest 是收尾/跨模块大改的最终验证；日常按影响面选测（纯几何 → test_curve + test_expression；parametric 引擎 → test_resolver + test_commands + test_serializer + test_migration；工具/UI → test_select_wkey / test_rotate_copy / test_context_strip / test_dialog_tabs 等）。
- 单测：`ctest -R <名>`；守卫脚本（check_layering / check_hardcoded_colors / check_test_fixtures）已进 ctest，全量即覆盖。
- 不进 ctest 需手动跑：test_realdoc_perf、test_realdoc_full（env `GCAD_DOC`）、test_nav_smoke。
- 回归基线红 = 0；环境漂移红（test_intersection_update 全部 + test_extend::savedDocFormulaStartExtendRenders）依赖活档 E:/3.gcad，非代码回归。详见 CONVENTIONS.md 验证命令区。

## 许可证

[MIT](./LICENSE)

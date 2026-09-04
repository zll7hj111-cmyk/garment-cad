cmake_minimum_required(VERSION 3.25)

project(WildWindPattern
    VERSION 0.1.0
    DESCRIPTION "Parametric Garment CAD"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: use /std:c++latest to enable C++26 features
if(MSVC)
    add_compile_options(/std:c++latest /permissive-)
    # /FS: safe parallel compilation (several CL.EXE share one PDB per
    # target; without it, /m:N builds intermittently fail with C1041).
    add_compile_options(/FS)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg OpenGLWidgets WidgetsPrivate)

# --- miniz (ZIP read/write, single-file public-domain library) ---
include(FetchContent)
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        3.0.2
    GIT_SHALLOW    TRUE
)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_MakeAvailable(miniz)
unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

# --- ElaWidgetTools (Fluent-design Widgets UI library, MIT) ---
# The repo root CMakeLists (ElaFramework) hard-codes QT_SDK_DIR and
# FATAL_ERRORs on Qt > 6.7, so we only add_subdirectory the library itself
# (pinned to a commit; the repo publishes no tags).
FetchContent_Declare(
    elawidgettools
    GIT_REPOSITORY https://github.com/Liniyous/ElaWidgetTools.git
    GIT_TAG        aa1856b8f8589ba0a82e50c0ac01e289eb3ff2c4
    GIT_SHALLOW    TRUE
    # Qt 6.9+ removed QChar(enum) support; see third_party/elawidgettools_qt69_patch.cmake.
    PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/third_party/elawidgettools_qt69_patch.cmake
)
FetchContent_GetProperties(elawidgettools)
if(NOT elawidgettools_POPULATED)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.30")
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(elawidgettools)
    set(ELAWIDGETTOOLS_BUILD_STATIC_LIB ON CACHE BOOL "Build static library." FORCE)
    add_subdirectory(${elawidgettools_SOURCE_DIR}/ElaWidgetTools
                     ${elawidgettools_BINARY_DIR}/ElaWidgetTools EXCLUDE_FROM_ALL)
    # Their sources are UTF-8 without BOM; without /utf-8 MSVC mis-parses
    # Chinese string literals (C2001/C2061). /FS: parallel cl.exe sharing
    # ElaWidgetTools.pdb must not race (C1041) under Ninja's high parallelism.
    if(MSVC)
        target_compile_options(ElaWidgetTools PRIVATE /utf-8 /FS)
    endif()
endif()

# --- spdlog (fast C++ logging, MIT) ---
# Compiled static lib; bundles fmt (MIT). Link spdlog::spdlog.
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.17.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# --- Tracy Profiler (frame profiler client, BSD-3) ---
# Client-only: the profiler GUI is a standalone tool (prebuilt Windows x64
# binaries on the releases page). TRACY_ON_DEMAND keeps the app at ~zero
# overhead unless a profiler is connected; disable entirely with
# -DTRACY_ENABLE=OFF for release builds.
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        v0.14.0
    GIT_SHALLOW    TRUE
)
set(TRACY_ON_DEMAND ON CACHE BOOL "Connect to profiler only when a listener is present" FORCE)
FetchContent_MakeAvailable(tracy)

set(GCAD_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ═════════════════════════════════════════════════════════════════════════════
# Static libraries — one per module directory. Layering (bottom → top):
#   gcad_geometry → gcad_parametric → gcad_canvas / gcad_document
#                                 → gcad_ui → gcad_tools → gcad_app
# Every .cpp lives in exactly ONE library; the app and the test executables
# link the libraries instead of re-listing sources. When adding a new .cpp,
# add it ONLY to its module library below (no per-test edits needed).
#
# NOTE: do NOT list project headers in the test/executable source lists —
# AUTOMOC would moc them again in the executable and collide with the
# library's own moc output (LNK2005). Headers are found via the include dir.
# ═════════════════════════════════════════════════════════════════════════════

# ─── geometry: pure math, no engine/UI dependency ────────────────────────────
add_library(gcad_geometry STATIC
    src/geometry/Vec2.h
    src/geometry/Units.h
    src/geometry/Angle.h
    src/geometry/CurveMath.h
    src/geometry/CurveMath.cpp
    src/geometry/CurveSplitter.h
    src/geometry/CurveSplitter.cpp
    src/geometry/TriangleUnfold.h
    src/geometry/TriangleUnfold.cpp
)
target_include_directories(gcad_geometry PUBLIC ${GCAD_SRC})
target_link_libraries(gcad_geometry PUBLIC Qt6::Core Qt6::Gui)

# ─── parametric: the engine core (never depends on UI/tools/canvas) ──────────
add_library(gcad_parametric STATIC
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
    src/parametric/Block.h
    src/parametric/Block.cpp
    # ── P1-2 前半段（B 类）：门面按域分组的只读视图（header-only，委托门面实现）──
    src/parametric/BlockView.h
    src/parametric/AttachmentsView.h
    src/parametric/ComponentsView.h
    src/parametric/MeasurementsView.h
    src/parametric/LayersView.h
    src/parametric/VariablesView.h
    src/parametric/DomainViews.h
    src/parametric/Component.h
    src/parametric/Attachment.h
    src/parametric/AttachmentGraph.h
    src/parametric/FollowerAngle.h
    src/parametric/Resolver.h
    src/parametric/Resolver.cpp
    src/parametric/ParamDocumentDetail.h
    src/parametric/ParamDocument.h
    src/parametric/ParamDocument.cpp
    src/parametric/ParamDocumentStores.cpp
    src/parametric/ParamDocumentParameters.cpp
    src/parametric/ParamDocumentBlocks.cpp
    src/parametric/ParamDocumentAttachments.cpp
    src/parametric/ParamDocumentShadow.cpp
    src/parametric/ParamDocumentIndexes.cpp
    src/parametric/ParamDocumentResolver.cpp
    src/parametric/ParamDocumentLifecycle.cpp
    src/parametric/LayerRegistry.h
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.h
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.h
    src/parametric/MeasurementStore.cpp
    src/parametric/Duplicate.h
    src/parametric/Duplicate.cpp
    src/parametric/Serial.h
    src/parametric/Serial.cpp
    src/parametric/Variable.h
    src/parametric/FormulaVariable.h
    src/parametric/FormulaGroup.h
    src/parametric/Layer.h
    src/parametric/LinkedVariable.h
    src/parametric/LinkedVariable.cpp
    src/parametric/MeasureVariable.h
    src/parametric/AngleMeasureVariable.h
    src/parametric/ExpressionEvaluator.h
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/Condition.h
    src/parametric/ConditionEngine.h
    src/parametric/ConditionEngine.cpp
    src/parametric/PerfProbe.h
    src/parametric/IntersectDebug.h
)
target_include_directories(gcad_parametric PUBLIC ${GCAD_SRC})
target_link_libraries(gcad_parametric PUBLIC Qt6::Gui gcad_geometry)

# ─── canvas: scene/view rendering ────────────────────────────────────────────
add_library(gcad_canvas STATIC
    src/canvas/CanvasView.h
    src/canvas/CanvasView.cpp
    src/canvas/CanvasScene.h
    src/canvas/CanvasScene.cpp
    src/canvas/CanvasStyle.h
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.h
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.h
    src/canvas/OriginCrosshair.cpp
    src/canvas/HudItem.h
    src/canvas/HudItem.cpp
    src/canvas/BlockItem.h
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.h
    src/canvas/CurveItem.cpp
)
target_include_directories(gcad_canvas PUBLIC ${GCAD_SRC})
target_link_libraries(gcad_canvas PUBLIC
    Qt6::Widgets Qt6::OpenGLWidgets ElaWidgetTools
    gcad_parametric gcad_geometry)

# ─── document: serialization + undo commands ─────────────────────────────────
add_library(gcad_document STATIC
    src/document/DocumentSerializer.h
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.h
    src/document/DocumentFile.cpp
    src/document/FormatMigration.h
    src/document/FormatMigration.cpp
    src/document/commands/BlockCommands.h
    src/document/commands/BlockTransformCommands.h
    src/document/commands/BlockTransformCommands.cpp
    src/document/commands/BlockLifecycleCommands.h
    src/document/commands/BlockLifecycleCommands.cpp
    src/document/commands/SegmentPropertyCommands.h
    src/document/commands/SegmentPropertyCommands.cpp
    src/document/commands/ReverseSegmentCommand.h
    src/document/commands/ReverseSegmentCommand.cpp
    src/document/commands/CurveCommands.h
    src/document/commands/CurveCommands.cpp
    src/document/commands/EndpointCommands.h
    src/document/commands/EndpointCommands.cpp
    src/document/commands/CommandIds.h
    src/document/commands/ComponentCommands.h
    src/document/commands/ComponentCommands.cpp
    src/document/commands/LayerCommands.h
    src/document/commands/LayerCommands.cpp
    src/document/commands/AttachmentCommands.h
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/AttachmentLifecycleCommands.h
    src/document/commands/AttachmentLifecycleCommands.cpp
    src/document/commands/AttachmentAngleCommands.h
    src/document/commands/AttachmentAngleCommands.cpp
    src/document/commands/AttachmentSlideCommands.h
    src/document/commands/AttachmentSlideCommands.cpp
    src/document/commands/VariableCommands.h
    src/document/commands/VariableCommands.cpp
    src/document/commands/DocumentCommands.h
    src/document/commands/DocumentCommands.cpp
    src/document/commands/BreakState.h
    src/document/commands/BreakAnalysis.h
    src/document/commands/BreakAnalysis.cpp
    src/document/commands/BreakEvaluate.cpp
    src/document/commands/BreakExecution.h
    src/document/commands/BreakExecution.cpp
    src/document/commands/BreakFinish.cpp
    src/document/commands/BreakCommands.h
    src/document/commands/BreakCommands.cpp
)
target_include_directories(gcad_document PUBLIC ${GCAD_SRC})
target_link_libraries(gcad_document PUBLIC Qt6::Gui miniz gcad_parametric gcad_geometry)

# ─── ui: panels, cards, dialogs, theme (P1-6: DeleteImpactConfirm is a UI
#      dialog, so it lives HERE in src/ui/ — document/ must not depend on ui/.
#      Run tools/check_layering.py to keep it that way) ───────────────────────
add_library(gcad_ui STATIC
    src/ui/VariableCard.h
    src/ui/VariableCard.cpp
    src/ui/CopyChip.h
    src/ui/CopyChip.cpp
    src/ui/CompoundChip.h
    src/ui/CompoundChip.cpp
    src/ui/FormulaCard.h
    src/ui/FormulaCard.cpp
    src/ui/FormulaGroupHeader.h
    src/ui/FormulaGroupHeader.cpp
    src/ui/LinkedCard.h
    src/ui/LinkedCard.cpp
    src/ui/MeasureCard.h
    src/ui/MeasureCard.cpp
    src/ui/AngleMeasureCard.h
    src/ui/AngleMeasureCard.cpp
    src/ui/VirtualCardList.h
    src/ui/VirtualCardList.cpp
    src/ui/CardBase.h
    src/ui/CardBase.cpp
    src/ui/ConditionDialog.h
    src/ui/ConditionDialog.cpp
    src/ui/VariablePanel.h
    src/ui/VariablePanel.cpp
    src/ui/PanelSubTabBar.h
    src/ui/PanelSubTabBar.cpp
    src/ui/MeasureTab.h
    src/ui/MeasureTab.cpp
    src/ui/NoteButton.h
    src/ui/NoteButton.cpp
    src/ui/ComponentTab.h
    src/ui/ComponentTab.cpp
    src/ui/FormulaTabModel.h
    src/ui/FormulaTabModel.cpp
    src/ui/SerialDelegate.h
    src/ui/SerialDelegate.cpp
    src/ui/LayerCard.h
    src/ui/LayerCard.cpp
    src/ui/LayerPanel.h
    src/ui/LayerPanel.cpp
    src/ui/IconHelper.h
    src/ui/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/ui/TooltipFormatter.h
    src/ui/ElaMsgBox.h
    src/ui/ElaDialogButtons.h
    src/ui/FormScaffold.h
    src/ui/DeleteImpactConfirm.h
    src/ui/DeleteImpactConfirm.cpp
    # ── P2-4: 线段属性对话框及其卡片 （原在 src/tools/，QWidget 归位 ui/）──
    # (AngleHud 已随 CONTEXT_STRIP 二期退场, 连接手势角度输入并入 ContextStrip)
    src/ui/LayerFeedback.h
    src/ui/AuxPointForm.h
    src/ui/AuxPointForm.cpp
    src/ui/QuickAuxDialog.h
    src/ui/QuickAuxDialog.cpp
    src/ui/MeasureResultDialog.h
    src/ui/MeasureResultDialog.cpp
    src/ui/IntersectionForm.h
    src/ui/IntersectionForm.cpp
    src/ui/LinePropertyDialog.h
    src/ui/LinePropertyDialog.cpp
    src/ui/LinePropertySession.h
    src/ui/LinePropertySession.cpp
    src/ui/LineEndpointSection.h
    src/ui/LineEndpointSection.cpp
    src/ui/LineAppearanceSection.h
    src/ui/LineAppearanceSection.cpp
    src/ui/LineGeometrySection.h
    src/ui/LineGeometrySection.cpp
    src/ui/SegmentAnchorTab.h
    src/ui/SegmentAnchorTab.cpp
    src/ui/SegmentAuxTab.h
    src/ui/SegmentAuxTab.cpp
    src/ui/SegmentConnectionCard.h
    src/ui/SegmentConnectionCard.cpp
    src/ui/SegmentConnectionCardBuild.cpp
    src/ui/SegmentConnectionCardRefresh.cpp
    src/ui/SegmentConnectionCardConn.cpp
    src/ui/SegmentAngleCard.h
    src/ui/SegmentAngleCard.cpp
    src/ui/SegmentRefCard.h
    src/ui/SegmentRefCard.cpp
    src/ui/PointRefEdit.h
    src/ui/PointRefEdit.cpp
)
target_include_directories(gcad_ui PUBLIC ${GCAD_SRC})
target_link_libraries(gcad_ui PUBLIC
    Qt6::Widgets Qt6::Svg ElaWidgetTools
    gcad_canvas gcad_document gcad_parametric gcad_geometry)

# ─── tools: interactive tools / gestures / snapping ──────────────────────────
add_library(gcad_tools STATIC
    src/tools/Tool.h
    src/tools/Tool.cpp
    src/tools/ToolRegistry.h
    src/tools/ToolRegistry.cpp
    src/tools/ToolManager.h
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.h
    src/tools/ToolSelect.cpp
    src/tools/ToolSelectHitTest.cpp
    src/tools/ToolSelectActions.cpp
    src/tools/SelectDragController.h
    src/tools/SelectDragController.cpp
    src/tools/CurveAnchorDragSession.h
    src/tools/CurveAnchorDragSession.cpp
    src/tools/OverlapDisambiguationController.h
    src/tools/OverlapDisambiguationController.cpp
    src/tools/SelectHoverFeedback.h
    src/tools/SelectHoverFeedback.cpp
    src/tools/MarqueeGesture.h
    src/tools/MarqueeGesture.cpp
    src/tools/SelectState.h
    src/tools/ConnectGesture.h
    src/tools/ConnectGesture.cpp
    src/tools/ConnectGestureAttach.cpp
    src/tools/ConnectGestureAngleSession.cpp
    src/tools/ConnectOverlapResolver.h
    src/tools/ConnectOverlapResolver.cpp
    src/tools/CopyDragController.h
    src/tools/CopyDragController.cpp
    src/tools/ToolSmartPen.h
    src/tools/ToolSmartPen.cpp
    src/tools/SmartPenEndConfirm.cpp
    src/tools/SmartPenDart.cpp
    src/tools/SmartPenAuxPoint.cpp
    src/tools/SmartPenStrokeInput.h
    src/tools/SmartPenStrokeInput.cpp
    src/tools/LineFactory.h
    src/tools/LineFactory.cpp
    src/tools/LeaderCandidatePicker.h
    src/tools/LeaderCandidatePicker.cpp
    src/tools/ToolCurveEdit.h
    src/tools/ToolCurveEdit.cpp
    src/tools/ToolRotate.h
    src/tools/ToolRotate.cpp
    src/tools/RotateCopyGesture.h
    src/tools/RotateCopyGesture.cpp
    src/tools/RotateGizmo.h
    src/tools/RotateGizmo.cpp
    src/tools/RotateAimSnap.h
    src/tools/RotateAimSnap.cpp
    src/tools/RotateInputTracker.h
    src/tools/RotateInputTracker.cpp
    src/tools/RotateSession.h
    src/tools/RotateSession.cpp
    src/tools/MultiRotateSession.h
    src/tools/MultiRotateSession.cpp
    src/tools/ToolBreak.h
    src/tools/ToolBreak.cpp
    src/tools/ToolIntersection.h
    src/tools/ToolIntersection.cpp
    src/tools/ToolMeasure.h
    src/tools/ToolMeasure.cpp
    src/tools/ToolAngleMeasure.h
    src/tools/ToolAngleMeasure.cpp
    src/tools/SnapEngine.h
    src/tools/SnapEngine.cpp
    # 2026-12 面板重设计: 角度/引用线段 自连接卡拆出为独立卡.
)
target_include_directories(gcad_tools PUBLIC ${GCAD_SRC})
target_link_libraries(gcad_tools PUBLIC
    Qt6::Widgets ElaWidgetTools
    gcad_ui gcad_canvas gcad_document gcad_parametric gcad_geometry)

# ─── app: application shell ───────────────────────────────────────────────────
add_library(gcad_app STATIC
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/ContextStrip.h
    src/app/ContextStrip.cpp
)
target_include_directories(gcad_app PUBLIC ${GCAD_SRC})
target_link_libraries(gcad_app PUBLIC
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets ElaWidgetTools miniz
    gcad_tools gcad_ui gcad_canvas gcad_document gcad_parametric gcad_geometry)

# ═════════════════════════════════════════════════════════════════════════════
# Application
# ═════════════════════════════════════════════════════════════════════════════
add_executable(${PROJECT_NAME}
    resources/icons.qrc
    src/main.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE gcad_app spdlog::spdlog Tracy::TracyClient)

# ═════════════════════════════════════════════════════════════════════════════
# Unit Tests (CTest + Qt Test)
# ═════════════════════════════════════════════════════════════════════════════
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)

# ── 守卫脚本进 ctest（2026-09）：分层/测试夹具/硬编码颜色三守卫秒级完成，
#    全量 ctest 即覆盖，无需再手动单独跑。脚本以仓库根为工作目录（脚本内部
#    用 __file__ 定位仓库，不依赖 CWD，此处仅统一输出路径）。 ──────────────
add_test(NAME check_layering
         COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
                 python ${CMAKE_SOURCE_DIR}/tools/check_layering.py)
add_test(NAME check_test_fixtures
         COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
                 python ${CMAKE_SOURCE_DIR}/tools/check_test_fixtures.py)
add_test(NAME check_hardcoded_colors
         COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
                 python ${CMAKE_SOURCE_DIR}/tools/check_hardcoded_colors.py)
add_test(NAME check_file_size
         COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
                 python ${CMAKE_SOURCE_DIR}/tools/check_file_size.py)
add_test(NAME check_header_classification
         COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
                 python ${CMAKE_SOURCE_DIR}/tools/check_header_classification.py)
add_test(NAME check_bool_flags
         COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
                 python ${CMAKE_SOURCE_DIR}/tools/check_bool_flags.py)
add_test(NAME check_test_split
         COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
                 python ${CMAKE_SOURCE_DIR}/tools/check_test_split.py)
set_tests_properties(check_layering check_test_fixtures check_hardcoded_colors
                     check_file_size check_header_classification check_bool_flags check_test_split
    PROPERTIES RUN_SERIAL TRUE)

# ── Test: Tool hint completeness (TOOL_SYSTEM_AUDIT H3) ──
add_executable(test_tool_hints tests/test_tool_hints.cpp)
target_include_directories(test_tool_hints PRIVATE ${GCAD_SRC})
target_link_libraries(test_tool_hints PRIVATE gcad_tools Qt6::Core Qt6::Test)
add_test(NAME test_tool_hints COMMAND test_tool_hints)

# ── Test: W 键模式切换的持久化显示标识 (2026-08-29) ──
# 锁四条契约: 状态栏持久层存在 / 静态与运行期文案同源 / 三态带位置 /
# W 语义随工具状态变化。链接与 test_measure 同 (需要真实 CanvasScene)。
add_executable(test_mode_indicator tests/test_mode_indicator.cpp)
target_include_directories(test_mode_indicator PRIVATE ${GCAD_SRC})
target_link_libraries(test_mode_indicator PRIVATE
    gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg Qt6::Test ElaWidgetTools)
add_test(NAME test_mode_indicator COMMAND test_mode_indicator)

# ── Test: ExpressionEvaluator ──
add_executable(test_expression tests/test_expression.cpp)
target_include_directories(test_expression PRIVATE ${GCAD_SRC})
target_link_libraries(test_expression PRIVATE gcad_parametric Qt6::Core Qt6::Test)
add_test(NAME test_expression COMMAND test_expression)

# ── Test: Resolver ──
add_executable(test_resolver tests/test_resolver.cpp)
target_include_directories(test_resolver PRIVATE ${GCAD_SRC})
target_link_libraries(test_resolver PRIVATE gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_resolver COMMAND test_resolver)

# ── Test: DocumentSerializer round-trip ──
add_executable(test_serializer tests/test_serializer.cpp)
target_include_directories(test_serializer PRIVATE ${GCAD_SRC})
target_link_libraries(test_serializer PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_serializer COMMAND test_serializer)

# ── Test: real-doc P612 borrow-point intersection colinearity (1.gcad regression) ──
add_executable(test_p612_colinearity tests/test_p612_colinearity.cpp)
target_include_directories(test_p612_colinearity PRIVATE ${GCAD_SRC})
target_link_libraries(test_p612_colinearity PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_p612_colinearity COMMAND test_p612_colinearity)

# ── Test: cross-layer intersection follows parameter changes (3.gcad regression) ──
add_executable(test_intersection_update tests/test_intersection_update.cpp)
target_include_directories(test_intersection_update PRIVATE ${GCAD_SRC})
target_link_libraries(test_intersection_update PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_intersection_update COMMAND test_intersection_update)

# ── Test: duplicateBlocks (Ctrl+drag quick copy) ──
add_executable(test_duplicate tests/test_duplicate.cpp)
target_include_directories(test_duplicate PRIVATE ${GCAD_SRC})
target_link_libraries(test_duplicate PRIVATE gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_duplicate COMMAND test_duplicate)

# ── Test: breakSegment (point break tool) ──
add_executable(test_break tests/test_break.cpp)
target_include_directories(test_break PRIVATE ${GCAD_SRC})
target_link_libraries(test_break PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_break COMMAND test_break)

# ── Test: intersection point + aux ref point ──
add_executable(test_intersection tests/test_intersection.cpp)
target_include_directories(test_intersection PRIVATE ${GCAD_SRC})
target_link_libraries(test_intersection PRIVATE gcad_parametric Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_intersection COMMAND test_intersection)

# ── Test: component (组件) create / rigid sync / dissolve / delete / round-trip
#    + full connect-gesture harness (junction internal-follower connect) ──
add_executable(test_component
    tests/test_component.cpp
    resources/icons.qrc
)
target_include_directories(test_component PRIVATE ${GCAD_SRC})
target_link_libraries(test_component PRIVATE
    gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_component COMMAND test_component)

# ── Domain-specific command test suites (Rule VIII test decomposition) ──
add_executable(test_block_commands tests/test_block_commands.cpp)
target_include_directories(test_block_commands PRIVATE ${GCAD_SRC})
target_link_libraries(test_block_commands PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_block_commands COMMAND test_block_commands)

add_executable(test_attachment_commands tests/test_attachment_commands.cpp)
target_include_directories(test_attachment_commands PRIVATE ${GCAD_SRC})
target_link_libraries(test_attachment_commands PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_attachment_commands COMMAND test_attachment_commands)

add_executable(test_variable_layer_commands tests/test_variable_layer_commands.cpp)
target_include_directories(test_variable_layer_commands PRIVATE ${GCAD_SRC})
target_link_libraries(test_variable_layer_commands PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_variable_layer_commands COMMAND test_variable_layer_commands)

add_executable(test_reverse_segment_commands tests/test_reverse_segment_commands.cpp)
target_include_directories(test_reverse_segment_commands PRIVATE ${GCAD_SRC})
target_link_libraries(test_reverse_segment_commands PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_reverse_segment_commands COMMAND test_reverse_segment_commands)

# ── Test: CurveMath (Bezier / Catmull-Rom / arc length / projection / intersection) ──
add_executable(test_curve tests/test_curve.cpp)
target_include_directories(test_curve PRIVATE ${GCAD_SRC})
target_link_libraries(test_curve PRIVATE gcad_geometry Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_curve COMMAND test_curve)

# ── Test: triangle sloper surface unfolding (edge-preserving paper-space layout) ──
add_executable(test_triangle_unfold tests/test_triangle_unfold.cpp)
target_include_directories(test_triangle_unfold PRIVATE ${GCAD_SRC})
target_link_libraries(test_triangle_unfold PRIVATE gcad_geometry Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_triangle_unfold COMMAND test_triangle_unfold)

# ── Benchmark: layered dirty-marking resolve pipeline (aux frozen vs full) ──
add_executable(test_perf tests/test_perf.cpp)
target_include_directories(test_perf PRIVATE ${GCAD_SRC})
target_link_libraries(test_perf PRIVATE gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_perf COMMAND test_perf)

# ── Test: on-disk format migration chain (P2-1) ──────────────────────────────
add_executable(test_migration tests/test_migration.cpp)
target_include_directories(test_migration PRIVATE ${GCAD_SRC})
target_link_libraries(test_migration PRIVATE gcad_document gcad_parametric
    Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_migration COMMAND test_migration)

# ── Benchmark: solve-cost scaling (P2-6 quantification: how big before the
#      GUI-thread solve blows the 60fps frame budget?). Reports, never
#      asserts on timing — see tests/test_resolve_scale.cpp header.
add_executable(test_resolve_scale tests/test_resolve_scale.cpp)
target_include_directories(test_resolve_scale PRIVATE ${GCAD_SRC})
target_link_libraries(test_resolve_scale PRIVATE gcad_parametric Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_resolve_scale COMMAND test_resolve_scale)

# ── Test: auxiliary-layer rendering (aux point visibility across layer modes) ──
add_executable(test_aux_layer tests/test_aux_layer.cpp)
target_include_directories(test_aux_layer PRIVATE ${GCAD_SRC})
target_link_libraries(test_aux_layer PRIVATE
    gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg Qt6::Test ElaWidgetTools)
add_test(NAME test_aux_layer COMMAND test_aux_layer)

# ── Test: 端点延长线 (EXTEND_LINE_DESIGN.md): 辅助点本体不动 / 交点尾巴 /
#    跟随线联动 / 打断归属 / 命令 undo / 序列化 / 尾巴范围判定 ──
add_executable(test_extend tests/test_extend.cpp)
target_include_directories(test_extend PRIVATE ${GCAD_SRC})
target_link_libraries(test_extend PRIVATE
    gcad_canvas gcad_document gcad_parametric gcad_tools
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME test_extend COMMAND test_extend)

# ── Test: smart-pen line-body X marker / quick aux point interaction ──
add_executable(test_smartpen_aux tests/test_smartpen_aux.cpp)
target_include_directories(test_smartpen_aux PRIVATE ${GCAD_SRC})
target_link_libraries(test_smartpen_aux PRIVATE
    gcad_app gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg Qt6::Test ElaWidgetTools)
add_test(NAME test_smartpen_aux COMMAND test_smartpen_aux)

# ── Test: measure tool W-mode cycling (距离/水平/垂直), per-kind values ──
add_executable(test_measure tests/test_measure.cpp)
target_include_directories(test_measure PRIVATE ${GCAD_SRC})
target_link_libraries(test_measure PRIVATE
    gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg Qt6::Test ElaWidgetTools)
add_test(NAME test_measure COMMAND test_measure)

# ── Test: ContextStrip (上下文属性条, CONTEXT_STRIP_DESIGN.md 一期) ──
add_executable(test_context_strip tests/test_context_strip.cpp)
target_include_directories(test_context_strip PRIVATE ${GCAD_SRC})
target_link_libraries(test_context_strip PRIVATE
    gcad_app gcad_document gcad_parametric
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test ElaWidgetTools)
add_test(NAME test_context_strip COMMAND test_context_strip)

# ── Test: formula-tab grouping (VirtualCardList stale-rebind regression) ──
add_executable(test_formula_groups
    tests/test_formula_groups.cpp
    resources/icons.qrc
)
target_include_directories(test_formula_groups PRIVATE ${GCAD_SRC})
target_link_libraries(test_formula_groups PRIVATE
    gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::Test ElaWidgetTools)
add_test(NAME test_formula_groups COMMAND test_formula_groups)

# ── Test: intersection tool aim-point interaction flow (full event chain) ──
add_executable(test_tool_intersection
    tests/test_tool_intersection.cpp
    resources/icons.qrc
)
target_include_directories(test_tool_intersection PRIVATE ${GCAD_SRC})
target_link_libraries(test_tool_intersection PRIVATE
    gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_tool_intersection COMMAND test_tool_intersection)

# ── Test: select tool W-key mode toggle through the full event chain ──
add_executable(test_select_wkey
    tests/test_select_wkey.cpp
    resources/icons.qrc
)
target_include_directories(test_select_wkey PRIVATE ${GCAD_SRC})
target_link_libraries(test_select_wkey PRIVATE
    gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_select_wkey COMMAND test_select_wkey)

# ── Test: curve-edit tool Alt+drag tangent unlock (P3-1, CURVE_P3_DESIGN.md) ──
add_executable(test_curve_edit
    tests/test_curve_edit.cpp
    resources/icons.qrc
)
target_include_directories(test_curve_edit PRIVATE ${GCAD_SRC})
target_link_libraries(test_curve_edit PRIVATE
    gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_curve_edit COMMAND test_curve_edit)


# ── Test: rotate-copy (Ctrl+drag in ToolRotate) ──
add_executable(test_rotate_copy
    tests/test_rotate_copy.cpp
    resources/icons.qrc
)
target_include_directories(test_rotate_copy PRIVATE ${GCAD_SRC})
target_link_libraries(test_rotate_copy PRIVATE
    gcad_app gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_rotate_copy COMMAND test_rotate_copy)

# ── Test: line-property dialog tab switching ──
add_executable(test_dialog_tabs
    tests/test_dialog_tabs.cpp
    resources/icons.qrc
)
target_include_directories(test_dialog_tabs PRIVATE ${GCAD_SRC})
target_link_libraries(test_dialog_tabs PRIVATE
    gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_dialog_tabs COMMAND test_dialog_tabs)

# ── Benchmark: curve-heavy canvas interaction (compute + software repaint cost) ──
add_executable(test_canvas_perf tests/test_canvas_perf.cpp)
target_include_directories(test_canvas_perf PRIVATE ${GCAD_SRC})
target_link_libraries(test_canvas_perf PRIVATE
    gcad_canvas gcad_parametric gcad_tools
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)
add_test(NAME test_canvas_perf COMMAND test_canvas_perf)

# ── Test: hold-to-show overlays (N = all names, L = all lengths) ──
add_executable(test_hold_show tests/test_hold_show.cpp)
target_include_directories(test_hold_show PRIVATE ${GCAD_SRC})
target_link_libraries(test_hold_show PRIVATE
    gcad_canvas gcad_parametric gcad_tools
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME test_hold_show COMMAND test_hold_show)

# ── Benchmark: real user document drag-frame breakdown (env GCAD_DOC=<path>) ──
add_executable(test_realdoc_perf tests/test_realdoc_perf.cpp)
target_include_directories(test_realdoc_perf PRIVATE ${GCAD_SRC})
target_link_libraries(test_realdoc_perf PRIVATE
    gcad_document gcad_canvas gcad_parametric gcad_tools
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test miniz)

# ── Benchmark: FULL MainWindow drag on a real document (env GCAD_DOC=<path>).
#    Links gcad_app (the whole application minus main.cpp) for full fidelity. ──
add_executable(test_realdoc_full
    tests/test_realdoc_full.cpp
    resources/icons.qrc
)
target_include_directories(test_realdoc_full PRIVATE ${GCAD_SRC})
target_link_libraries(test_realdoc_full PRIVATE
    gcad_app
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)

# ── Smoke test: ElaWindow navigation stack switching (reproduces the
#    "点击图层=闪退" crash). Not registered with ctest — run manually. ──
add_executable(test_nav_smoke
    tests/test_nav_smoke.cpp
    resources/icons.qrc
)
target_include_directories(test_nav_smoke PRIVATE ${GCAD_SRC})
target_link_libraries(test_nav_smoke PRIVATE
    gcad_app
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)

# ── Test: spdlog + Tracy integration (file-sink log write + ZoneScoped probe) ──
add_executable(test_log_probe tests/test_log_probe.cpp)
target_include_directories(test_log_probe PRIVATE ${GCAD_SRC})
target_link_libraries(test_log_probe PRIVATE Qt6::Core Qt6::Test spdlog::spdlog Tracy::TracyClient)
add_test(NAME test_log_probe COMMAND test_log_probe)

# ── PDB 膨胀治理（2026-08）：全链测试关增量链接 ────────────────────────────
# 这些测试把整个项目 + ElaWidgetTools 链接进一个 exe，Debug /Zi 符号下 PDB
# 达 ~1GB；/INCREMENTAL 让 PDB 跨重链只增不减（实测 12GB 总量，大头在此）。
# 关掉增量后 PDB 每次链接重写一次、大小封顶，调试能力完整保留；代价仅是
# 重链为全量链接（慢几秒）。主程序 WildWindPattern 保留增量（日常调试迭代）。
foreach(_t IN ITEMS
        test_realdoc_full test_nav_smoke test_aux_layer test_smartpen_aux
        test_measure test_context_strip test_tool_intersection
        test_select_wkey test_curve_edit test_rotate_copy test_dialog_tabs
        test_component)
    target_link_options(${_t} PRIVATE /INCREMENTAL:NO)
endforeach()

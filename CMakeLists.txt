cmake_minimum_required(VERSION 3.25)

project(WildWindPattern
    VERSION 0.1.0
    DESCRIPTION "Parametric Garment CAD"
    LANGUAGES CXX
)

# ── MSVC /showIncludes prefix repair (header-dependency correctness) ──────────
# CMake detects the /showIncludes prefix with execute_process(ENCODING AUTO),
# which decodes compiler output with the active console code page and falls back
# to ANSI when no console is attached.  cl.exe writes that note as UTF-8, so on
# a zh-CN Windows (ANSI = CP936) the prefix stored in rules.ninja becomes
# mojibake that can never match the compiler output.  Ninja then records NO
# header dependencies: editing a header leaves every dependent object stale, and
# an incremental build can link objects compiled against different class
# layouts (observed as 0xC0000409 / 0xC0000374 heap corruption once
# CanvasStyle.h grew).  Re-detect the prefix from the compiler's raw output and
# adopt it only when those exact bytes occur in that output.
if(MSVC AND CMAKE_GENERATOR MATCHES "^Ninja")
    set(_gcad_si_dir "${CMAKE_BINARY_DIR}/CMakeFiles/gcad-showincludes-probe")
    file(MAKE_DIRECTORY "${_gcad_si_dir}")
    file(WRITE "${_gcad_si_dir}/foo.h" "\n")
    file(WRITE "${_gcad_si_dir}/main.c" "#include \"foo.h\"\nint main(){}\n")
    if(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c main.c
            WORKING_DIRECTORY "${_gcad_si_dir}"
            OUTPUT_FILE "${_gcad_si_dir}/probe.txt"
            ERROR_FILE "${_gcad_si_dir}/probe.err"
            RESULT_VARIABLE _gcad_si_res)
        if(_gcad_si_res EQUAL 0)
            file(READ "${_gcad_si_dir}/probe.txt" _gcad_si_text)
            file(READ "${_gcad_si_dir}/probe.txt" _gcad_si_hex HEX)
            if(_gcad_si_text MATCHES "(^|\n)([^:\n][^:\n]+:[^:\n]*[^: \n][^: \n]:?[ \t]+)([A-Za-z]:\\\\|/)")
                set(_gcad_si_prefix "${CMAKE_MATCH_2}")
                string(HEX "${_gcad_si_prefix}" _gcad_si_prefix_hex)
                string(TOLOWER "${_gcad_si_hex}" _gcad_si_hex)
                string(TOLOWER "${_gcad_si_prefix_hex}" _gcad_si_prefix_hex)
                if(_gcad_si_hex MATCHES "${_gcad_si_prefix_hex}")
                    if(NOT "${_gcad_si_prefix}" STREQUAL "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}")
                        message(STATUS "MSVC /showIncludes prefix repaired: \"${_gcad_si_prefix}\"")
                    endif()
                    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${_gcad_si_prefix}")
                    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${_gcad_si_prefix}")
                endif()
            endif()
        endif()
    endif()
endif()

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
    target_compile_definitions(ElaWidgetTools INTERFACE ELAWIDGETTOOLS_LIBRARY)
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
    src/geometry/Epsilon.h
    src/geometry/CurveMath.h
    src/geometry/CurveMath.cpp
    src/geometry/CurveSplitter.h
    src/geometry/CurveSplitter.cpp
    src/geometry/TriangleUnfold.h
    src/geometry/TriangleUnfold.cpp
    src/geometry/RayCast.h
    src/geometry/RayCast.cpp
)
target_include_directories(gcad_geometry PUBLIC ${GCAD_SRC})
target_link_libraries(gcad_geometry PUBLIC Qt6::Core Qt6::Gui)

# ─── parametric: the engine core (never depends on UI/tools/canvas) ──────────
add_library(gcad_parametric STATIC
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
    src/parametric/DefaultAppearance.h
    src/parametric/Block.h
    src/parametric/Block.cpp
    src/parametric/BlockResolve.cpp
    src/parametric/BlockCurve.cpp
    src/parametric/BlockExtend.cpp
    src/parametric/BlockQuery.cpp
    src/parametric/BlockLifecycle.cpp
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
    src/parametric/ResolveDiagnostics.h
    src/parametric/CrossSelectionPolicy.h
    src/parametric/Resolver.h
    src/parametric/Resolver.cpp
    src/parametric/ResolverAttachment.cpp
    src/parametric/ResolverIntersection.cpp
    src/parametric/ResolverSlide.cpp
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
    src/canvas/CanvasFonts.h
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.h
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.h
    src/canvas/OriginCrosshair.cpp
    src/canvas/HudItem.h
    src/canvas/HudItem.cpp
    src/canvas/BlockItem.h
    src/canvas/BlockItem.cpp
    src/canvas/BlockGeometryCache.h
    src/canvas/BlockGeometryCache.cpp
    src/canvas/BlockItemPainter.h
    src/canvas/BlockItemPainter.cpp
    src/canvas/BlockItemPick.h
    src/canvas/BlockItemPick.cpp
    src/canvas/CurveItem.h
    src/canvas/CurveItem.cpp
    src/canvas/OverlapBatteryHud.h
    src/canvas/OverlapBatteryHud.cpp
    src/canvas/overlay/TransientOverlay.h
    src/canvas/overlay/TransientOverlay.cpp
)
target_include_directories(gcad_canvas PUBLIC ${GCAD_SRC})
target_link_libraries(gcad_canvas PUBLIC
    Qt6::Widgets Qt6::OpenGLWidgets ElaWidgetTools
    gcad_parametric gcad_geometry)

# ─── document: serialization + undo commands ─────────────────────────────────
add_library(gcad_document STATIC
    src/document/DocumentSerializer.h
    src/document/DocumentSerializer.cpp
    src/document/EnumCodec.h
    src/document/SchemaKeys.h
    src/document/CommandTexts.h
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
    src/document/commands/CircleCommands.h
    src/document/commands/CircleCommands.cpp
    src/document/commands/EndpointCommands.h
    src/document/commands/EndpointCommands.cpp
    src/document/commands/CommandIds.h
    src/document/commands/ComponentCommands.h
    src/document/commands/ComponentCommands.cpp
    src/document/commands/CascadeSnapshot.h
    src/document/commands/CascadeSnapshot.cpp
    src/document/commands/LayerCommands.h
    src/document/commands/LayerCommands.cpp
    src/document/commands/AttachmentCommands.h
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/AttachmentLifecycleCommands.h
    src/document/commands/AttachmentLifecycleCommands.cpp
    src/document/commands/ShadowLifecycleCommands.h
    src/document/commands/ShadowLifecycleCommands.cpp
    src/document/commands/AttachmentAngleCommands.h
    src/document/commands/AttachmentAngleCommands.cpp
    src/document/commands/AttachmentSlideCommands.h
    src/document/commands/AttachmentSlideCommands.cpp
    src/document/commands/VariableCommands.h
    src/document/commands/VariableCommands.cpp
    src/document/commands/LinkedVariableCommands.h
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
    src/ui/CardTabBase.h
    src/ui/CardTabBase.cpp
    src/ui/VariableTab.h
    src/ui/VariableTab.cpp
    src/ui/FormulaTab.h
    src/ui/FormulaTab.cpp
    src/ui/LinkedTab.h
    src/ui/LinkedTab.cpp
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
    src/ui/ConnHintFormatter.h
    src/ui/ElaMsgBox.h
    src/ui/ElaDialogButtons.h
    src/ui/FormScaffold.h
    src/ui/NumericFieldSpecs.h
    src/ui/UiStrings.h
    src/ui/DeleteImpactConfirm.h
    src/ui/DeleteImpactConfirm.cpp
    # ── P2-4: 线段属性对话框及其卡片 （原在 src/tools/，QWidget 归位 ui/）──
    # (AngleHud 已随 CONTEXT_STRIP 二期退场, 连接手势角度输入并入 ContextStrip)
    src/ui/LayerFeedback.h
    src/ui/AuxPointForm.h
    src/ui/AuxPointForm.cpp
    src/ui/QuickAuxDialog.h
    src/ui/QuickAuxDialog.cpp
    src/ui/PlacedPointDialog.h
    src/ui/PlacedPointDialog.cpp
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
    src/ui/LineEndpointSectionConn.cpp
    src/ui/LineAppearanceSection.h
    src/ui/LineAppearanceSection.cpp
    src/ui/LineGeometrySection.h
    src/ui/LineGeometrySection.cpp
    src/ui/CircleGeometrySection.h
    src/ui/CircleGeometrySection.cpp
    src/ui/LineOrthoOffsetCard.h
    src/ui/LineOrthoOffsetCard.cpp
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
    src/ui/SegmentAlignPointCard.h
    src/ui/SegmentAlignPointCard.cpp
    src/ui/SegmentShadowBasisCard.h
    src/ui/SegmentShadowBasisCard.cpp
    src/ui/SegmentAngleRefCard.h
    src/ui/SegmentAngleRefCard.cpp
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
    src/tools/InteractionTolerances.h
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
    src/tools/SmartPenAuxPoint.cpp
    src/tools/SmartPenStrokeInput.h
    src/tools/SmartPenStrokeInput.cpp
    src/tools/LineFactory.h
    src/tools/LineFactory.cpp
    # M1 画圆工具 (docs/design/CIRCLE_TOOL_DESIGN.md)
    src/tools/CircleFactory.h
    src/tools/CircleFactory.cpp
    src/tools/ToolCircle.h
    src/tools/ToolCircle.cpp
    src/tools/LeaderCandidatePicker.h
    src/tools/LeaderCandidatePicker.cpp
    src/tools/ToolCurveEdit.h
    src/tools/ToolCurveEdit.cpp
    src/tools/ToolCurveEditAnchor.cpp
    src/tools/ToolCurveEditHandles.cpp
    src/tools/ToolRotate.h
    src/tools/ToolRotate.cpp
    src/tools/RotateDragMath.h
    src/tools/RotateDragMath.cpp
    src/tools/RotateHintTexts.h
    src/tools/RotateHintTexts.cpp
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
    src/tools/IntersectionAngleAim.h
    src/tools/IntersectionToolVisuals.h
    src/tools/IntersectionToolVisuals.cpp
    src/tools/ToolPlacePoint.h
    src/tools/ToolPlacePoint.cpp
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
    src/app/MainWindowFileSession.cpp
    src/app/MainWindowMenuBar.cpp
    src/app/MainWindowToolBar.cpp
    src/app/MainWindowStatusBar.cpp
    src/app/MainWindowPanelWindow.cpp
    src/app/MainWindowSegmentMenu.cpp
    src/app/ContextStrip.h
    src/app/ContextStrip.cpp
    src/app/ContextStripSessions.cpp
    src/app/ContextStripDisplay.cpp
    src/app/ContextStripEdit.cpp
    src/app/PlacedPointStripBar.h
    src/app/PlacedPointStripBar.cpp
    src/app/CircleStripBar.h
    src/app/CircleStripBar.cpp
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
add_test(NAME check_inline_units
         COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
                 python ${CMAKE_SOURCE_DIR}/tools/check_inline_units.py)
set_tests_properties(check_layering check_test_fixtures check_hardcoded_colors
                     check_file_size check_header_classification check_bool_flags check_test_split
                     check_inline_units
    PROPERTIES RUN_SERIAL TRUE)

# ── Test: Tool hint completeness (TOOL_SYSTEM_AUDIT H3) ──
add_executable(test_tool_hints tests/test_tool_hints.cpp)
target_include_directories(test_tool_hints PRIVATE ${GCAD_SRC})
target_link_libraries(test_tool_hints PRIVATE gcad_tools gcad_ui Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME test_tool_hints COMMAND test_tool_hints)

# ── Test: Theme ↔ CanvasStyle 令牌同步守卫（2026-12 审计 P0-3 / 工单 G3）──
# 两张令牌表分属 ui 与 canvas 两层，无法共享同一类型，历史上只靠注释维持；
# 本测试把「Kept in sync ... by hand」变成可执行断言（同语义令牌逐通道相等，
# 含 alpha），有意分歧在测试内显式锁定。任一侧改动未跟随即在此失败。
add_executable(test_theme_sync tests/test_theme_sync.cpp)
target_include_directories(test_theme_sync PRIVATE ${GCAD_SRC})
target_link_libraries(test_theme_sync PRIVATE
    gcad_canvas gcad_ui gcad_parametric
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME test_theme_sync COMMAND test_theme_sync)

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

# ── Test: Resolver (Rule VIII test decomposition) ──
add_executable(test_resolver_points tests/test_resolver_points.cpp)
target_include_directories(test_resolver_points PRIVATE ${GCAD_SRC})
target_link_libraries(test_resolver_points PRIVATE gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_resolver_points COMMAND test_resolver_points)

add_executable(test_resolver_attachment tests/test_resolver_attachment.cpp)
target_include_directories(test_resolver_attachment PRIVATE ${GCAD_SRC})
target_link_libraries(test_resolver_attachment PRIVATE gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_resolver_attachment COMMAND test_resolver_attachment)

add_executable(test_resolver_curve_arc tests/test_resolver_curve_arc.cpp)
target_include_directories(test_resolver_curve_arc PRIVATE ${GCAD_SRC})
target_link_libraries(test_resolver_curve_arc PRIVATE gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_resolver_curve_arc COMMAND test_resolver_curve_arc)

add_executable(test_resolver_diag_misc tests/test_resolver_diag_misc.cpp)
target_include_directories(test_resolver_diag_misc PRIVATE ${GCAD_SRC})
target_link_libraries(test_resolver_diag_misc PRIVATE gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_resolver_diag_misc COMMAND test_resolver_diag_misc)

# ── Test: OrthoOffset (拐角偏置) ──
add_executable(test_ortho_offset tests/test_ortho_offset.cpp)
target_include_directories(test_ortho_offset PRIVATE ${GCAD_SRC})
target_link_libraries(test_ortho_offset PRIVATE gcad_ui gcad_canvas gcad_document gcad_parametric gcad_geometry gcad_tools Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test ElaWidgetTools)
add_test(NAME test_ortho_offset COMMAND test_ortho_offset)

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

# ── Test: breakSegment (Rule VIII test decomposition) ──
add_executable(test_break_basic tests/test_break_basic.cpp)
target_include_directories(test_break_basic PRIVATE ${GCAD_SRC})
target_link_libraries(test_break_basic PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_break_basic COMMAND test_break_basic)

add_executable(test_break_curve tests/test_break_curve.cpp)
target_include_directories(test_break_curve PRIVATE ${GCAD_SRC})
target_link_libraries(test_break_curve PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_break_curve COMMAND test_break_curve)

add_executable(test_break_follower tests/test_break_follower.cpp)
target_include_directories(test_break_follower PRIVATE ${GCAD_SRC})
target_link_libraries(test_break_follower PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_break_follower COMMAND test_break_follower)

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

add_executable(test_attachment_shadow tests/test_attachment_shadow.cpp)
target_include_directories(test_attachment_shadow PRIVATE ${GCAD_SRC})
target_link_libraries(test_attachment_shadow PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_attachment_shadow COMMAND test_attachment_shadow)

add_executable(test_attachment_slide tests/test_attachment_slide.cpp)
target_include_directories(test_attachment_slide PRIVATE ${GCAD_SRC})
target_link_libraries(test_attachment_slide PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_attachment_slide COMMAND test_attachment_slide)

add_executable(test_attachment_angle tests/test_attachment_angle.cpp)
target_include_directories(test_attachment_angle PRIVATE ${GCAD_SRC})
target_link_libraries(test_attachment_angle PRIVATE gcad_document gcad_parametric gcad_tools Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_attachment_angle COMMAND test_attachment_angle)

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
    tests/test_select_wkey_gestures.cpp
    tests/test_select_wkey_cards.cpp
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


# ── Test: rotate-copy (Rule VIII test decomposition) ──
add_executable(test_rotate_copy_semantics
    tests/test_rotate_copy_semantics.cpp
    resources/icons.qrc
)
target_include_directories(test_rotate_copy_semantics PRIVATE ${GCAD_SRC})
target_link_libraries(test_rotate_copy_semantics PRIVATE
    gcad_app gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_rotate_copy_semantics COMMAND test_rotate_copy_semantics)

add_executable(test_rotate_copy_shadow
    tests/test_rotate_copy_shadow.cpp
    resources/icons.qrc
)
target_include_directories(test_rotate_copy_shadow PRIVATE ${GCAD_SRC})
target_link_libraries(test_rotate_copy_shadow PRIVATE
    gcad_app gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_rotate_copy_shadow COMMAND test_rotate_copy_shadow)

add_executable(test_rotate_copy_endtarget
    tests/test_rotate_copy_endtarget.cpp
    resources/icons.qrc
)
target_include_directories(test_rotate_copy_endtarget PRIVATE ${GCAD_SRC})
target_link_libraries(test_rotate_copy_endtarget PRIVATE
    gcad_app gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_rotate_copy_endtarget COMMAND test_rotate_copy_endtarget)

# ── Test: rotate gestures (Rule VIII test decomposition) ──
add_executable(test_rotate_anchor
    tests/test_rotate_anchor.cpp
    resources/icons.qrc
)
target_include_directories(test_rotate_anchor PRIVATE ${GCAD_SRC})
target_link_libraries(test_rotate_anchor PRIVATE
    gcad_app gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_rotate_anchor COMMAND test_rotate_anchor)

add_executable(test_rotate_strip
    tests/test_rotate_strip.cpp
    resources/icons.qrc
)
target_include_directories(test_rotate_strip PRIVATE ${GCAD_SRC})
target_link_libraries(test_rotate_strip PRIVATE
    gcad_app gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_rotate_strip COMMAND test_rotate_strip)

# ── Test: 旋转手势角度域守卫（2026-12 审计 P0-4）──
# 写侧唯一入口 writeFollowerAngleForMode（Angle 存 [0,360) / ArcLength 用 360 域 /
# ChordLength 用折角域），读侧唯一入口 formatRotationBadge（旋转量不折叠、姿态角
# 一律折角域）。锁死「同一姿态两个数字」（连接段 −90.0° vs 自由段 270.0°）回归。
add_executable(test_rotate_angle_domain tests/test_rotate_angle_domain.cpp)
target_include_directories(test_rotate_angle_domain PRIVATE ${GCAD_SRC})
target_link_libraries(test_rotate_angle_domain PRIVATE
    gcad_tools gcad_parametric gcad_geometry
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME test_rotate_angle_domain COMMAND test_rotate_angle_domain)

add_executable(test_rotate_d15_gate
    tests/test_rotate_d15_gate.cpp
    resources/icons.qrc
)
target_include_directories(test_rotate_d15_gate PRIVATE ${GCAD_SRC})
target_link_libraries(test_rotate_d15_gate PRIVATE
    gcad_app gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_rotate_d15_gate COMMAND test_rotate_d15_gate)

add_executable(test_rotate_copy_flow
    tests/test_rotate_copy_flow.cpp
    resources/icons.qrc
)
target_include_directories(test_rotate_copy_flow PRIVATE ${GCAD_SRC})
target_link_libraries(test_rotate_copy_flow PRIVATE
    gcad_app gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_rotate_copy_flow COMMAND test_rotate_copy_flow)

add_executable(test_rotate_pivot_multi
    tests/test_rotate_pivot_multi.cpp
    resources/icons.qrc
)
target_include_directories(test_rotate_pivot_multi PRIVATE ${GCAD_SRC})
target_link_libraries(test_rotate_pivot_multi PRIVATE
    gcad_app gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_rotate_pivot_multi COMMAND test_rotate_pivot_multi)

# ── Test: line-property dialog tab switching (Rule VIII test decomposition) ──
add_executable(test_dialog_tabs_switch
    tests/test_dialog_tabs_switch.cpp
    resources/icons.qrc
)
target_include_directories(test_dialog_tabs_switch PRIVATE ${GCAD_SRC})
target_link_libraries(test_dialog_tabs_switch PRIVATE
    gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_dialog_tabs_switch COMMAND test_dialog_tabs_switch)

add_executable(test_dialog_tabs_angle_conn
    tests/test_dialog_tabs_angle_conn.cpp
    resources/icons.qrc
)
target_include_directories(test_dialog_tabs_angle_conn PRIVATE ${GCAD_SRC})
target_link_libraries(test_dialog_tabs_angle_conn PRIVATE
    gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_dialog_tabs_angle_conn COMMAND test_dialog_tabs_angle_conn)

add_executable(test_dialog_tabs_aux
    tests/test_dialog_tabs_aux.cpp
    resources/icons.qrc
)
target_include_directories(test_dialog_tabs_aux PRIVATE ${GCAD_SRC})
target_link_libraries(test_dialog_tabs_aux PRIVATE
    gcad_canvas gcad_tools gcad_ui gcad_document gcad_parametric
    Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_dialog_tabs_aux COMMAND test_dialog_tabs_aux)

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

# ── Test: place-point tool & placed point dialog ──
add_executable(test_place_point tests/test_place_point.cpp)
target_include_directories(test_place_point PRIVATE ${GCAD_SRC})
target_link_libraries(test_place_point PRIVATE
    gcad_ui gcad_canvas gcad_document gcad_parametric gcad_tools gcad_geometry
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test ElaWidgetTools)
add_test(NAME test_place_point COMMAND test_place_point)

# ── Test: overlap battery chips & drag landing pads (任务 4) ──
add_executable(test_overlap_battery tests/test_overlap_battery.cpp)
target_include_directories(test_overlap_battery PRIVATE ${GCAD_SRC})
target_link_libraries(test_overlap_battery PRIVATE
    gcad_canvas gcad_document gcad_parametric gcad_tools gcad_ui gcad_geometry
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test ElaWidgetTools)
add_test(NAME test_overlap_battery COMMAND test_overlap_battery)

# ── Test: transient visual overlay pipeline & lifecycle bus ──
add_executable(test_transient_overlay tests/test_transient_overlay.cpp)
target_include_directories(test_transient_overlay PRIVATE ${GCAD_SRC})
target_link_libraries(test_transient_overlay PRIVATE
    gcad_canvas gcad_document gcad_parametric gcad_tools gcad_ui gcad_geometry
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test ElaWidgetTools)
add_test(NAME test_transient_overlay COMMAND test_transient_overlay)

# 并行跑测（ctest -j）下，性能测量类测试必须独占运行，否则邻居用例的 CPU 抢占会污染计时断言。
# ctest 默认串行时本属性无影响；tools\test.bat all 与 CI 走并行。
set_tests_properties(test_perf test_canvas_perf PROPERTIES RUN_SERIAL TRUE)

# ── PDB 膨胀治理 + 链接映像完整性（2026-08 起，2026-12 泛化）──────────────
# 全部 test_* 目标关闭增量链接，两个理由：
# ① 这些测试把整个项目 + ElaWidgetTools 链接进一个 exe，Debug /Zi 符号下 PDB
#    曾因 /INCREMENTAL 跨重链只增不减累积到 ~1GB/个（实测 12GB 总量）；
# ② .ilk 陈旧时增量链接可能产出半更新映像（2026-12 实测 test_resolver_points.exe
#    的 InitializeSListHead IAT 槽为 0 → 启动即 AV，重链接即恢复；见 TROUBLESHOOTING.md）。
# 主程序 WildWindPattern 保留增量（日常调试迭代速度，见 AGENTS.md 构建命令）。
# 名单不再硬编码：按目标名 ^test_ 全量覆盖（新增测试自动生效）。
get_property(_gcad_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t IN LISTS _gcad_targets)
    if(_t MATCHES "^test_")
        target_link_options(${_t} PRIVATE /INCREMENTAL:NO)
    endif()
endforeach()

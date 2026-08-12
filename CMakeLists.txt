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
    # /FS: safe parallel compilation 鈥?several CL.EXE share one PDB per
    # target (without it, /m:N builds intermittently fail with C1041).
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
    # Chinese string literals (C2001/C2061).
    if(MSVC)
        target_compile_options(ElaWidgetTools PRIVATE /utf-8)
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

add_executable(${PROJECT_NAME}
    resources/icons.qrc
    src/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/SegmentEditBar.h
    src/app/SegmentEditBar.cpp
    src/app/DemoData.h
    src/app/DemoData.cpp
    src/geometry/Vec2.h
    src/geometry/Units.h
    src/geometry/Angle.h
    src/geometry/CurveMath.h
    src/geometry/CurveMath.cpp
    src/canvas/CanvasView.h
    src/canvas/CanvasView.cpp
    src/canvas/CanvasScene.h
    src/canvas/CanvasView.h
    src/canvas/CanvasView.cpp
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.h
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.h
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.h
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.h
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.h
    src/canvas/CurveItem.cpp
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
    src/parametric/Block.h
    src/parametric/Block.cpp
    src/parametric/Attachment.h
    src/parametric/AttachmentGraph.h
    src/parametric/FollowerAngle.h
    src/parametric/Resolver.h
    src/parametric/Resolver.cpp
    src/parametric/ParamDocument.h
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.h
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.h
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.h
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.h
    src/parametric/GroupRegistry.cpp
    src/parametric/Duplicate.h
    src/parametric/Duplicate.cpp
    src/parametric/Serial.h
    src/parametric/Serial.cpp
    src/parametric/Group.h
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
    src/ui/VariableCard.h
    src/ui/VariableCard.cpp
    src/ui/CopyChip.h
    src/ui/CopyChip.cpp
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
    src/ui/ConditionDialog.h
    src/ui/ConditionDialog.cpp
    src/ui/VariablePanel.h
    src/ui/MeasureTab.h
    src/ui/MeasureTab.cpp
    src/ui/VariablePanel.cpp
    src/ui/FormulaTabModel.h
    src/ui/FormulaTabModel.cpp
    src/ui/SerialDelegate.h
    src/ui/SerialDelegate.cpp
    src/ui/LayerPanel.h
    src/ui/LayerPanel.cpp
    src/ui/GroupPanel.h
    src/ui/GroupPanel.cpp
    src/tools/Tool.h
    src/tools/ToolManager.h
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.h
    src/tools/ToolSelect.cpp
    src/tools/MarqueeGesture.h
    src/tools/MarqueeGesture.cpp
    src/tools/SelectState.h
    src/tools/ConnectGesture.h
    src/tools/ConnectGesture.cpp
    src/tools/CopyDragController.h
    src/tools/CopyDragController.cpp
    src/tools/ToolSmartPen.h
    src/tools/ToolSmartPen.cpp
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
    src/tools/ToolBreak.h
    src/tools/ToolBreak.cpp
    src/tools/ToolIntersection.h
    src/tools/ToolIntersection.cpp
    src/tools/ToolMeasure.h
    src/tools/ToolMeasure.cpp
    src/tools/ToolAngleMeasure.h
    src/tools/ToolAngleMeasure.cpp
    src/tools/AngleHud.h
    src/tools/AngleHud.cpp
    src/tools/SnapEngine.h
    src/tools/SnapEngine.cpp
    src/tools/AuxPointForm.h
    src/tools/AuxPointForm.cpp
    src/tools/QuickAuxDialog.h
    src/tools/QuickAuxDialog.cpp
    src/tools/MeasureResultDialog.h
    src/tools/MeasureResultDialog.cpp
    src/tools/IntersectionForm.h
    src/tools/IntersectionForm.cpp
    src/tools/LinePropertyDialog.h
    src/tools/LinePropertyDialog.cpp
    src/tools/SegmentAnchorTab.h
    src/tools/SegmentAnchorTab.cpp
    src/tools/SegmentAuxTab.h
    src/tools/SegmentAuxTab.cpp
    src/tools/SegmentAimCard.h
    src/tools/SegmentAimCard.cpp
    src/tools/SegmentConnectionCard.h
    src/tools/SegmentConnectionCard.cpp
    src/tools/PointRefEdit.h
    src/tools/PointRefEdit.cpp
    src/document/DocumentSerializer.h
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.h
    src/document/DocumentFile.cpp
    src/document/DeleteImpactConfirm.h
    src/document/DeleteImpactConfirm.cpp
    src/document/commands/BlockCommands.h
    src/document/commands/BlockCommands.cpp
    src/document/commands/LayerCommands.h
    src/document/commands/LayerCommands.cpp
    src/document/commands/AttachmentCommands.h
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/VariableCommands.h
    src/document/commands/VariableCommands.cpp
    src/document/commands/DocumentCommands.h
    src/document/commands/DocumentCommands.cpp
    src/document/commands/GroupCommands.h
    src/document/commands/GroupCommands.cpp
    src/document/commands/BreakCommands.h
    src/document/commands/BreakCommands.cpp
    src/ui/IconHelper.h
    src/ui/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets miniz ElaWidgetTools spdlog::spdlog Tracy::TracyClient)

# --- Unit Tests (CTest + Qt Test) ---
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)

# Test: ExpressionEvaluator
add_executable(test_expression
    tests/test_expression.cpp
    src/parametric/ExpressionEvaluator.h
    src/parametric/ExpressionEvaluator.cpp
)
target_include_directories(test_expression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_expression PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_expression COMMAND test_expression)

# Test: Resolver
add_executable(test_resolver
    tests/test_resolver.cpp
    src/parametric/Resolver.h
    src/parametric/Resolver.cpp
    src/parametric/Block.h
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/ExpressionEvaluator.h
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.h
    src/parametric/ConditionEngine.cpp
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
    src/parametric/Attachment.h
    src/parametric/AttachmentGraph.h
    src/parametric/Condition.h
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/tools/SnapEngine.cpp
    src/geometry/Vec2.h
    src/geometry/Units.h
)
target_include_directories(test_resolver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_resolver PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_resolver COMMAND test_resolver)

# Test: DocumentSerializer round-trip
add_executable(test_serializer
    tests/test_serializer.cpp
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/tools/SnapEngine.cpp
)
target_include_directories(test_serializer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_serializer PRIVATE Qt6::Core Qt6::Gui Qt6::Test miniz)
add_test(NAME test_serializer COMMAND test_serializer)

# Test: real-doc P612 borrow-point intersection colinearity (1.gcad regression)
add_executable(test_p612_colinearity
    tests/test_p612_colinearity.cpp
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/tools/SnapEngine.cpp
)
target_include_directories(test_p612_colinearity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_p612_colinearity PRIVATE Qt6::Core Qt6::Gui Qt6::Test miniz)
add_test(NAME test_p612_colinearity COMMAND test_p612_colinearity)

# Test: user groups (閹存劗绮? make/ungroup, guards, undo, serialization)
add_executable(test_group
    tests/test_group.cpp
    src/document/commands/GroupCommands.cpp
    src/document/commands/DocumentCommands.cpp
    src/document/commands/BlockCommands.cpp
    src/document/DocumentSerializer.cpp
    src/parametric/Duplicate.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/tools/SnapEngine.cpp
)
target_include_directories(test_group PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_group PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_group COMMAND test_group)

# Test: duplicateBlocks (Ctrl+drag quick copy)
add_executable(test_duplicate
    tests/test_duplicate.cpp
    src/parametric/Duplicate.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/tools/SnapEngine.cpp
)
target_include_directories(test_duplicate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_duplicate PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_duplicate COMMAND test_duplicate)

# Test: breakSegment (point break tool)
add_executable(test_break
    tests/test_break.cpp
    src/document/commands/BreakCommands.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/tools/SnapEngine.cpp
)
target_include_directories(test_break PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_break PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_break COMMAND test_break)

# Test: intersection point + aux ref point
add_executable(test_intersection
    tests/test_intersection.cpp
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
    src/parametric/Attachment.h
    src/parametric/AttachmentGraph.h
    src/parametric/Condition.h
    src/geometry/Vec2.h
    src/geometry/Units.h
)
target_include_directories(test_intersection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_intersection PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_intersection COMMAND test_intersection)

# Test: document commands (BlockCommands + VariableCommands undo/redo)
add_executable(test_commands
    tests/test_commands.cpp
    src/document/commands/BlockCommands.cpp
    src/document/commands/VariableCommands.cpp
    src/document/commands/DocumentCommands.cpp
    src/document/commands/LayerCommands.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/tools/SnapEngine.cpp
)
target_include_directories(test_commands PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_commands PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_commands COMMAND test_commands)

# Test: CurveMath (B鑼厇ier / Catmull-Rom / arc length / projection / intersection)
add_executable(test_curve
    tests/test_curve.cpp
    src/geometry/CurveMath.h
    src/geometry/CurveMath.cpp
    src/geometry/Vec2.h
)
target_include_directories(test_curve PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_curve PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_curve COMMAND test_curve)

# Benchmark: layered dirty-marking resolve pipeline (aux frozen vs full)
add_executable(test_perf
    tests/test_perf.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/tools/SnapEngine.cpp
)
target_include_directories(test_perf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_perf PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_perf COMMAND test_perf)

# Test: auxiliary-layer rendering (aux point visibility across layer modes)
add_executable(test_aux_layer
    tests/test_aux_layer.cpp
    src/canvas/CanvasView.cpp
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.cpp
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.cpp
    src/tools/MarqueeGesture.h
    src/tools/MarqueeGesture.cpp
    src/tools/SelectState.h
    src/tools/ConnectGesture.h
    src/tools/ConnectGesture.cpp
    src/tools/CopyDragController.h
    src/tools/CopyDragController.cpp
    src/tools/ToolSmartPen.cpp
    src/tools/LineFactory.h
    src/tools/LineFactory.cpp
    src/tools/LeaderCandidatePicker.h
    src/tools/LeaderCandidatePicker.cpp
    src/tools/ToolCurveEdit.cpp
    src/tools/ToolRotate.cpp
    src/tools/RotateCopyGesture.h
    src/tools/RotateCopyGesture.cpp
    src/tools/RotateGizmo.h
    src/tools/RotateGizmo.cpp
    src/tools/ToolBreak.cpp
    src/tools/ToolIntersection.cpp
    src/tools/ToolMeasure.cpp
    src/tools/ToolAngleMeasure.cpp
    src/tools/AngleHud.cpp
    src/tools/SnapEngine.cpp
    src/tools/AuxPointForm.cpp
    src/tools/QuickAuxDialog.cpp
    src/tools/MeasureResultDialog.cpp
    src/tools/IntersectionForm.cpp
    src/tools/LinePropertyDialog.cpp
    src/tools/SegmentAnchorTab.h
    src/tools/SegmentAnchorTab.cpp
    src/tools/SegmentAuxTab.h
    src/tools/SegmentAuxTab.cpp
    src/tools/SegmentAimCard.h
    src/tools/SegmentAimCard.cpp
    src/tools/SegmentConnectionCard.h
    src/tools/SegmentConnectionCard.cpp
    src/tools/PointRefEdit.cpp
    src/document/commands/BlockCommands.cpp
    src/document/commands/DocumentCommands.cpp
    src/document/commands/GroupCommands.cpp
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/BreakCommands.cpp
    src/document/commands/VariableCommands.cpp
    src/document/DeleteImpactConfirm.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/parametric/Duplicate.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/ui/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
)
target_include_directories(test_aux_layer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_aux_layer PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg Qt6::Test ElaWidgetTools)
add_test(NAME test_aux_layer COMMAND test_aux_layer)

# Test: smart-pen line-body X marker / quick aux point interaction
add_executable(test_smartpen_aux
    tests/test_smartpen_aux.cpp
    src/tools/ToolSmartPen.cpp
    src/tools/LineFactory.h
    src/tools/LineFactory.cpp
    src/tools/LeaderCandidatePicker.h
    src/tools/LeaderCandidatePicker.cpp
    src/tools/ToolSelect.cpp
    src/tools/MarqueeGesture.h
    src/tools/MarqueeGesture.cpp
    src/tools/SelectState.h
    src/tools/ConnectGesture.h
    src/tools/ConnectGesture.cpp
    src/tools/CopyDragController.h
    src/tools/CopyDragController.cpp
    src/tools/SnapEngine.cpp
    src/tools/QuickAuxDialog.cpp
    src/tools/AuxPointForm.cpp
    src/tools/LinePropertyDialog.cpp
    src/tools/SegmentAnchorTab.h
    src/tools/SegmentAnchorTab.cpp
    src/tools/SegmentAuxTab.h
    src/tools/SegmentAuxTab.cpp
    src/tools/SegmentAimCard.h
    src/tools/SegmentAimCard.cpp
    src/tools/SegmentConnectionCard.h
    src/tools/SegmentConnectionCard.cpp
    src/tools/PointRefEdit.cpp
    src/tools/IntersectionForm.cpp
    src/tools/AngleHud.cpp
    src/document/commands/BlockCommands.cpp
    src/document/commands/DocumentCommands.cpp
    src/document/commands/GroupCommands.cpp
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/VariableCommands.cpp
    src/document/DeleteImpactConfirm.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/parametric/Duplicate.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.cpp
    src/ui/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
)
target_include_directories(test_smartpen_aux PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_smartpen_aux PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg Qt6::Test ElaWidgetTools)
add_test(NAME test_smartpen_aux COMMAND test_smartpen_aux)

# Test: SegmentEditBar (鍒涘缓鍚庡唴宓岀紪杈戞潯)
add_executable(test_segment_edit_bar
    tests/test_segment_edit_bar.cpp
    src/app/SegmentEditBar.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/parametric/Duplicate.cpp
    src/geometry/CurveMath.cpp
    src/document/commands/BlockCommands.cpp
)
target_include_directories(test_segment_edit_bar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_segment_edit_bar PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test ElaWidgetTools)
add_test(NAME test_segment_edit_bar COMMAND test_segment_edit_bar)

# Test: user-group tool guards (缂佸嫬鍞撮崡鏇犲殠娑撳秴褰查崚?/ 缂佸嫬鍞存潻鐐村复閹峰棔绗夊鈧?
add_executable(test_group_guards
    tests/test_group_guards.cpp
    src/tools/ToolSelect.cpp
    src/tools/MarqueeGesture.h
    src/tools/MarqueeGesture.cpp
    src/tools/SelectState.h
    src/tools/ConnectGesture.h
    src/tools/ConnectGesture.cpp
    src/tools/CopyDragController.h
    src/tools/CopyDragController.cpp
    src/tools/SnapEngine.cpp
    src/tools/AngleHud.cpp
    src/tools/LinePropertyDialog.cpp
    src/tools/SegmentAnchorTab.h
    src/tools/SegmentAnchorTab.cpp
    src/tools/SegmentAuxTab.h
    src/tools/SegmentAuxTab.cpp
    src/tools/SegmentAimCard.h
    src/tools/SegmentAimCard.cpp
    src/tools/SegmentConnectionCard.h
    src/tools/SegmentConnectionCard.cpp
    src/tools/PointRefEdit.cpp
    src/tools/AuxPointForm.cpp
    src/tools/IntersectionForm.cpp
    src/document/commands/BlockCommands.cpp
    src/document/commands/DocumentCommands.cpp
    src/document/commands/GroupCommands.cpp
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/VariableCommands.cpp
    src/document/DeleteImpactConfirm.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/parametric/Duplicate.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.cpp
    src/ui/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
)
target_include_directories(test_group_guards PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_group_guards PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg Qt6::Test ElaWidgetTools)
add_test(NAME test_group_guards COMMAND test_group_guards)

# Test: intersection tool aim-point (閹稿洤鎮滈悙? interaction flow 閳?full event
# chain through CanvasView (QTest mouse events 閳?dispatch 閳?tool).
add_executable(test_tool_intersection
    tests/test_tool_intersection.cpp
    resources/icons.qrc
    src/geometry/Vec2.h
    src/geometry/Units.h
    src/geometry/Angle.h
    src/geometry/CurveMath.h
    src/geometry/CurveMath.cpp
    src/canvas/CanvasView.h
    src/canvas/CanvasView.cpp
    src/canvas/CanvasScene.h
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.h
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.h
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.h
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.h
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.h
    src/canvas/CurveItem.cpp
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
    src/parametric/Block.h
    src/parametric/Block.cpp
    src/parametric/Attachment.h
    src/parametric/AttachmentGraph.h
    src/parametric/FollowerAngle.h
    src/parametric/Resolver.h
    src/parametric/Resolver.cpp
    src/parametric/ParamDocument.h
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.h
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.h
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.h
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.h
    src/parametric/GroupRegistry.cpp
    src/parametric/Duplicate.h
    src/parametric/Duplicate.cpp
    src/parametric/Serial.h
    src/parametric/Serial.cpp
    src/parametric/Group.h
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
    src/ui/VariableCard.h
    src/ui/VariableCard.cpp
    src/ui/CopyChip.h
    src/ui/CopyChip.cpp
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
    src/ui/ConditionDialog.h
    src/ui/ConditionDialog.cpp
    src/ui/VariablePanel.h
    src/ui/MeasureTab.h
    src/ui/MeasureTab.cpp
    src/ui/VariablePanel.cpp
    src/ui/FormulaTabModel.h
    src/ui/FormulaTabModel.cpp
    src/ui/SerialDelegate.h
    src/ui/SerialDelegate.cpp
    src/ui/LayerPanel.h
    src/ui/LayerPanel.cpp
    src/ui/GroupPanel.h
    src/ui/GroupPanel.cpp
    src/tools/Tool.h
    src/tools/ToolManager.h
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.h
    src/tools/ToolSelect.cpp
    src/tools/MarqueeGesture.h
    src/tools/MarqueeGesture.cpp
    src/tools/SelectState.h
    src/tools/ConnectGesture.h
    src/tools/ConnectGesture.cpp
    src/tools/CopyDragController.h
    src/tools/CopyDragController.cpp
    src/tools/ToolSmartPen.h
    src/tools/ToolSmartPen.cpp
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
    src/tools/ToolBreak.h
    src/tools/ToolBreak.cpp
    src/tools/ToolIntersection.h
    src/tools/ToolIntersection.cpp
    src/tools/ToolMeasure.h
    src/tools/ToolMeasure.cpp
    src/tools/ToolAngleMeasure.h
    src/tools/ToolAngleMeasure.cpp
    src/tools/AngleHud.h
    src/tools/AngleHud.cpp
    src/tools/SnapEngine.h
    src/tools/SnapEngine.cpp
    src/tools/AuxPointForm.h
    src/tools/AuxPointForm.cpp
    src/tools/QuickAuxDialog.h
    src/tools/QuickAuxDialog.cpp
    src/tools/MeasureResultDialog.h
    src/tools/MeasureResultDialog.cpp
    src/tools/IntersectionForm.h
    src/tools/IntersectionForm.cpp
    src/tools/LinePropertyDialog.h
    src/tools/LinePropertyDialog.cpp
    src/tools/SegmentAnchorTab.h
    src/tools/SegmentAnchorTab.cpp
    src/tools/SegmentAuxTab.h
    src/tools/SegmentAuxTab.cpp
    src/tools/SegmentAimCard.h
    src/tools/SegmentAimCard.cpp
    src/tools/SegmentConnectionCard.h
    src/tools/SegmentConnectionCard.cpp
    src/tools/PointRefEdit.h
    src/tools/PointRefEdit.cpp
    src/document/DocumentSerializer.h
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.h
    src/document/DocumentFile.cpp
    src/document/DeleteImpactConfirm.h
    src/document/DeleteImpactConfirm.cpp
    src/document/commands/BlockCommands.h
    src/document/commands/BlockCommands.cpp
    src/document/commands/LayerCommands.h
    src/document/commands/LayerCommands.cpp
    src/document/commands/AttachmentCommands.h
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/VariableCommands.h
    src/document/commands/VariableCommands.cpp
    src/document/commands/DocumentCommands.h
    src/document/commands/DocumentCommands.cpp
    src/document/commands/GroupCommands.h
    src/document/commands/GroupCommands.cpp
    src/document/commands/BreakCommands.h
    src/document/commands/BreakCommands.cpp
    src/ui/IconHelper.h
    src/ui/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
)
target_include_directories(test_tool_intersection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_tool_intersection PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_tool_intersection COMMAND test_tool_intersection)

# Test: select tool W-key mode toggle through the full event
# chain: real QKeyEvent -> CanvasView::keyPressEvent -> ToolManager -> ToolSelect.
add_executable(test_select_wkey
    tests/test_select_wkey.cpp
    src/geometry/Vec2.h
    src/geometry/Units.h
    src/geometry/CurveMath.h
    src/geometry/CurveMath.cpp
    src/canvas/CanvasView.h
    src/canvas/CanvasView.cpp
    src/canvas/CanvasScene.h
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.h
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.h
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.h
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.h
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.h
    src/canvas/CurveItem.cpp
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
    src/parametric/Block.h
    src/parametric/Block.cpp
    src/parametric/Attachment.h
    src/parametric/AttachmentGraph.h
    src/parametric/FollowerAngle.h
    src/parametric/Resolver.h
    src/parametric/Resolver.cpp
    src/parametric/ParamDocument.h
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.h
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.h
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.h
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.h
    src/parametric/GroupRegistry.cpp
    src/parametric/Duplicate.h
    src/parametric/Duplicate.cpp
    src/parametric/Serial.h
    src/parametric/Serial.cpp
    src/parametric/Group.h
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
    src/tools/Tool.h
    src/tools/ToolManager.h
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.h
    src/tools/ToolSelect.cpp
    src/tools/MarqueeGesture.h
    src/tools/MarqueeGesture.cpp
    src/tools/SelectState.h
    src/tools/ConnectGesture.h
    src/tools/ConnectGesture.cpp
    src/tools/CopyDragController.h
    src/tools/CopyDragController.cpp
    src/tools/ToolSmartPen.h
    src/tools/ToolSmartPen.cpp
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
    src/tools/ToolBreak.h
    src/tools/ToolBreak.cpp
    src/tools/ToolIntersection.h
    src/tools/ToolIntersection.cpp
    src/tools/ToolMeasure.h
    src/tools/ToolMeasure.cpp
    src/tools/ToolAngleMeasure.h
    src/tools/ToolAngleMeasure.cpp
    src/tools/AngleHud.h
    src/tools/AngleHud.cpp
    src/tools/SnapEngine.h
    src/tools/SnapEngine.cpp
    src/tools/AuxPointForm.h
    src/tools/AuxPointForm.cpp
    src/tools/QuickAuxDialog.h
    src/tools/QuickAuxDialog.cpp
    src/tools/MeasureResultDialog.h
    src/tools/MeasureResultDialog.cpp
    src/tools/IntersectionForm.h
    src/tools/IntersectionForm.cpp
    src/tools/LinePropertyDialog.h
    src/tools/LinePropertyDialog.cpp
    src/tools/SegmentAnchorTab.h
    src/tools/SegmentAnchorTab.cpp
    src/tools/SegmentAuxTab.h
    src/tools/SegmentAuxTab.cpp
    src/tools/SegmentAimCard.h
    src/tools/SegmentAimCard.cpp
    src/tools/SegmentConnectionCard.h
    src/tools/SegmentConnectionCard.cpp
    src/tools/PointRefEdit.h
    src/tools/PointRefEdit.cpp
    src/document/DocumentSerializer.h
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.h
    src/document/DocumentFile.cpp
    src/document/DeleteImpactConfirm.h
    src/document/DeleteImpactConfirm.cpp
    src/document/commands/BlockCommands.h
    src/document/commands/BlockCommands.cpp
    src/document/commands/LayerCommands.h
    src/document/commands/LayerCommands.cpp
    src/document/commands/AttachmentCommands.h
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/VariableCommands.h
    src/document/commands/VariableCommands.cpp
    src/document/commands/DocumentCommands.h
    src/document/commands/DocumentCommands.cpp
    src/document/commands/GroupCommands.h
    src/document/commands/GroupCommands.cpp
    src/document/commands/BreakCommands.h
    src/document/commands/BreakCommands.cpp
    src/ui/IconHelper.h
    src/ui/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
)
target_include_directories(test_select_wkey PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_select_wkey PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_select_wkey COMMAND test_select_wkey)

# Test: rotate-copy (Ctrl+drag in ToolRotate) 鈥?clone attaches back to the
# original with a relative construction angle; full event-chain coverage.
add_executable(test_rotate_copy
    tests/test_rotate_copy.cpp
    src/geometry/Vec2.h
    src/geometry/Units.h
    src/geometry/CurveMath.h
    src/geometry/CurveMath.cpp
    src/canvas/CanvasView.h
    src/canvas/CanvasView.cpp
    src/canvas/CanvasScene.h
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.h
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.h
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.h
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.h
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.h
    src/canvas/CurveItem.cpp
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
    src/parametric/Block.h
    src/parametric/Block.cpp
    src/parametric/Attachment.h
    src/parametric/AttachmentGraph.h
    src/parametric/FollowerAngle.h
    src/parametric/Resolver.h
    src/parametric/Resolver.cpp
    src/parametric/ParamDocument.h
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.h
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.h
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.h
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.h
    src/parametric/GroupRegistry.cpp
    src/parametric/Duplicate.h
    src/parametric/Duplicate.cpp
    src/parametric/Serial.h
    src/parametric/Serial.cpp
    src/parametric/Group.h
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
    src/tools/Tool.h
    src/tools/ToolManager.h
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.h
    src/tools/ToolSelect.cpp
    src/tools/MarqueeGesture.h
    src/tools/MarqueeGesture.cpp
    src/tools/SelectState.h
    src/tools/ConnectGesture.h
    src/tools/ConnectGesture.cpp
    src/tools/CopyDragController.h
    src/tools/CopyDragController.cpp
    src/tools/ToolSmartPen.h
    src/tools/ToolSmartPen.cpp
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
    src/tools/ToolBreak.h
    src/tools/ToolBreak.cpp
    src/tools/ToolIntersection.h
    src/tools/ToolIntersection.cpp
    src/tools/ToolMeasure.h
    src/tools/ToolMeasure.cpp
    src/tools/ToolAngleMeasure.h
    src/tools/ToolAngleMeasure.cpp
    src/tools/AngleHud.h
    src/tools/AngleHud.cpp
    src/tools/SnapEngine.h
    src/tools/SnapEngine.cpp
    src/tools/LinePropertyDialog.h
    src/tools/LinePropertyDialog.cpp
    src/tools/SegmentAnchorTab.h
    src/tools/SegmentAnchorTab.cpp
    src/tools/SegmentAuxTab.h
    src/tools/SegmentAuxTab.cpp
    src/tools/SegmentAimCard.h
    src/tools/SegmentAimCard.cpp
    src/tools/SegmentConnectionCard.h
    src/tools/SegmentConnectionCard.cpp
    src/tools/PointRefEdit.h
    src/tools/PointRefEdit.cpp
    src/tools/AuxPointForm.h
    src/tools/AuxPointForm.cpp
    src/tools/QuickAuxDialog.h
    src/tools/QuickAuxDialog.cpp
    src/tools/MeasureResultDialog.h
    src/tools/MeasureResultDialog.cpp
    src/tools/IntersectionForm.h
    src/tools/IntersectionForm.cpp
    src/document/DocumentSerializer.h
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.h
    src/document/DocumentFile.cpp
    src/document/DeleteImpactConfirm.h
    src/document/DeleteImpactConfirm.cpp
    src/document/commands/BlockCommands.h
    src/document/commands/BlockCommands.cpp
    src/document/commands/LayerCommands.h
    src/document/commands/LayerCommands.cpp
    src/document/commands/AttachmentCommands.h
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/VariableCommands.h
    src/document/commands/VariableCommands.cpp
    src/document/commands/DocumentCommands.h
    src/document/commands/DocumentCommands.cpp
    src/document/commands/GroupCommands.h
    src/document/commands/GroupCommands.cpp
    src/document/commands/BreakCommands.h
    src/document/commands/BreakCommands.cpp
    src/ui/IconHelper.h
    src/ui/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
)
target_include_directories(test_rotate_copy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_rotate_copy PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_rotate_copy COMMAND test_rotate_copy)

# Test: line-property dialog tab switching (灞炴€?tab must not freeze when the
# aux tab's focused editor loses focus on page switch).
add_executable(test_dialog_tabs
    tests/test_dialog_tabs.cpp
    src/geometry/Vec2.h
    src/geometry/Units.h
    src/geometry/CurveMath.h
    src/geometry/CurveMath.cpp
    src/canvas/CanvasView.h
    src/canvas/CanvasView.cpp
    src/canvas/CanvasScene.h
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.h
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.h
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.h
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.h
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.h
    src/canvas/CurveItem.cpp
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
    src/parametric/Block.h
    src/parametric/Block.cpp
    src/parametric/Attachment.h
    src/parametric/AttachmentGraph.h
    src/parametric/FollowerAngle.h
    src/parametric/Resolver.h
    src/parametric/Resolver.cpp
    src/parametric/ParamDocument.h
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.h
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.h
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.h
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.h
    src/parametric/GroupRegistry.cpp
    src/parametric/Duplicate.h
    src/parametric/Duplicate.cpp
    src/parametric/Serial.h
    src/parametric/Serial.cpp
    src/parametric/Group.h
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
    src/tools/Tool.h
    src/tools/ToolManager.h
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.h
    src/tools/ToolSelect.cpp
    src/tools/MarqueeGesture.h
    src/tools/MarqueeGesture.cpp
    src/tools/SelectState.h
    src/tools/ConnectGesture.h
    src/tools/ConnectGesture.cpp
    src/tools/CopyDragController.h
    src/tools/CopyDragController.cpp
    src/tools/ToolSmartPen.h
    src/tools/ToolSmartPen.cpp
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
    src/tools/ToolBreak.h
    src/tools/ToolBreak.cpp
    src/tools/ToolIntersection.h
    src/tools/ToolIntersection.cpp
    src/tools/ToolMeasure.h
    src/tools/ToolMeasure.cpp
    src/tools/ToolAngleMeasure.h
    src/tools/ToolAngleMeasure.cpp
    src/tools/AngleHud.h
    src/tools/AngleHud.cpp
    src/tools/SnapEngine.h
    src/tools/SnapEngine.cpp
    src/tools/LinePropertyDialog.h
    src/tools/LinePropertyDialog.cpp
    src/tools/SegmentAnchorTab.h
    src/tools/SegmentAnchorTab.cpp
    src/tools/SegmentAuxTab.h
    src/tools/SegmentAuxTab.cpp
    src/tools/SegmentAimCard.h
    src/tools/SegmentAimCard.cpp
    src/tools/SegmentConnectionCard.h
    src/tools/SegmentConnectionCard.cpp
    src/tools/PointRefEdit.h
    src/tools/PointRefEdit.cpp
    src/tools/AuxPointForm.h
    src/tools/AuxPointForm.cpp
    src/tools/QuickAuxDialog.h
    src/tools/QuickAuxDialog.cpp
    src/tools/MeasureResultDialog.h
    src/tools/MeasureResultDialog.cpp
    src/tools/IntersectionForm.h
    src/tools/IntersectionForm.cpp
    src/document/DocumentSerializer.h
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.h
    src/document/DocumentFile.cpp
    src/document/DeleteImpactConfirm.h
    src/document/DeleteImpactConfirm.cpp
    src/document/commands/BlockCommands.h
    src/document/commands/BlockCommands.cpp
    src/document/commands/LayerCommands.h
    src/document/commands/LayerCommands.cpp
    src/document/commands/AttachmentCommands.h
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/VariableCommands.h
    src/document/commands/VariableCommands.cpp
    src/document/commands/DocumentCommands.h
    src/document/commands/DocumentCommands.cpp
    src/document/commands/GroupCommands.h
    src/document/commands/GroupCommands.cpp
    src/document/commands/BreakCommands.h
    src/document/commands/BreakCommands.cpp
    src/ui/IconHelper.h
    src/ui/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
)
target_include_directories(test_dialog_tabs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_dialog_tabs PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)
add_test(NAME test_dialog_tabs COMMAND test_dialog_tabs)

# Benchmark: curve-heavy canvas interaction (compute + software repaint cost)
add_executable(test_canvas_perf
    tests/test_canvas_perf.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/tools/SnapEngine.cpp
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.cpp
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
)
target_include_directories(test_canvas_perf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_canvas_perf PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)
add_test(NAME test_canvas_perf COMMAND test_canvas_perf)

# Test: hold-to-show overlays (N = all names, L = all lengths) — force flags
# reveal labels at the pixel level, release restores model flags, and
# overlapping same-name points merge into one label.
add_executable(test_hold_show
    tests/test_hold_show.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.cpp
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
)
target_include_directories(test_hold_show PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_hold_show PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)
add_test(NAME test_hold_show COMMAND test_hold_show)

# Benchmark: real user document drag-frame breakdown (env GCAD_DOC=<path>)
add_executable(test_realdoc_perf
    tests/test_realdoc_perf.cpp
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/tools/SnapEngine.cpp
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.cpp
)
target_include_directories(test_realdoc_perf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_realdoc_perf PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test miniz)

# Benchmark: FULL MainWindow drag on a real document (env GCAD_DOC=<path>).
# Same sources as the app itself (minus main.cpp) for full fidelity.
add_executable(test_realdoc_full
    tests/test_realdoc_full.cpp
    resources/icons.qrc
    src/app/MainWindow.cpp
    src/app/SegmentEditBar.cpp
    src/app/DemoData.cpp
    src/geometry/CurveMath.cpp
    src/canvas/CanvasView.h
    src/canvas/CanvasView.cpp
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/parametric/Duplicate.cpp
    src/ui/VariableCard.cpp
    src/ui/FormulaCard.cpp
    src/ui/FormulaGroupHeader.cpp
    src/ui/LinkedCard.cpp
    src/ui/MeasureCard.cpp
    src/ui/AngleMeasureCard.cpp
    src/ui/VirtualCardList.cpp
    src/ui/ConditionDialog.cpp
    src/ui/VariablePanel.cpp
    src/ui/MeasureTab.h
    src/ui/MeasureTab.cpp
    src/ui/FormulaTabModel.h
    src/ui/FormulaTabModel.cpp
    src/ui/SerialDelegate.cpp
    src/ui/LayerPanel.cpp
    src/ui/GroupPanel.cpp
    src/ui/CopyChip.cpp
    src/ui/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.cpp
    src/tools/MarqueeGesture.h
    src/tools/MarqueeGesture.cpp
    src/tools/SelectState.h
    src/tools/ConnectGesture.h
    src/tools/ConnectGesture.cpp
    src/tools/CopyDragController.h
    src/tools/CopyDragController.cpp
    src/tools/ToolSmartPen.cpp
    src/tools/LineFactory.h
    src/tools/LineFactory.cpp
    src/tools/LeaderCandidatePicker.h
    src/tools/LeaderCandidatePicker.cpp
    src/tools/ToolCurveEdit.cpp
    src/tools/ToolRotate.cpp
    src/tools/RotateCopyGesture.h
    src/tools/RotateCopyGesture.cpp
    src/tools/RotateGizmo.h
    src/tools/RotateGizmo.cpp
    src/tools/ToolBreak.cpp
    src/tools/ToolIntersection.cpp
    src/tools/ToolMeasure.cpp
    src/tools/ToolAngleMeasure.cpp
    src/tools/AngleHud.cpp
    src/tools/SnapEngine.cpp
    src/tools/AuxPointForm.cpp
    src/tools/QuickAuxDialog.cpp
    src/tools/MeasureResultDialog.cpp
    src/tools/IntersectionForm.cpp
    src/tools/LinePropertyDialog.cpp
    src/tools/SegmentAnchorTab.h
    src/tools/SegmentAnchorTab.cpp
    src/tools/SegmentAuxTab.h
    src/tools/SegmentAuxTab.cpp
    src/tools/SegmentAimCard.h
    src/tools/SegmentAimCard.cpp
    src/tools/SegmentConnectionCard.h
    src/tools/SegmentConnectionCard.cpp
    src/tools/PointRefEdit.cpp
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.cpp
    src/document/DeleteImpactConfirm.cpp
    src/document/commands/BlockCommands.cpp
    src/document/commands/LayerCommands.cpp
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/VariableCommands.cpp
    src/document/commands/DocumentCommands.cpp
    src/document/commands/GroupCommands.cpp
    src/document/commands/BreakCommands.cpp
)
target_include_directories(test_realdoc_full PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_realdoc_full PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)

# Smoke test: ElaWindow navigation stack switching (reproduces the
# "点击图层=闪退" crash). Not registered with ctest — run manually.
add_executable(test_nav_smoke
    tests/test_nav_smoke.cpp
    resources/icons.qrc
    src/app/MainWindow.cpp
    src/app/SegmentEditBar.cpp
    src/app/DemoData.cpp
    src/geometry/CurveMath.cpp
    src/canvas/CanvasView.h
    src/canvas/CanvasView.cpp
    src/canvas/CanvasScene.cpp
    src/canvas/GroupBadgeItem.h
    src/canvas/GroupBadgeItem.cpp
    src/canvas/CanvasStyle.cpp
    src/canvas/CanvasAnimator.cpp
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.cpp
    src/canvas/CurveItem.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/LayerRegistry.cpp
    src/parametric/VariableStore.cpp
    src/parametric/MeasurementStore.cpp
    src/parametric/GroupRegistry.cpp
    src/parametric/Block.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/parametric/Duplicate.cpp
    src/ui/VariableCard.cpp
    src/ui/FormulaCard.cpp
    src/ui/FormulaGroupHeader.cpp
    src/ui/LinkedCard.cpp
    src/ui/MeasureCard.cpp
    src/ui/AngleMeasureCard.cpp
    src/ui/VirtualCardList.cpp
    src/ui/ConditionDialog.cpp
    src/ui/VariablePanel.cpp
    src/ui/MeasureTab.h
    src/ui/MeasureTab.cpp
    src/ui/FormulaTabModel.h
    src/ui/FormulaTabModel.cpp
    src/ui/SerialDelegate.cpp
    src/ui/LayerPanel.cpp
    src/ui/GroupPanel.cpp
    src/ui/CopyChip.cpp
    src/ui/IconHelper.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.cpp
    src/tools/MarqueeGesture.h
    src/tools/MarqueeGesture.cpp
    src/tools/SelectState.h
    src/tools/ConnectGesture.h
    src/tools/ConnectGesture.cpp
    src/tools/CopyDragController.h
    src/tools/CopyDragController.cpp
    src/tools/ToolSmartPen.cpp
    src/tools/LineFactory.h
    src/tools/LineFactory.cpp
    src/tools/LeaderCandidatePicker.h
    src/tools/LeaderCandidatePicker.cpp
    src/tools/ToolCurveEdit.cpp
    src/tools/ToolRotate.cpp
    src/tools/RotateCopyGesture.h
    src/tools/RotateCopyGesture.cpp
    src/tools/RotateGizmo.h
    src/tools/RotateGizmo.cpp
    src/tools/ToolBreak.cpp
    src/tools/ToolIntersection.cpp
    src/tools/ToolMeasure.cpp
    src/tools/ToolAngleMeasure.cpp
    src/tools/AngleHud.cpp
    src/tools/SnapEngine.cpp
    src/tools/AuxPointForm.cpp
    src/tools/QuickAuxDialog.cpp
    src/tools/MeasureResultDialog.cpp
    src/tools/IntersectionForm.cpp
    src/tools/LinePropertyDialog.cpp
    src/tools/SegmentAnchorTab.h
    src/tools/SegmentAnchorTab.cpp
    src/tools/SegmentAuxTab.h
    src/tools/SegmentAuxTab.cpp
    src/tools/SegmentAimCard.h
    src/tools/SegmentAimCard.cpp
    src/tools/SegmentConnectionCard.h
    src/tools/SegmentConnectionCard.cpp
    src/tools/PointRefEdit.cpp
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.cpp
    src/document/DeleteImpactConfirm.cpp
    src/document/commands/BlockCommands.cpp
    src/document/commands/LayerCommands.cpp
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/VariableCommands.cpp
    src/document/commands/DocumentCommands.cpp
    src/document/commands/GroupCommands.cpp
    src/document/commands/BreakCommands.cpp
)
target_include_directories(test_nav_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_nav_smoke PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz ElaWidgetTools)

# Test: spdlog + Tracy integration (file-sink log write + ZoneScoped probe)
add_executable(test_log_probe
    tests/test_log_probe.cpp
)
target_include_directories(test_log_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_log_probe PRIVATE Qt6::Core Qt6::Test spdlog::spdlog Tracy::TracyClient)
add_test(NAME test_log_probe COMMAND test_log_probe)

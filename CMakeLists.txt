cmake_minimum_required(VERSION 3.25)

project(GarmentCAD
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
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg OpenGLWidgets)

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

add_executable(${PROJECT_NAME}
    resources/icons.qrc
    src/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
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
    src/ui/VariablePanel.cpp
    src/ui/SerialDelegate.h
    src/ui/SerialDelegate.cpp
    src/ui/LayerPanel.h
    src/ui/LayerPanel.cpp
    src/ui/GroupPanel.h
    src/ui/GroupPanel.cpp
    src/ui/SidePanel.h
    src/ui/SidePanel.cpp
    src/tools/Tool.h
    src/tools/ToolManager.h
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.h
    src/tools/ToolSelect.cpp
    src/tools/ToolSmartPen.h
    src/tools/ToolSmartPen.cpp
    src/tools/ToolCurveEdit.h
    src/tools/ToolCurveEdit.cpp
    src/tools/ToolRotate.h
    src/tools/ToolRotate.cpp
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
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets miniz)

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

# Test: user groups (鎴愮粍: make/ungroup, guards, undo, serialization)
add_executable(test_group
    tests/test_group.cpp
    src/document/commands/GroupCommands.cpp
    src/document/commands/DocumentCommands.cpp
    src/document/commands/BlockCommands.cpp
    src/document/DocumentSerializer.cpp
    src/parametric/Duplicate.cpp
    src/parametric/ParamDocument.cpp
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

# Test: CurveMath (B茅zier / Catmull-Rom / arc length / projection / intersection)
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
    src/tools/ToolSmartPen.cpp
    src/tools/ToolCurveEdit.cpp
    src/tools/ToolRotate.cpp
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
    src/tools/PointRefEdit.cpp
    src/document/commands/BlockCommands.cpp
    src/document/commands/DocumentCommands.cpp
    src/document/commands/GroupCommands.cpp
    src/document/commands/AttachmentCommands.cpp
    src/document/commands/BreakCommands.cpp
    src/document/commands/VariableCommands.cpp
    src/document/DeleteImpactConfirm.cpp
    src/parametric/ParamDocument.cpp
    src/parametric/Block.cpp
    src/parametric/Duplicate.cpp
    src/geometry/CurveMath.cpp
    src/parametric/Resolver.cpp
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.cpp
    src/parametric/Serial.cpp
    src/parametric/LinkedVariable.cpp
    src/ui/IconHelper.cpp
)
target_include_directories(test_aux_layer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_aux_layer PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg Qt6::Test)
add_test(NAME test_aux_layer COMMAND test_aux_layer)

# Test: smart-pen line-body X marker / quick aux point interaction
add_executable(test_smartpen_aux
    tests/test_smartpen_aux.cpp
    src/tools/ToolSmartPen.cpp
    src/tools/ToolSelect.cpp
    src/tools/SnapEngine.cpp
    src/tools/QuickAuxDialog.cpp
    src/tools/AuxPointForm.cpp
    src/tools/LinePropertyDialog.cpp
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
)
target_include_directories(test_smartpen_aux PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_smartpen_aux PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg Qt6::Test)
add_test(NAME test_smartpen_aux COMMAND test_smartpen_aux)

# Test: user-group tool guards (缁勫唴鍗曠嚎涓嶅彲鍒?/ 缁勫唴杩炴帴鎷嗕笉寮€)
add_executable(test_group_guards
    tests/test_group_guards.cpp
    src/tools/ToolSelect.cpp
    src/tools/SnapEngine.cpp
    src/tools/AngleHud.cpp
    src/tools/LinePropertyDialog.cpp
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
)
target_include_directories(test_group_guards PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_group_guards PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg Qt6::Test)
add_test(NAME test_group_guards COMMAND test_group_guards)

# Test: intersection tool aim-point (鎸囧悜鐐? interaction flow 鈥?full event
# chain through CanvasView (QTest mouse events 鈫?dispatch 鈫?tool).
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
    src/ui/VariablePanel.cpp
    src/ui/SerialDelegate.h
    src/ui/SerialDelegate.cpp
    src/ui/LayerPanel.h
    src/ui/LayerPanel.cpp
    src/ui/GroupPanel.h
    src/ui/GroupPanel.cpp
    src/ui/SidePanel.h
    src/ui/SidePanel.cpp
    src/tools/Tool.h
    src/tools/ToolManager.h
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.h
    src/tools/ToolSelect.cpp
    src/tools/ToolSmartPen.h
    src/tools/ToolSmartPen.cpp
    src/tools/ToolCurveEdit.h
    src/tools/ToolCurveEdit.cpp
    src/tools/ToolRotate.h
    src/tools/ToolRotate.cpp
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
)
target_include_directories(test_tool_intersection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_tool_intersection PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz)
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
    src/tools/ToolSmartPen.h
    src/tools/ToolSmartPen.cpp
    src/tools/ToolCurveEdit.h
    src/tools/ToolCurveEdit.cpp
    src/tools/ToolRotate.h
    src/tools/ToolRotate.cpp
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
)
target_include_directories(test_select_wkey PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_select_wkey PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz)
add_test(NAME test_select_wkey COMMAND test_select_wkey)

# Test: rotate-copy (Ctrl+drag in ToolRotate) — clone attaches back to the
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
    src/tools/ToolSmartPen.h
    src/tools/ToolSmartPen.cpp
    src/tools/ToolCurveEdit.h
    src/tools/ToolCurveEdit.cpp
    src/tools/ToolRotate.h
    src/tools/ToolRotate.cpp
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
)
target_include_directories(test_rotate_copy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_rotate_copy PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz)
add_test(NAME test_rotate_copy COMMAND test_rotate_copy)

# Benchmark: curve-heavy canvas interaction (compute + software repaint cost)
add_executable(test_canvas_perf
    tests/test_canvas_perf.cpp
    src/parametric/ParamDocument.cpp
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

# Benchmark: real user document drag-frame breakdown (env GCAD_DOC=<path>)
add_executable(test_realdoc_perf
    tests/test_realdoc_perf.cpp
    src/document/DocumentSerializer.cpp
    src/document/DocumentFile.cpp
    src/parametric/ParamDocument.cpp
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
    src/app/DemoData.cpp
    src/geometry/CurveMath.cpp
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
    src/ui/SerialDelegate.cpp
    src/ui/LayerPanel.cpp
    src/ui/GroupPanel.cpp
    src/ui/SidePanel.cpp
    src/ui/CopyChip.cpp
    src/ui/IconHelper.cpp
    src/tools/ToolManager.cpp
    src/tools/ToolSelect.cpp
    src/tools/ToolSmartPen.cpp
    src/tools/ToolCurveEdit.cpp
    src/tools/ToolRotate.cpp
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
target_link_libraries(test_realdoc_full PRIVATE Qt6::Widgets Qt6::Svg Qt6::OpenGLWidgets Qt6::Test miniz)

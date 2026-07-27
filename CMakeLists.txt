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

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/DemoData.h
    src/app/DemoData.cpp
    src/geometry/Vec2.h
    src/geometry/Units.h
    src/canvas/CanvasView.h
    src/canvas/CanvasView.cpp
    src/canvas/CanvasScene.h
    src/canvas/CanvasScene.cpp
    src/canvas/OriginCrosshair.h
    src/canvas/OriginCrosshair.cpp
    src/canvas/BlockItem.h
    src/canvas/BlockItem.cpp
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
    src/parametric/Block.h
    src/parametric/Block.cpp
    src/parametric/Attachment.h
    src/parametric/Resolver.h
    src/parametric/Resolver.cpp
    src/parametric/ParamDocument.h
    src/parametric/ParamDocument.cpp
    src/parametric/Serial.h
    src/parametric/Serial.cpp
    src/parametric/Group.h
    src/parametric/GroupModel.h
    src/parametric/GroupModel.cpp
    src/parametric/Variable.h
    src/parametric/FormulaVariable.h
    src/parametric/ExpressionEvaluator.h
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/Condition.h
    src/parametric/ConditionEngine.h
    src/parametric/ConditionEngine.cpp
    src/ui/VariableCard.h
    src/ui/VariableCard.cpp
    src/ui/CopyChip.h
    src/ui/CopyChip.cpp
    src/ui/FormulaCard.h
    src/ui/FormulaCard.cpp
    src/ui/ConditionDialog.h
    src/ui/ConditionDialog.cpp
    src/ui/VariablePanel.h
    src/ui/VariablePanel.cpp
    src/ui/SerialDelegate.h
    src/ui/SerialDelegate.cpp
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
    src/tools/SnapEngine.h
    src/tools/SnapEngine.cpp
    src/tools/LinePropertyDialog.h
    src/tools/LinePropertyDialog.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets)

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
    src/parametric/ExpressionEvaluator.h
    src/parametric/ExpressionEvaluator.cpp
    src/parametric/ConditionEngine.h
    src/parametric/ConditionEngine.cpp
    src/parametric/ParamPoint.h
    src/parametric/Segment.h
    src/parametric/Attachment.h
    src/parametric/Condition.h
    src/geometry/Vec2.h
    src/geometry/Units.h
)
target_include_directories(test_resolver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_resolver PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_resolver COMMAND test_resolver)

cmake_minimum_required(VERSION 3.24)

project(ZimaCadCpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ZIMA_BUILD_TESTS "Build the C++ contract tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Svg)
find_package(OpenCASCADE REQUIRED)
set(ZIMA_OCCT_VERSION
    "${OpenCASCADE_MAJOR_VERSION}.${OpenCASCADE_MINOR_VERSION}.${OpenCASCADE_MAINTENANCE_VERSION}")
if(ZIMA_OCCT_VERSION VERSION_LESS 7.9)
    message(FATAL_ERROR
        "ZIMA-CAD requires OpenCASCADE 7.9 or newer; found ${ZIMA_OCCT_VERSION}")
endif()
find_package(nlohmann_json 3.11 REQUIRED)

add_library(zima_kernel_api INTERFACE)
target_include_directories(zima_kernel_api INTERFACE
    modules/kernel_api/include)

add_library(zima_sketcher
    modules/sketcher/src/sketch.cpp
    modules/sketcher/src/sketch_trim.cpp)
target_include_directories(zima_sketcher PUBLIC modules/sketcher/include)
target_link_libraries(zima_sketcher
    PUBLIC zima_kernel_api
    PRIVATE nlohmann_json::nlohmann_json)

add_library(zima_document_core
    modules/document_core/src/part_document.cpp
    modules/document_core/src/relations.cpp
    modules/document_core/src/document_session.cpp
    modules/document_core/src/viewer_packet_json.cpp)
target_include_directories(zima_document_core PUBLIC
    modules/document_core/include)
target_link_libraries(zima_document_core
    PUBLIC zima_kernel_api zima_sketcher
    PRIVATE nlohmann_json::nlohmann_json)

add_library(zima_assembly
    modules/assembly/src/assembly_document.cpp
    modules/assembly/src/assembly_session.cpp)
target_include_directories(zima_assembly PUBLIC modules/assembly/include)
target_link_libraries(zima_assembly
    PUBLIC zima_kernel_api zima_document_core
    PRIVATE nlohmann_json::nlohmann_json)

add_library(zima_drawing modules/drawing/src/drawing_document.cpp)
target_include_directories(zima_drawing PUBLIC modules/drawing/include)
target_link_libraries(zima_drawing
    PUBLIC zima_kernel_api zima_document_core
    PRIVATE nlohmann_json::nlohmann_json)

add_library(zima_workspace modules/workspace/src/workspace.cpp)
target_include_directories(zima_workspace PUBLIC modules/workspace/include)
target_link_libraries(zima_workspace PUBLIC zima_document_core zima_assembly zima_drawing)

add_library(zima_kernel_occt
    modules/kernel_occt/src/occt_kernel.cpp)
target_include_directories(zima_kernel_occt PUBLIC
    modules/kernel_occt/include)
target_link_libraries(zima_kernel_occt
    PUBLIC zima_kernel_api
    PRIVATE TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo
            TKTopAlgo TKPrim TKBO TKBool TKMesh)
target_link_libraries(zima_kernel_occt PRIVATE TKOffset)
target_link_libraries(zima_kernel_occt PRIVATE TKFillet)
target_link_libraries(zima_kernel_occt PRIVATE
    TKDESTEP TKDESTL TKXSBase TKXCAF TKCAF TKLCAF TKCDF)

add_library(zima_viewer
    modules/viewer/src/mesh_view.cpp
    modules/viewer/src/picking.cpp)
target_include_directories(zima_viewer PUBLIC modules/viewer/include)
target_link_libraries(zima_viewer PUBLIC zima_kernel_api
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)

add_library(zima_ui modules/ui/src/properties_subwindow.cpp)
target_include_directories(zima_ui PUBLIC modules/ui/include)
target_link_libraries(zima_ui PUBLIC Qt6::Widgets)

add_library(zima_interchange modules/interchange/src/interchange.cpp
    modules/interchange/src/dxf.cpp modules/interchange/src/planar_face.cpp
    modules/interchange/src/step.cpp)
target_include_directories(zima_interchange PUBLIC modules/interchange/include)
target_link_libraries(zima_interchange PUBLIC zima_sketcher
    PRIVATE TKDESTEP TKXSBase TKXCAF TKCAF TKLCAF TKCDF TKBRep TKGeomBase)

add_executable(zima-cad-part-harness
    app/main.cpp app/main_window.cpp app/file_dialog.cpp
    app/primitive_properties_dialog.cpp)
target_include_directories(zima-cad-part-harness PRIVATE app)
target_link_libraries(zima-cad-part-harness PRIVATE
    zima_document_core zima_kernel_occt zima_viewer zima_ui
    Qt6::Core Qt6::Gui Qt6::Widgets)

add_executable(zima-cad-cpp
    app/main_workspace.cpp app/assembly_workspace_window.cpp
    app/application_settings.cpp app/global_settings_dialog.cpp
    app/document_tools_dialogs.cpp
    app/drawing_window.cpp app/file_dialog.cpp app/resource_icon.cpp
    app/zima_resources.qrc
    app/component_properties_dialog.cpp app/primitive_properties_dialog.cpp
    app/construction_properties_dialog.cpp
    app/mate_properties_dialog.cpp app/sketch_properties_dialog.cpp
    app/sketch_bspline_properties_dialog.cpp
    app/sketch_text_properties_dialog.cpp
    app/sketch_dimension_properties_dialog.cpp)
target_include_directories(zima-cad-cpp PRIVATE app)
target_link_libraries(zima-cad-cpp PRIVATE
    zima_workspace zima_assembly zima_document_core zima_drawing zima_kernel_occt zima_viewer zima_ui
    zima_interchange
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)
set_target_properties(zima-cad-cpp PROPERTIES AUTORCC ON)

add_executable(zima-cad-drawing-harness
    app/main_drawing.cpp app/drawing_window.cpp app/file_dialog.cpp)
target_include_directories(zima-cad-drawing-harness PRIVATE app)
target_link_libraries(zima-cad-drawing-harness PRIVATE
    zima_workspace zima_drawing zima_document_core zima_assembly zima_ui
    Qt6::Core Qt6::Gui Qt6::Widgets)

if(ZIMA_BUILD_TESTS)
    enable_testing()
    add_executable(zima_cpp_contract_tests tests/contract_tests.cpp)
    target_link_libraries(zima_cpp_contract_tests PRIVATE
        zima_document_core zima_kernel_occt TKPrim TKDESTEP)
    add_test(NAME zima_cpp_contract_tests COMMAND zima_cpp_contract_tests)
    add_executable(zima_cpp_ui_contract_tests
        tests/ui_contract_tests.cpp app/primitive_properties_dialog.cpp
        app/construction_properties_dialog.cpp
        app/component_properties_dialog.cpp app/mate_properties_dialog.cpp
        app/file_dialog.cpp app/application_settings.cpp
        app/document_tools_dialogs.cpp)
    target_sources(zima_cpp_ui_contract_tests PRIVATE app/sketch_properties_dialog.cpp)
    target_sources(zima_cpp_ui_contract_tests PRIVATE app/sketch_dimension_properties_dialog.cpp)
    target_sources(zima_cpp_ui_contract_tests PRIVATE app/sketch_text_properties_dialog.cpp)
    target_include_directories(zima_cpp_ui_contract_tests PRIVATE app)
    target_link_libraries(zima_cpp_ui_contract_tests PRIVATE
        zima_document_core zima_assembly zima_ui Qt6::Widgets)
    add_test(NAME zima_cpp_ui_contract_tests COMMAND zima_cpp_ui_contract_tests)
    set_tests_properties(zima_cpp_ui_contract_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(zima_cpp_viewer_contract_tests tests/viewer_contract_tests.cpp)
    target_link_libraries(zima_cpp_viewer_contract_tests PRIVATE zima_viewer)
    add_test(NAME zima_cpp_viewer_contract_tests COMMAND zima_cpp_viewer_contract_tests)
    add_executable(zima_cpp_assembly_contract_tests tests/assembly_contract_tests.cpp)
    target_link_libraries(zima_cpp_assembly_contract_tests PRIVATE
        zima_assembly zima_kernel_occt zima_viewer)
    add_test(NAME zima_cpp_assembly_contract_tests COMMAND zima_cpp_assembly_contract_tests)
    add_executable(zima_cpp_workspace_contract_tests tests/workspace_contract_tests.cpp)
    target_link_libraries(zima_cpp_workspace_contract_tests PRIVATE
        zima_workspace zima_kernel_occt)
    add_test(NAME zima_cpp_workspace_contract_tests COMMAND zima_cpp_workspace_contract_tests)
    add_executable(zima_cpp_sketcher_contract_tests tests/sketcher_contract_tests.cpp)
    target_link_libraries(zima_cpp_sketcher_contract_tests PRIVATE zima_sketcher zima_viewer)
    add_test(NAME zima_cpp_sketcher_contract_tests COMMAND zima_cpp_sketcher_contract_tests)
    add_executable(zima_cpp_interchange_contract_tests tests/interchange_contract_tests.cpp)
    target_link_libraries(zima_cpp_interchange_contract_tests PRIVATE zima_interchange)
    add_test(NAME zima_cpp_interchange_contract_tests COMMAND zima_cpp_interchange_contract_tests)
    add_test(NAME zima_cpp_workspace_startup_contract
        COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_workspace_startup_contract PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(zima_cpp_drawing_contract_tests tests/drawing_contract_tests.cpp)
    target_link_libraries(zima_cpp_drawing_contract_tests PRIVATE zima_drawing)
    add_test(NAME zima_cpp_drawing_contract_tests COMMAND zima_cpp_drawing_contract_tests)
    set_tests_properties(zima_cpp_drawing_contract_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
endif()

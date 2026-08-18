cmake_minimum_required(VERSION 3.24)

project(ZimaCadCpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ZIMA_BUILD_TESTS "Build the C++ contract tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(OpenCASCADE 7.9 REQUIRED COMPONENTS
    FoundationClasses ModelingData ModelingAlgorithms)
find_package(nlohmann_json 3.11 REQUIRED)

add_library(zima_kernel_api INTERFACE)
target_include_directories(zima_kernel_api INTERFACE
    modules/kernel_api/include)

add_library(zima_document_core
    modules/document_core/src/part_document.cpp
    modules/document_core/src/document_session.cpp
    modules/document_core/src/viewer_packet_json.cpp)
target_include_directories(zima_document_core PUBLIC
    modules/document_core/include)
target_link_libraries(zima_document_core
    PUBLIC zima_kernel_api
    PRIVATE nlohmann_json::nlohmann_json)

add_library(zima_assembly
    modules/assembly/src/assembly_document.cpp
    modules/assembly/src/assembly_session.cpp)
target_include_directories(zima_assembly PUBLIC modules/assembly/include)
target_link_libraries(zima_assembly
    PUBLIC zima_kernel_api zima_document_core
    PRIVATE nlohmann_json::nlohmann_json)

add_library(zima_workspace modules/workspace/src/workspace.cpp)
target_include_directories(zima_workspace PUBLIC modules/workspace/include)
target_link_libraries(zima_workspace PUBLIC zima_document_core zima_assembly)

add_library(zima_kernel_occt
    modules/kernel_occt/src/occt_kernel.cpp)
target_include_directories(zima_kernel_occt PUBLIC
    modules/kernel_occt/include)
target_link_libraries(zima_kernel_occt
    PUBLIC zima_kernel_api
    PRIVATE TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo
            TKTopAlgo TKPrim TKBO TKBool TKMesh)

add_library(zima_viewer
    modules/viewer/src/mesh_view.cpp
    modules/viewer/src/picking.cpp)
target_include_directories(zima_viewer PUBLIC modules/viewer/include)
target_link_libraries(zima_viewer PUBLIC zima_kernel_api
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)

add_library(zima_ui modules/ui/src/properties_subwindow.cpp)
target_include_directories(zima_ui PUBLIC modules/ui/include)
target_link_libraries(zima_ui PUBLIC Qt6::Widgets)

add_executable(zima-cad-cpp
    app/main.cpp app/main_window.cpp app/primitive_properties_dialog.cpp
    app/component_properties_dialog.cpp)
target_include_directories(zima-cad-cpp PRIVATE app)
target_link_libraries(zima-cad-cpp PRIVATE
    zima_document_core zima_kernel_occt zima_viewer zima_ui
    Qt6::Core Qt6::Gui Qt6::Widgets)

add_executable(zima-cad-workspace-cpp
    app/main_workspace.cpp app/assembly_workspace_window.cpp
    app/component_properties_dialog.cpp app/primitive_properties_dialog.cpp)
target_include_directories(zima-cad-workspace-cpp PRIVATE app)
target_link_libraries(zima-cad-workspace-cpp PRIVATE
    zima_workspace zima_assembly zima_document_core zima_kernel_occt zima_viewer zima_ui
    Qt6::Core Qt6::Gui Qt6::Widgets)

if(ZIMA_BUILD_TESTS)
    enable_testing()
    add_executable(zima_cpp_contract_tests tests/contract_tests.cpp)
    target_link_libraries(zima_cpp_contract_tests PRIVATE
        zima_document_core zima_kernel_occt)
    add_test(NAME zima_cpp_contract_tests COMMAND zima_cpp_contract_tests)
    add_executable(zima_cpp_ui_contract_tests
        tests/ui_contract_tests.cpp app/primitive_properties_dialog.cpp
        app/component_properties_dialog.cpp)
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
    target_link_libraries(zima_cpp_workspace_contract_tests PRIVATE zima_workspace)
    add_test(NAME zima_cpp_workspace_contract_tests COMMAND zima_cpp_workspace_contract_tests)
endif()

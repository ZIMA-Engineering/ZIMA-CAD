cmake_minimum_required(VERSION 3.24)

project(ZimaCadCpp VERSION 0.1.0 LANGUAGES CXX)

file(READ "${CMAKE_SOURCE_DIR}/../RELEASE_DATE" ZIMA_RELEASE_DATE)
string(STRIP "${ZIMA_RELEASE_DATE}" ZIMA_RELEASE_DATE)
if(NOT ZIMA_RELEASE_DATE MATCHES "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$")
    message(FATAL_ERROR "RELEASE_DATE must use the YYYY-MM-DD format")
endif()

include(GNUInstallDirs)

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
target_link_libraries(zima_workspace PUBLIC
    zima_document_core zima_assembly zima_drawing zima_kernel_occt)

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

add_library(zima_ui modules/ui/src/properties_subwindow.cpp
    modules/ui/src/reference_cell.cpp
    modules/ui/src/container_placement_section.cpp)
target_include_directories(zima_ui PUBLIC modules/ui/include)
target_link_libraries(zima_ui PUBLIC Qt6::Widgets zima_document_core)

add_library(zima_interchange modules/interchange/src/interchange.cpp
    modules/interchange/src/dxf.cpp modules/interchange/src/planar_face.cpp
    modules/interchange/src/step.cpp)
target_include_directories(zima_interchange PUBLIC modules/interchange/include)
target_link_libraries(zima_interchange PUBLIC zima_sketcher
    PRIVATE TKDESTEP TKXSBase TKXCAF TKCAF TKLCAF TKCDF TKBRep TKGeomBase)

add_executable(zima-cad-part-harness
    app/main.cpp app/main_window.cpp app/file_dialog.cpp
    app/resource_icon.cpp app/zima_resources.qrc
    app/primitive_properties_dialog.cpp)
target_include_directories(zima-cad-part-harness PRIVATE app)
target_link_libraries(zima-cad-part-harness PRIVATE
    zima_document_core zima_kernel_occt zima_viewer zima_ui
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)
set_target_properties(zima-cad-part-harness PROPERTIES AUTORCC ON)

add_executable(zima-cad-cpp
    app/main_workspace.cpp app/assembly_workspace_window.cpp app/shaft_thread_command.cpp
    app/application_settings.cpp app/global_settings_dialog.cpp
    app/document_tools_dialogs.cpp
    app/drawing_window.cpp app/file_dialog.cpp app/resource_icon.cpp
    app/zima_resources.qrc
    app/component_properties_dialog.cpp app/primitive_properties_dialog.cpp
    app/construction_properties_dialog.cpp
    app/orientation_dialog.cpp
    app/sketch_properties_dialog.cpp
    app/sketch_bspline_properties_dialog.cpp
    app/sketch_text_properties_dialog.cpp
    app/sketch_dimension_properties_dialog.cpp)
target_include_directories(zima-cad-cpp PRIVATE app)
target_compile_definitions(zima-cad-cpp PRIVATE
    ZIMA_RELEASE_DATE="${ZIMA_RELEASE_DATE}")
target_link_libraries(zima-cad-cpp PRIVATE
    zima_workspace zima_assembly zima_document_core zima_drawing zima_kernel_occt zima_viewer zima_ui
    zima_interchange nlohmann_json::nlohmann_json
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)
set_target_properties(zima-cad-cpp PROPERTIES AUTORCC ON)
if(MSVC)
    # assembly_workspace_window.cpp intentionally owns the integrated UI
    # contracts. Background file-operation lambdas push its section count
    # past COFF's legacy limit in optimized Windows builds.
    target_compile_options(zima-cad-cpp PRIVATE /bigobj)
endif()
if(WIN32)
    target_sources(zima-cad-cpp PRIVATE app/windows/zima-cad.rc)
endif()
if(WIN32 AND TARGET Qt6::QWindowsIntegrationPlugin)
    add_custom_command(TARGET zima-cad-cpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:zima-cad-cpp>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
            "$<TARGET_FILE_DIR:zima-cad-cpp>/platforms/")
endif()

if(WIN32 AND TARGET Qt6::QModernWindowsStylePlugin)
    add_custom_command(TARGET zima-cad-cpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:zima-cad-cpp>/styles"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QModernWindowsStylePlugin>"
            "$<TARGET_FILE_DIR:zima-cad-cpp>/styles/")
endif()
install(TARGETS zima-cad-cpp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    install(FILES app/linux/zima-cad.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ../resources/branding/app-icon.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME zima-cad.svg)
endif()

add_executable(zima-cad-drawing-harness
    app/main_drawing.cpp app/drawing_window.cpp app/file_dialog.cpp
    app/resource_icon.cpp app/zima_resources.qrc)
target_include_directories(zima-cad-drawing-harness PRIVATE app)
target_link_libraries(zima-cad-drawing-harness PRIVATE
    zima_workspace zima_drawing zima_document_core zima_assembly zima_ui
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)
set_target_properties(zima-cad-drawing-harness PROPERTIES AUTORCC ON)

add_executable(zima_cpp_performance_benchmark
    tests/performance_benchmark.cpp)
target_link_libraries(zima_cpp_performance_benchmark PRIVATE
    zima_workspace zima_assembly zima_document_core zima_kernel_occt zima_viewer
    zima_sketcher)

if(ZIMA_BUILD_TESTS)
    enable_testing()
    if(WIN32)
        set(ZIMA_GUI_TEST_ENVIRONMENT
            "QT_QPA_PLATFORM=windows;QT_STYLE_OVERRIDE=Fusion")
    else()
        set(ZIMA_GUI_TEST_ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    add_executable(zima_cpp_contract_tests tests/contract_tests.cpp)
    target_link_libraries(zima_cpp_contract_tests PRIVATE
        zima_document_core zima_kernel_occt TKPrim TKDESTEP)
    add_test(NAME zima_cpp_contract_tests COMMAND zima_cpp_contract_tests)
    set_tests_properties(zima_cpp_contract_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_sweep2d_contract_tests tests/sweep2d_contract_tests.cpp)
    target_link_libraries(zima_cpp_sweep2d_contract_tests PRIVATE zima_document_core zima_kernel_occt)
    add_test(NAME zima_cpp_sweep2d_contract_tests COMMAND zima_cpp_sweep2d_contract_tests)
    add_executable(zima_cpp_helical_sweep_contract_tests tests/helical_sweep_contract_tests.cpp)
    target_link_libraries(zima_cpp_helical_sweep_contract_tests PRIVATE zima_document_core zima_kernel_occt)
    add_test(NAME zima_cpp_helical_sweep_contract_tests COMMAND zima_cpp_helical_sweep_contract_tests)
    add_executable(zima_cpp_shaft_thread_contract_tests tests/shaft_thread_contract_tests.cpp)
    target_link_libraries(zima_cpp_shaft_thread_contract_tests PRIVATE zima_document_core zima_kernel_occt)
    add_test(NAME zima_cpp_shaft_thread_contract_tests COMMAND zima_cpp_shaft_thread_contract_tests)
    add_executable(zima_cpp_hole_contract_tests tests/hole_contract_tests.cpp)
    target_link_libraries(zima_cpp_hole_contract_tests PRIVATE
        zima_document_core zima_kernel_occt)
    add_test(NAME zima_cpp_hole_contract_tests
        COMMAND zima_cpp_hole_contract_tests)
    add_executable(zima_cpp_ui_contract_tests
        tests/ui_contract_tests.cpp app/primitive_properties_dialog.cpp
        app/construction_properties_dialog.cpp
        app/orientation_dialog.cpp
        app/component_properties_dialog.cpp
        app/file_dialog.cpp app/resource_icon.cpp app/zima_resources.qrc
        app/application_settings.cpp
        app/document_tools_dialogs.cpp)
    target_sources(zima_cpp_ui_contract_tests PRIVATE app/sketch_properties_dialog.cpp)
    target_sources(zima_cpp_ui_contract_tests PRIVATE app/sketch_dimension_properties_dialog.cpp)
    target_sources(zima_cpp_ui_contract_tests PRIVATE app/sketch_text_properties_dialog.cpp)
    target_include_directories(zima_cpp_ui_contract_tests PRIVATE app)
    target_link_libraries(zima_cpp_ui_contract_tests PRIVATE
        zima_document_core zima_assembly zima_ui zima_viewer
        Qt6::Widgets Qt6::Svg nlohmann_json::nlohmann_json)
    set_target_properties(zima_cpp_ui_contract_tests PROPERTIES AUTORCC ON)
    add_test(NAME zima_cpp_ui_contract_tests COMMAND zima_cpp_ui_contract_tests)
    set_tests_properties(zima_cpp_ui_contract_tests PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT}")
    add_executable(zima_cpp_viewer_contract_tests tests/viewer_contract_tests.cpp)
    target_link_libraries(zima_cpp_viewer_contract_tests PRIVATE zima_viewer)
    add_test(NAME zima_cpp_viewer_contract_tests COMMAND zima_cpp_viewer_contract_tests)
    add_executable(zima_cpp_assembly_contract_tests tests/assembly_contract_tests.cpp)
    target_link_libraries(zima_cpp_assembly_contract_tests PRIVATE
        zima_assembly zima_kernel_occt zima_viewer)
    add_test(NAME zima_cpp_assembly_contract_tests COMMAND zima_cpp_assembly_contract_tests)
    set_tests_properties(zima_cpp_assembly_contract_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_workspace_contract_tests tests/workspace_contract_tests.cpp)
    target_link_libraries(zima_cpp_workspace_contract_tests PRIVATE
        zima_workspace zima_kernel_occt)
    add_test(NAME zima_cpp_workspace_contract_tests COMMAND zima_cpp_workspace_contract_tests)
    add_executable(zima_cpp_sketcher_contract_tests tests/sketcher_contract_tests.cpp)
    target_link_libraries(zima_cpp_sketcher_contract_tests PRIVATE
        zima_sketcher zima_viewer zima_document_core)
    add_test(NAME zima_cpp_sketcher_contract_tests COMMAND zima_cpp_sketcher_contract_tests)
    add_executable(zima_cpp_interchange_contract_tests tests/interchange_contract_tests.cpp)
    target_link_libraries(zima_cpp_interchange_contract_tests PRIVATE zima_interchange)
    add_test(NAME zima_cpp_interchange_contract_tests COMMAND zima_cpp_interchange_contract_tests)
    add_test(NAME zima_cpp_workspace_startup_contract
        COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_workspace_startup_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_drawing_contract_tests tests/drawing_contract_tests.cpp)
    target_link_libraries(zima_cpp_drawing_contract_tests PRIVATE zima_drawing)
    add_test(NAME zima_cpp_drawing_contract_tests COMMAND zima_cpp_drawing_contract_tests)
    set_tests_properties(zima_cpp_drawing_contract_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    set_tests_properties(zima_cpp_drawing_contract_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_cross_language_persistence_emitter
        tests/cross_language_persistence_emitter.cpp)
    target_link_libraries(zima_cpp_cross_language_persistence_emitter PRIVATE
        zima_document_core zima_assembly zima_drawing)
endif()

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
find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)

add_library(zima_commands modules/commands/src/dispatcher.cpp)
target_include_directories(zima_commands PUBLIC modules/commands/include)
target_link_libraries(zima_commands PUBLIC nlohmann_json::nlohmann_json)

add_library(zima_kernel_api INTERFACE)
target_include_directories(zima_kernel_api INTERFACE
    modules/kernel_api/include)

# Embed the same licensed font as the Qt resources; headless modeling must
# not depend on installed system fonts or on a GUI process.
set(ZIMA_SKETCH_FONT "${CMAKE_SOURCE_DIR}/../config/fonts/osifont-lgpl3fe.ttf")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ZIMA_SKETCH_FONT}")
file(READ "${ZIMA_SKETCH_FONT}" ZIMA_SKETCH_FONT_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," ZIMA_SKETCH_FONT_BYTES "${ZIMA_SKETCH_FONT_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sketch_font_data.cpp"
    "#include <span>\nnamespace zima::sketcher { std::span<const unsigned char> bundled_sketch_font() { static const unsigned char bytes[]={${ZIMA_SKETCH_FONT_BYTES}}; return bytes; } }\n")

add_library(zima_sketcher
    modules/sketcher/src/sketch.cpp
    modules/sketcher/src/text_geometry.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/sketch_font_data.cpp"
    modules/sketcher/src/template_image.cpp
    modules/sketcher/src/rectilinear_template_solver.cpp
    modules/sketcher/src/sketch_trim.cpp
    modules/sketcher/src/curve_geometry.cpp
    modules/sketcher/src/sketch_offset.cpp)
target_include_directories(zima_sketcher PUBLIC modules/sketcher/include)
target_compile_options(zima_sketcher PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(zima_sketcher
    PUBLIC zima_kernel_api
    PRIVATE nlohmann_json::nlohmann_json Freetype::Freetype harfbuzz::harfbuzz)

add_library(zima_document_core
    modules/document_core/src/part_document.cpp
    modules/document_core/src/appearance.cpp
    modules/document_core/src/body_history.cpp
    modules/document_core/src/section.cpp
    modules/document_core/src/measurement_record.cpp
    modules/document_core/src/relations.cpp
    modules/document_core/src/metadata.cpp
    modules/document_core/src/engineering_metadata.cpp
    modules/document_core/src/material_library.cpp
    modules/document_core/src/document_session.cpp
    modules/document_core/src/dimension_identifiers.cpp
    modules/document_core/src/viewer_packet_json.cpp
    modules/document_core/src/cache_storage.cpp)
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

add_library(zima_drawing modules/drawing/src/drawing_document.cpp modules/drawing/src/model_annotations.cpp
    modules/drawing/src/measurement_dimension.cpp
    modules/drawing/src/drawing_template.cpp modules/drawing/src/drawing_section_trace.cpp)
target_include_directories(zima_drawing PUBLIC modules/drawing/include)
target_link_libraries(zima_drawing
    PUBLIC zima_kernel_api zima_document_core
    PRIVATE nlohmann_json::nlohmann_json)

add_library(zima_workspace modules/workspace/src/workspace.cpp
    modules/workspace/src/document_copy.cpp
    modules/workspace/src/document_operations.cpp
    modules/workspace/src/body_operations.cpp
    modules/workspace/src/history_operations.cpp
    modules/workspace/src/reference_sources.cpp
    modules/workspace/src/export_operations.cpp
    modules/workspace/src/import_operations.cpp
    modules/workspace/src/assembly_import_operations.cpp
    modules/workspace/src/metadata_operations.cpp
    modules/workspace/src/engineering_metadata_operations.cpp
    modules/workspace/src/component_operations.cpp
    modules/workspace/src/component_source_operations.cpp
    modules/workspace/src/drawing_operations.cpp
    modules/workspace/src/drawing_state.cpp
    modules/workspace/src/drawing_sources.cpp
    modules/workspace/src/drawing_annotation_sources.cpp
    modules/workspace/src/drawing_title_operations.cpp
    modules/workspace/src/drawing_dimension_operations.cpp
    modules/workspace/src/drawing_annotation_operations.cpp
    modules/workspace/src/drawing_view_operations.cpp
    modules/workspace/src/drawing_projection.cpp
    modules/workspace/src/sketch_operations.cpp
    modules/workspace/src/sketch_reference_operations.cpp
    modules/workspace/src/native_documents.cpp
    modules/workspace/src/part_references.cpp
    modules/workspace/src/model_calculation.cpp
    modules/workspace/src/placement_edit.cpp
    modules/workspace/src/primitive_operations.cpp)
target_include_directories(zima_workspace PUBLIC modules/workspace/include)
target_link_libraries(zima_workspace PUBLIC
    zima_document_core zima_assembly zima_drawing zima_kernel_occt zima_interchange
    PRIVATE nlohmann_json::nlohmann_json)

add_library(zima_command_host
    modules/command_host/src/host.cpp
    modules/command_host/src/model_tree.cpp
    modules/command_host/src/primitive_commands.cpp
    modules/command_host/src/document_commands.cpp
    modules/command_host/src/body_commands.cpp
    modules/command_host/src/history_commands.cpp
    modules/command_host/src/reference_commands.cpp
    modules/command_host/src/construction_commands.cpp
    modules/command_host/src/placement_commands.cpp
    modules/command_host/src/sketch_commands.cpp
    modules/command_host/src/sketch_curve_commands.cpp
    modules/command_host/src/sketch_relation_commands.cpp
    modules/command_host/src/sketch_dimension_commands.cpp
    modules/command_host/src/sketch_reference_commands.cpp
    modules/command_host/src/sketch_text_commands.cpp
    modules/command_host/src/sketch_spline_commands.cpp
    modules/command_host/src/import_commands.cpp
    modules/command_host/src/export_commands.cpp
    modules/command_host/src/metadata_commands.cpp
    modules/command_host/src/engineering_metadata_commands.cpp
    modules/command_host/src/component_commands.cpp
    modules/command_host/src/drawing_title_commands.cpp
    modules/command_host/src/drawing_dimension_commands.cpp
    modules/command_host/src/drawing_annotation_commands.cpp
    modules/command_host/src/drawing_commands.cpp)
target_include_directories(zima_command_host PUBLIC modules/command_host/include)
target_link_libraries(zima_command_host PUBLIC zima_commands zima_workspace)
# Commands carry UTF-8 paths and localized text even without Qt's build flags.
target_compile_options(zima_command_host PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

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
    TKDESTEP TKDEIGES TKDESTL TKXSBase TKXCAF TKCAF TKLCAF TKCDF)

add_library(zima_viewer
    modules/viewer/src/mesh_view.cpp
    modules/viewer/src/measurement.cpp
    modules/viewer/src/picking.cpp)
target_include_directories(zima_viewer PUBLIC modules/viewer/include)
target_link_libraries(zima_viewer PUBLIC zima_kernel_api
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg)

add_library(zima_drawing_render modules/drawing_render/src/sheet_renderer.cpp modules/drawing_render/src/pdf_export.cpp modules/drawing_render/src/dxf_export.cpp modules/drawing_render/src/image_export.cpp)
target_include_directories(zima_drawing_render PUBLIC modules/drawing_render/include PRIVATE app modules/viewer/include)
target_link_libraries(zima_drawing_render PUBLIC zima_drawing zima_workspace Qt6::Gui Qt6::Svg)
target_link_libraries(zima_command_host PUBLIC zima_drawing_render)

add_library(zima_ui modules/ui/src/properties_subwindow.cpp
    modules/ui/src/reference_cell.cpp
    modules/ui/src/container_placement_section.cpp)
target_include_directories(zima_ui PUBLIC modules/ui/include)
target_link_libraries(zima_ui PUBLIC Qt6::Widgets zima_document_core)

add_library(zima_interchange modules/interchange/src/interchange.cpp
    modules/interchange/src/dxf.cpp
    modules/interchange/src/dxf_export.cpp modules/interchange/src/planar_face.cpp
    modules/interchange/src/step.cpp modules/interchange/src/step_model.cpp
    modules/interchange/src/model_import.cpp)
target_include_directories(zima_interchange PUBLIC modules/interchange/include)
target_compile_options(zima_interchange PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(zima_interchange PUBLIC zima_sketcher zima_document_core zima_assembly zima_kernel_occt
    PRIVATE TKDESTEP TKXSBase TKXCAF TKCAF TKLCAF TKCDF TKBRep TKGeomBase)

add_executable(zima-cad-part-harness
    app/main.cpp app/main_window.cpp app/file_dialog.cpp
    app/application_settings.cpp
    app/resource_icon.cpp app/zima_resources.qrc
    app/primitive_properties_dialog.cpp)
target_include_directories(zima-cad-part-harness PRIVATE app)
target_link_libraries(zima-cad-part-harness PRIVATE
    zima_document_core zima_kernel_occt zima_viewer zima_ui
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)
set_target_properties(zima-cad-part-harness PROPERTIES AUTORCC ON)

include(app/workspace/sources.cmake)

add_executable(zima-cad-cpp
    ${ZIMA_WORKSPACE_SOURCES}
    app/command_console.cpp app/console_ui_verification.cpp
    app/main_workspace.cpp app/appearance_dialog.cpp app/appearance_command.cpp app/section_command.cpp app/section_ui_verification.cpp app/assembly_workspace_window.cpp app/template_command.cpp app/numeric_value_lock_command.cpp app/shaft_thread_command.cpp app/derived_copy_command.cpp
    app/application_settings.cpp app/global_settings_dialog.cpp app/measurement_command.cpp app/measurement_ui_verification.cpp
    app/document_tools_dialogs.cpp
    app/drawing_window.cpp app/show_erase_dialog.cpp app/file_dialog.cpp app/resource_icon.cpp
    app/zima_resources.qrc
    app/component_properties_dialog.cpp app/primitive_properties_dialog.cpp
    app/construction_properties_dialog.cpp
    app/orientation_dialog.cpp
    app/sketch_properties_dialog.cpp
    app/import_options_dialog.cpp
    app/sketch_bspline_properties_dialog.cpp
    app/sketch_text_properties_dialog.cpp
    app/sketch_dimension_properties_dialog.cpp)
target_include_directories(zima-cad-cpp PRIVATE app)
target_compile_definitions(zima-cad-cpp PRIVATE
    ZIMA_RELEASE_DATE="${ZIMA_RELEASE_DATE}")
target_link_libraries(zima-cad-cpp PRIVATE
    zima_command_host zima_commands zima_workspace zima_assembly zima_document_core zima_drawing zima_kernel_occt zima_viewer zima_ui
    zima_interchange nlohmann_json::nlohmann_json
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)
set_target_properties(zima-cad-cpp PROPERTIES AUTORCC ON)
if(MSVC)
    # Retain the large-object budget for lambda-heavy UI translation units.
    target_compile_options(zima-cad-cpp PRIVATE /bigobj)
endif()
if(WIN32)
    set_target_properties(zima-cad-cpp PROPERTIES WIN32_EXECUTABLE TRUE)
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
target_link_libraries(zima-cad-cpp PRIVATE zima_drawing_render)
install(TARGETS zima-cad-cpp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# A console-subsystem executable. Qt Gui runs offscreen for shared PDF painting; no Widgets are created.
add_executable(zima-cad-cli cli/main.cpp cli/runner.cpp cli/settings.cpp)
target_link_libraries(zima-cad-cli PRIVATE zima_command_host)
if(WIN32 AND TARGET Qt6::QOffscreenIntegrationPlugin)
    add_custom_command(TARGET zima-cad-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:zima-cad-cli>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>" "$<TARGET_FILE_DIR:zima-cad-cli>/platforms/")
endif()
install(TARGETS zima-cad-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(UNIX AND NOT APPLE)
    install(FILES app/linux/zima-cad.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ../resources/branding/app-icon.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME zima-cad.svg)
endif()

add_executable(zima-cad-drawing-harness
    app/main_drawing.cpp app/drawing_window.cpp app/show_erase_dialog.cpp app/file_dialog.cpp app/application_settings.cpp
    tests/drawing_ui_contract.cpp tests/show_erase_ui_contract.cpp tests/measurement_dimension_ui_contract.cpp
    app/resource_icon.cpp app/zima_resources.qrc)
if(WIN32 AND TARGET Qt6::QJpegPlugin)
    foreach(image_target zima-cad-cpp zima-cad-drawing-harness zima-cad-cli)
        add_custom_command(TARGET ${image_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DPLUGIN=$<TARGET_FILE:Qt6::QJpegPlugin>"
                "-DPLUGIN_NAME=$<TARGET_FILE_NAME:Qt6::QJpegPlugin>"
                "-DQT_RUNTIME_DIRECTORY=$<TARGET_FILE_DIR:Qt6::Core>"
                "-DOUTPUT_DIRECTORY=$<TARGET_FILE_DIR:${image_target}>"
                -P "${CMAKE_SOURCE_DIR}/../tools/deploy-qt-image-plugin.cmake")
    endforeach()
endif()
target_include_directories(zima-cad-drawing-harness PRIVATE app)
target_link_libraries(zima-cad-drawing-harness PRIVATE zima_viewer zima_drawing_render)
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
    add_executable(zima_cpp_instance_startup_contract_tests tests/instance_startup_contract_tests.cpp)
    target_include_directories(zima_cpp_instance_startup_contract_tests PRIVATE app)
    target_link_libraries(zima_cpp_instance_startup_contract_tests PRIVATE
        Qt6::Core zima_document_core zima_assembly zima_drawing)
    add_test(NAME zima_cpp_instance_startup_contract
        COMMAND zima_cpp_instance_startup_contract_tests $<TARGET_FILE:zima-cad-cpp>)
    set_tests_properties(zima_cpp_instance_startup_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." TIMEOUT 90)
    add_executable(zima_cpp_import_model_contract_tests tests/import_model_contract_tests.cpp)
    target_link_libraries(zima_cpp_import_model_contract_tests PRIVATE zima_interchange zima_workspace TKDEIGES TKPrim)
    target_compile_definitions(zima_cpp_import_model_contract_tests PRIVATE ZIMA_IMPORT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/import")
    add_test(NAME zima_cpp_import_model_contract_tests COMMAND zima_cpp_import_model_contract_tests)
    add_executable(zima_cpp_step_model_contract_tests tests/step_model_contract_tests.cpp)
    target_link_libraries(zima_cpp_step_model_contract_tests PRIVATE zima_interchange zima_kernel_occt TKPrim TKDESTEP TKXCAF TKCAF TKLCAF TKCDF)
    add_test(NAME zima_cpp_step_model_contract_tests COMMAND zima_cpp_step_model_contract_tests)
    add_executable(zima_cpp_section_contract_tests tests/section_contract_tests.cpp)
    target_link_libraries(zima_cpp_section_contract_tests PRIVATE zima_document_core zima_assembly zima_drawing zima_kernel_occt)
    add_test(NAME zima_cpp_section_contract_tests COMMAND zima_cpp_section_contract_tests)
    enable_testing()
    add_executable(zima_cpp_cache_storage_contract_tests tests/cache_storage_contract_tests.cpp)
    target_link_libraries(zima_cpp_cache_storage_contract_tests PRIVATE zima_document_core nlohmann_json::nlohmann_json)
    add_test(NAME zima_cpp_cache_storage_contract_tests COMMAND zima_cpp_cache_storage_contract_tests)

    if(WIN32)
        set(ZIMA_GUI_TEST_ENVIRONMENT
            "QT_QPA_PLATFORM=windows;QT_STYLE_OVERRIDE=Fusion")
    else()
        set(ZIMA_GUI_TEST_ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    add_executable(zima_cpp_measurement_contract_tests tests/measurement_contract_tests.cpp)
    target_link_libraries(zima_cpp_measurement_contract_tests PRIVATE zima_viewer zima_document_core zima_assembly zima_kernel_occt nlohmann_json::nlohmann_json)
    add_test(NAME zima_cpp_measurement_contract_tests COMMAND zima_cpp_measurement_contract_tests)
    add_test(NAME zima_cpp_sweep2d_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_sweep2d_ui_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_SWEEP2D_ONLY=1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." TIMEOUT 120)
    add_test(NAME zima_cpp_helical_sweep_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_helical_sweep_ui_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_HELICAL_SWEEP_ONLY=1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." TIMEOUT 120)
    add_test(NAME zima_cpp_profile_frame_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_profile_frame_ui_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_PROFILE_FRAMES_ONLY=1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." TIMEOUT 240 RUN_SERIAL TRUE)
    add_test(NAME zima_cpp_dimension_edits_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_dimension_edits_ui_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_DIMENSION_EDITS_ONLY=1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." TIMEOUT 300 RUN_SERIAL TRUE)
    add_test(NAME zima_cpp_measurement_inspector_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_measurement_inspector_ui_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_MEASUREMENT_INSPECTOR_ONLY=1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." TIMEOUT 90)
    add_executable(zima_cpp_appearance_contract_tests tests/appearance_contract_tests.cpp app/appearance_dialog.cpp)
    target_include_directories(zima_cpp_appearance_contract_tests PRIVATE app)
    target_link_libraries(zima_cpp_appearance_contract_tests PRIVATE zima_document_core zima_assembly zima_ui zima_viewer)
    add_test(NAME zima_cpp_appearance_contract_tests COMMAND zima_cpp_appearance_contract_tests)
    set_tests_properties(zima_cpp_appearance_contract_tests PROPERTIES ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT}")
    add_executable(zima_cpp_dimension_identifiers_contract_tests tests/dimension_identifiers_contract_tests.cpp)
    target_link_libraries(zima_cpp_dimension_identifiers_contract_tests PRIVATE zima_document_core zima_assembly zima_drawing zima_kernel_occt)
    add_test(NAME zima_cpp_dimension_identifiers_contract_tests COMMAND zima_cpp_dimension_identifiers_contract_tests)
    add_executable(zima_cpp_history_recovery_contract_tests tests/history_recovery_contract_tests.cpp)
    target_link_libraries(zima_cpp_history_recovery_contract_tests PRIVATE zima_kernel_occt zima_document_core nlohmann_json::nlohmann_json)
    add_test(NAME zima_cpp_history_recovery_contract_tests COMMAND zima_cpp_history_recovery_contract_tests)
    add_executable(zima_cpp_multibody_contract_tests tests/multibody_contract_tests.cpp)
    target_link_libraries(zima_cpp_multibody_contract_tests PRIVATE zima_document_core zima_kernel_occt nlohmann_json::nlohmann_json)
    add_test(NAME zima_cpp_multibody_contract_tests COMMAND zima_cpp_multibody_contract_tests)
    add_executable(zima_cpp_contract_tests tests/contract_tests.cpp)
    if(MSVC)
        # This combined test keeps ~0.9 MiB of fixtures in main's stack frame.
        # Leave room for OCCT numerical routines, as on Linux's 8 MiB stack.
        target_link_options(zima_cpp_contract_tests PRIVATE /STACK:8388608)
    endif()
    target_link_libraries(zima_cpp_contract_tests PRIVATE
        zima_document_core zima_kernel_occt TKPrim TKDESTEP)
    add_test(NAME zima_cpp_contract_tests COMMAND zima_cpp_contract_tests)
    set_tests_properties(zima_cpp_contract_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_derived_copy_contract_tests tests/derived_copy_contract_tests.cpp)
    target_link_libraries(zima_cpp_derived_copy_contract_tests PRIVATE zima_document_core zima_assembly zima_kernel_occt zima_workspace)
    add_test(NAME zima_cpp_derived_copy_contract_tests COMMAND zima_cpp_derived_copy_contract_tests)
    add_executable(zima_cpp_sweep2d_contract_tests tests/sweep2d_contract_tests.cpp)
    target_link_libraries(zima_cpp_sweep2d_contract_tests PRIVATE zima_document_core zima_kernel_occt)
    add_test(NAME zima_cpp_sweep2d_contract_tests COMMAND zima_cpp_sweep2d_contract_tests)
    add_executable(zima_cpp_curve3d_sweep_contract_tests tests/curve3d_sweep_contract_tests.cpp)
    target_link_libraries(zima_cpp_curve3d_sweep_contract_tests PRIVATE zima_document_core zima_kernel_occt nlohmann_json::nlohmann_json)
    add_test(NAME zima_cpp_curve3d_sweep_contract_tests COMMAND zima_cpp_curve3d_sweep_contract_tests)
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
        tests/ui_contract_tests.cpp tests/numeric_value_lock_contract.cpp tests/translation_contract.cpp
        app/primitive_properties_dialog.cpp
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
        zima_workspace zima_document_core zima_assembly zima_ui zima_viewer
        Qt6::Widgets Qt6::Svg nlohmann_json::nlohmann_json)
    set_target_properties(zima_cpp_ui_contract_tests PROPERTIES AUTORCC ON)
    add_test(NAME zima_cpp_ui_contract_tests COMMAND zima_cpp_ui_contract_tests)
    set_tests_properties(zima_cpp_ui_contract_tests PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT}")
    add_test(NAME zima_cpp_entry_tables_contract COMMAND zima_cpp_ui_contract_tests)
    set_tests_properties(zima_cpp_entry_tables_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_ENTRY_TABLES_ONLY=1")
    add_test(NAME zima_cpp_translations_contract COMMAND zima_cpp_ui_contract_tests)
    set_tests_properties(zima_cpp_translations_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_TRANSLATIONS_ONLY=1")
    add_test(NAME zima_cpp_numeric_value_locks_contract COMMAND zima_cpp_ui_contract_tests)
    set_tests_properties(zima_cpp_numeric_value_locks_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_VALUE_LOCKS_ONLY=1")
    add_test(NAME zima_cpp_numeric_fields_contract COMMAND zima_cpp_ui_contract_tests)
    set_tests_properties(zima_cpp_numeric_fields_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_NUMERIC_ONLY=1")
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
    add_executable(zima_cpp_model_calculation_tests tests/model_calculation_tests.cpp)
    target_link_libraries(zima_cpp_model_calculation_tests PRIVATE zima_workspace)
    add_test(NAME zima_cpp_model_calculation_tests COMMAND zima_cpp_model_calculation_tests)
    if(WIN32)
        add_test(NAME zima_cpp_numeric_value_locks_windows_contract COMMAND zima_cpp_ui_contract_tests)
        set_tests_properties(zima_cpp_numeric_value_locks_windows_contract PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_STYLE_OVERRIDE=windows;ZIMA_VERIFY_VALUE_LOCKS_ONLY=1")
    endif()
    add_executable(zima_cpp_native_documents_tests tests/native_documents_tests.cpp)
    target_link_libraries(zima_cpp_native_documents_tests PRIVATE zima_workspace)
    add_test(NAME zima_cpp_native_documents_tests COMMAND zima_cpp_native_documents_tests)
    set_tests_properties(zima_cpp_native_documents_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_document_operations_tests tests/document_operations_tests.cpp)
    target_link_libraries(zima_cpp_document_operations_tests PRIVATE zima_workspace)
    add_test(NAME zima_cpp_document_operations_tests COMMAND zima_cpp_document_operations_tests)
    add_executable(zima_cpp_cli_process_tests tests/cli_process_tests.cpp cli/runner.cpp cli/settings.cpp)
    target_link_libraries(zima_cpp_cli_process_tests PRIVATE zima_command_host Qt6::Core)
    add_dependencies(zima_cpp_cli_process_tests zima-cad-cli)
    add_test(NAME zima_cpp_cli_process_tests COMMAND zima_cpp_cli_process_tests "$<TARGET_FILE:zima-cad-cli>")
    set_tests_properties(zima_cpp_cli_process_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." TIMEOUT 120)
    add_executable(zima_cpp_drawing_image_command_tests tests/drawing_image_command_tests.cpp)
    target_link_libraries(zima_cpp_drawing_image_command_tests PRIVATE zima_command_host)
    add_dependencies(zima_cpp_drawing_image_command_tests zima-cad-cli)
    add_test(NAME zima_cpp_drawing_image_command_tests COMMAND zima_cpp_drawing_image_command_tests)
    set_tests_properties(zima_cpp_drawing_image_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(zima_cpp_drawing_dxf_command_tests tests/drawing_dxf_command_tests.cpp)
    target_link_libraries(zima_cpp_drawing_dxf_command_tests PRIVATE zima_command_host)
    add_dependencies(zima_cpp_drawing_dxf_command_tests zima-cad-cli)
    add_test(NAME zima_cpp_drawing_dxf_command_tests COMMAND zima_cpp_drawing_dxf_command_tests)
    set_tests_properties(zima_cpp_drawing_dxf_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(zima_cpp_drawing_pdf_command_tests tests/drawing_pdf_command_tests.cpp)
    target_link_libraries(zima_cpp_drawing_pdf_command_tests PRIVATE zima_command_host)
    add_dependencies(zima_cpp_drawing_pdf_command_tests zima-cad-cli)
    add_test(NAME zima_cpp_drawing_pdf_command_tests COMMAND zima_cpp_drawing_pdf_command_tests)
    set_tests_properties(zima_cpp_drawing_pdf_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(zima_cpp_drawing_title_command_tests tests/drawing_title_command_tests.cpp)
    target_link_libraries(zima_cpp_drawing_title_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_drawing_title_command_tests COMMAND zima_cpp_drawing_title_command_tests)
    set_tests_properties(zima_cpp_drawing_title_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_drawing_dimension_command_tests tests/drawing_dimension_command_tests.cpp)
    target_link_libraries(zima_cpp_drawing_dimension_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_drawing_dimension_command_tests COMMAND zima_cpp_drawing_dimension_command_tests)
    set_tests_properties(zima_cpp_drawing_dimension_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_drawing_annotation_command_tests tests/drawing_annotation_command_tests.cpp)
    target_link_libraries(zima_cpp_drawing_annotation_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_drawing_annotation_command_tests COMMAND zima_cpp_drawing_annotation_command_tests)
    set_tests_properties(zima_cpp_drawing_annotation_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_drawing_view_command_tests tests/drawing_view_command_tests.cpp)
    target_link_libraries(zima_cpp_drawing_view_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_drawing_view_command_tests COMMAND zima_cpp_drawing_view_command_tests)
    set_tests_properties(zima_cpp_drawing_view_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_drawing_command_tests tests/drawing_command_tests.cpp)
    target_link_libraries(zima_cpp_drawing_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_drawing_command_tests COMMAND zima_cpp_drawing_command_tests)
    set_tests_properties(zima_cpp_drawing_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_component_source_tests tests/component_source_tests.cpp)
    target_link_libraries(zima_cpp_component_source_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_component_source_tests COMMAND zima_cpp_component_source_tests)
    set_tests_properties(zima_cpp_component_source_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_component_command_tests tests/component_command_tests.cpp)
    target_link_libraries(zima_cpp_component_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_component_command_tests COMMAND zima_cpp_component_command_tests)
    set_tests_properties(zima_cpp_component_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_material_library_tests tests/material_library_tests.cpp)
    target_link_libraries(zima_cpp_material_library_tests PRIVATE zima_document_core)
    add_test(NAME zima_cpp_material_library_tests COMMAND zima_cpp_material_library_tests)
    set_tests_properties(zima_cpp_material_library_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_engineering_metadata_command_tests tests/engineering_metadata_command_tests.cpp)
    target_link_libraries(zima_cpp_engineering_metadata_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_engineering_metadata_command_tests COMMAND zima_cpp_engineering_metadata_command_tests)
    set_tests_properties(zima_cpp_engineering_metadata_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_metadata_command_tests tests/metadata_command_tests.cpp)
    target_link_libraries(zima_cpp_metadata_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_metadata_command_tests COMMAND zima_cpp_metadata_command_tests)
    set_tests_properties(zima_cpp_metadata_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_export_command_tests tests/export_command_tests.cpp)
    target_link_libraries(zima_cpp_export_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_export_command_tests COMMAND zima_cpp_export_command_tests)
    set_tests_properties(zima_cpp_export_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_assembly_import_command_tests tests/assembly_import_command_tests.cpp)
    target_link_libraries(zima_cpp_assembly_import_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_assembly_import_command_tests COMMAND zima_cpp_assembly_import_command_tests)
    set_tests_properties(zima_cpp_assembly_import_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_import_command_tests tests/import_command_tests.cpp)
    target_link_libraries(zima_cpp_import_command_tests PRIVATE zima_command_host TKDEIGES TKPrim)
    add_test(NAME zima_cpp_import_command_tests COMMAND zima_cpp_import_command_tests)
    set_tests_properties(zima_cpp_import_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_sketch_spline_command_tests tests/sketch_spline_command_tests.cpp)
    target_link_libraries(zima_cpp_sketch_spline_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_sketch_spline_command_tests COMMAND zima_cpp_sketch_spline_command_tests)
    set_tests_properties(zima_cpp_sketch_spline_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_sketch_text_command_tests tests/sketch_text_command_tests.cpp)
    target_link_libraries(zima_cpp_sketch_text_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_sketch_text_command_tests COMMAND zima_cpp_sketch_text_command_tests)
    set_tests_properties(zima_cpp_sketch_text_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_sketch_text_geometry_tests tests/sketch_text_geometry_tests.cpp)
    target_link_libraries(zima_cpp_sketch_text_geometry_tests PRIVATE zima_document_core zima_kernel_occt)
    target_compile_options(zima_cpp_sketch_text_geometry_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_test(NAME zima_cpp_sketch_text_geometry_tests COMMAND zima_cpp_sketch_text_geometry_tests)
    add_executable(zima_cpp_sketch_reference_command_tests tests/sketch_reference_command_tests.cpp)
    target_link_libraries(zima_cpp_sketch_reference_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_sketch_reference_command_tests COMMAND zima_cpp_sketch_reference_command_tests)
    set_tests_properties(zima_cpp_sketch_reference_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_sketch_dimension_command_tests tests/sketch_dimension_command_tests.cpp)
    target_link_libraries(zima_cpp_sketch_dimension_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_sketch_dimension_command_tests COMMAND zima_cpp_sketch_dimension_command_tests)
    set_tests_properties(zima_cpp_sketch_dimension_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_sketch_relation_command_tests tests/sketch_relation_command_tests.cpp)
    target_link_libraries(zima_cpp_sketch_relation_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_sketch_relation_command_tests COMMAND zima_cpp_sketch_relation_command_tests)
    set_tests_properties(zima_cpp_sketch_relation_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_sketch_curve_command_tests tests/sketch_curve_command_tests.cpp)
    target_link_libraries(zima_cpp_sketch_curve_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_sketch_curve_command_tests COMMAND zima_cpp_sketch_curve_command_tests)
    set_tests_properties(zima_cpp_sketch_curve_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_sketch_command_tests tests/sketch_command_tests.cpp)
    target_link_libraries(zima_cpp_sketch_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_sketch_command_tests COMMAND zima_cpp_sketch_command_tests)
    set_tests_properties(zima_cpp_sketch_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_reference_command_tests tests/reference_command_tests.cpp)
    target_link_libraries(zima_cpp_reference_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_reference_command_tests COMMAND zima_cpp_reference_command_tests)
    set_tests_properties(zima_cpp_reference_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_history_command_tests tests/history_command_tests.cpp)
    target_link_libraries(zima_cpp_history_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_history_command_tests COMMAND zima_cpp_history_command_tests)
    set_tests_properties(zima_cpp_history_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_body_command_tests tests/body_command_tests.cpp)
    target_link_libraries(zima_cpp_body_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_body_command_tests COMMAND zima_cpp_body_command_tests)
    set_tests_properties(zima_cpp_body_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_placement_command_tests tests/placement_command_tests.cpp)
    target_link_libraries(zima_cpp_placement_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_placement_command_tests COMMAND zima_cpp_placement_command_tests)
    set_tests_properties(zima_cpp_placement_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_construction_command_tests tests/construction_command_tests.cpp)
    target_link_libraries(zima_cpp_construction_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_construction_command_tests COMMAND zima_cpp_construction_command_tests)
    set_tests_properties(zima_cpp_construction_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

    add_executable(zima_cpp_primitive_command_tests tests/primitive_command_tests.cpp)
    target_link_libraries(zima_cpp_primitive_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_primitive_command_tests COMMAND zima_cpp_primitive_command_tests)
    set_tests_properties(zima_cpp_primitive_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_box_command_tests tests/box_command_tests.cpp)
    target_link_libraries(zima_cpp_box_command_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_box_command_tests COMMAND zima_cpp_box_command_tests)
    set_tests_properties(zima_cpp_box_command_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_command_host_tests tests/command_host_tests.cpp)
    target_link_libraries(zima_cpp_command_host_tests PRIVATE zima_command_host)
    add_test(NAME zima_cpp_command_host_tests COMMAND zima_cpp_command_host_tests)
    set_tests_properties(zima_cpp_command_host_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_command_dispatcher_tests tests/command_dispatcher_tests.cpp)
    target_link_libraries(zima_cpp_command_dispatcher_tests PRIVATE zima_commands)
    add_test(NAME zima_cpp_command_dispatcher_tests COMMAND zima_cpp_command_dispatcher_tests)
    add_test(NAME zima_cpp_console_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_console_ui_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_CONSOLE_ONLY=1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.." TIMEOUT 120)
    add_test(NAME zima_cpp_workspace_startup_contract
        COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_workspace_startup_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_test(NAME zima_cpp_sketch_grips_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_sketch_grips_ui_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_BODY_SKETCH_ONLY=1;ZIMA_VERIFY_SKETCH_GRIPS_ONLY=1"
        TIMEOUT 90)
    add_test(NAME zima_cpp_assembly_refresh_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_assembly_refresh_ui_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_ASSEMBLY_REFRESH_ONLY=1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_test(NAME zima_cpp_section_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_section_ui_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_SECTIONS_ONLY=1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_test(NAME zima_cpp_template_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_template_ui_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_TEMPLATES_ONLY=1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_test(NAME zima_cpp_show_erase_ui_contract COMMAND zima-cad-drawing-harness --verify-show-erase)
    set_tests_properties(zima_cpp_show_erase_ui_contract PROPERTIES ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_test(NAME zima_cpp_measurement_dimension_ui_contract COMMAND zima-cad-drawing-harness --verify-measurements)
    set_tests_properties(zima_cpp_measurement_dimension_ui_contract PROPERTIES ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_test(NAME zima_cpp_drawing_ui_contract COMMAND zima-cad-drawing-harness --verify-ui)
    set_tests_properties(zima_cpp_drawing_ui_contract PROPERTIES
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_dimension_layout_contract_tests tests/dimension_layout_contract_tests.cpp)
    target_link_libraries(zima_cpp_dimension_layout_contract_tests PRIVATE zima_drawing zima_assembly zima_viewer zima_ui nlohmann_json::nlohmann_json)
    add_test(NAME zima_cpp_dimension_layout_contract_tests COMMAND zima_cpp_dimension_layout_contract_tests)
    set_tests_properties(zima_cpp_dimension_layout_contract_tests PROPERTIES ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
    add_executable(zima_cpp_show_erase_contract_tests tests/show_erase_contract_tests.cpp)
    target_link_libraries(zima_cpp_show_erase_contract_tests PRIVATE zima_drawing)
    add_test(NAME zima_cpp_show_erase_contract_tests COMMAND zima_cpp_show_erase_contract_tests)
    add_executable(zima_cpp_measurement_dimension_contract_tests tests/measurement_dimension_contract_tests.cpp)
    target_link_libraries(zima_cpp_measurement_dimension_contract_tests PRIVATE zima_drawing zima_kernel_occt zima_document_core nlohmann_json::nlohmann_json)
    add_test(NAME zima_cpp_measurement_dimension_contract_tests COMMAND zima_cpp_measurement_dimension_contract_tests)
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
    add_test(NAME zima_cpp_owned_profile_reference_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_owned_profile_reference_ui_contract PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.."
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_OWNED_PROFILE_REFERENCE_ONLY=1"
        TIMEOUT 120)
    add_test(NAME zima_cpp_sketch_offset_ui_contract COMMAND zima-cad-cpp --verify-startup)
    set_tests_properties(zima_cpp_sketch_offset_ui_contract PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.."
        ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT};ZIMA_VERIFY_OFFSET_ONLY=1"
        TIMEOUT 120)
    add_executable(zima_cpp_sketch_offset_geometry_tests tests/sketch_offset_geometry_tests.cpp)
    target_link_libraries(zima_cpp_sketch_offset_geometry_tests PRIVATE zima_sketcher)
    add_test(NAME zima_cpp_sketch_offset_geometry_tests COMMAND zima_cpp_sketch_offset_geometry_tests)
    add_executable(zima_cpp_exact_spline_contract_tests tests/exact_spline_contract_tests.cpp)
    target_link_libraries(zima_cpp_exact_spline_contract_tests PRIVATE zima_document_core zima_kernel_occt TKBRep TKG3d TKTopAlgo nlohmann_json::nlohmann_json)
    add_test(NAME zima_cpp_exact_spline_contract_tests COMMAND zima_cpp_exact_spline_contract_tests)
    add_executable(zima_cpp_exact_spline_dialog_tests tests/exact_spline_dialog_tests.cpp app/sketch_bspline_properties_dialog.cpp app/import_options_dialog.cpp)
    target_link_libraries(zima_cpp_exact_spline_dialog_tests PRIVATE zima_ui zima_sketcher Qt6::Widgets)
    add_test(NAME zima_cpp_exact_spline_dialog_tests COMMAND zima_cpp_exact_spline_dialog_tests)
    set_tests_properties(zima_cpp_exact_spline_dialog_tests PROPERTIES ENVIRONMENT "${ZIMA_GUI_TEST_ENVIRONMENT}")
endif()

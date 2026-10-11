# All local executables share a runtime directory. Serialize deployment so
# another linker cannot replace files while a deployment tool loads them.
file(LOCK "${TARGET_DIRECTORY}/.qt-deployment.lock" GUARD PROCESS TIMEOUT 120)
file(GLOB qt_libraries "${QT_SDK}/bin/Qt6*.dll")
foreach(qt_library IN LISTS qt_libraries)
    file(COPY "${qt_library}" DESTINATION "${TARGET_DIRECTORY}")
endforeach()
file(COPY "${QT_SDK}/plugins/" DESTINATION "${TARGET_DIRECTORY}")
# --force would also replace Windows' ICU forwarding DLL while Qt's deployment
# tool has it loaded. Qt files already come from the selected coherent kit.
execute_process(COMMAND "${QT_SDK}/bin/windeployqt.exe" "${QT_CONFIGURATION}"
    --no-compiler-runtime --no-translations "${TARGET_EXECUTABLE}"
    RESULT_VARIABLE deployment_result OUTPUT_VARIABLE deployment_output
    ERROR_VARIABLE deployment_error)
if(NOT deployment_result EQUAL 0)
    if(NOT "${deployment_output}${deployment_error}" MATCHES "does not seem to be a Qt executable")
        message(FATAL_ERROR "Selected Qt SDK deployment failed: ${deployment_result}\n${deployment_output}${deployment_error}")
    endif()
endif()

# Deploy the selected SDK's WebEngine helper and data for the local launcher.
find_program(qmake NAMES qmake.exe qmake
    PATHS "${QT_RUNTIME_DIRECTORY}" "${QT_RUNTIME_DIRECTORY}/../tools/Qt6/bin"
          "${QT_RUNTIME_DIRECTORY}/../../tools/Qt6/bin" NO_DEFAULT_PATH REQUIRED)
foreach(kind LIBEXECS DATA TRANSLATIONS)
    execute_process(COMMAND "${qmake}" -query "QT_INSTALL_${kind}"
        OUTPUT_VARIABLE qt_${kind} OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot query the selected Qt SDK: QT_INSTALL_${kind}")
    endif()
endforeach()
set(helper "${qt_LIBEXECS}/QtWebEngineProcess.exe")
if(CONFIGURATION STREQUAL "Debug")
    set(helper "${qt_LIBEXECS}/QtWebEngineProcessd.exe")
    if(NOT EXISTS "${helper}")
        set(helper "${qt_LIBEXECS}/debug/QtWebEngineProcessd.exe")
    endif()
endif()
if(NOT EXISTS "${helper}" OR NOT IS_DIRECTORY "${qt_DATA}/resources"
        OR NOT IS_DIRECTORY "${qt_TRANSLATIONS}/qtwebengine_locales")
    message(FATAL_ERROR "The selected Qt WebEngine SDK lacks its helper, resources or locales")
endif()
file(COPY "${helper}" DESTINATION "${OUTPUT_DIRECTORY}")
file(COPY "${qt_DATA}/resources/" DESTINATION "${OUTPUT_DIRECTORY}/resources/qt/resources")
file(COPY "${qt_TRANSLATIONS}/qtwebengine_locales" DESTINATION "${OUTPUT_DIRECTORY}/translations")
file(WRITE "${OUTPUT_DIRECTORY}/qt.conf"
    "[Paths]\nPrefix=.\nPlugins=.\nLibraryExecutables=.\nData=resources/qt\nTranslations=translations\n")

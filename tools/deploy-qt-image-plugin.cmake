if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()
# Deploy a dynamically loaded Qt image plugin and its non-system runtime DLLs.
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}/imageformats")
file(COPY_FILE "${PLUGIN}" "${OUTPUT_DIRECTORY}/imageformats/${PLUGIN_NAME}" ONLY_IF_DIFFERENT)
file(GET_RUNTIME_DEPENDENCIES
    LIBRARIES "${PLUGIN}"
    DIRECTORIES "${QT_RUNTIME_DIRECTORY}"
    RESOLVED_DEPENDENCIES_VAR runtime_dependencies
    UNRESOLVED_DEPENDENCIES_VAR missing_dependencies
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*" "[Vv][Cc][Rr][Uu][Nn][Tt][Ii][Mm][Ee].*" "[Mm][Ss][Vv][Cc][Pp].*"
    POST_EXCLUDE_REGEXES ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32.*")
if(missing_dependencies)
    message(FATAL_ERROR "Missing image plugin dependencies: ${missing_dependencies}")
endif()
foreach(dependency IN LISTS runtime_dependencies)
    get_filename_component(name "${dependency}" NAME)
    file(COPY_FILE "${dependency}" "${OUTPUT_DIRECTORY}/${name}" ONLY_IF_DIFFERENT)
endforeach()

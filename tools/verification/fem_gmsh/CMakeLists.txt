cmake_minimum_required(VERSION 3.24)
project(ZimaFemMesherProbe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(OpenCASCADE 8.0.1 EXACT CONFIG REQUIRED)
find_path(GMSH_INCLUDE_DIR gmsh.h REQUIRED)
find_library(GMSH_LIBRARY NAMES gmsh gmsh.dll gmsh-4.15 REQUIRED)
add_executable(zima-fem-mesher-probe main.cpp)
target_include_directories(zima-fem-mesher-probe PRIVATE
    "${OpenCASCADE_INCLUDE_DIR}" "${GMSH_INCLUDE_DIR}")
target_link_libraries(zima-fem-mesher-probe PRIVATE "${GMSH_LIBRARY}"
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
    TKPrim TKBO TKBool TKFillet TKOffset TKShHealing)
if(MSVC)
    target_compile_options(zima-fem-mesher-probe PRIVATE /utf-8 /bigobj)
endif()

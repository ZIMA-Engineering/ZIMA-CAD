# Shared Windows/Linux configuration for the initial native mesher probe.
# Pass the selected OCCT SDK through CMAKE_PREFIX_PATH and OCC_INC explicitly.
set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "Upstream CMake policy baseline")
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration")
set(GMSH_RELEASE ON CACHE BOOL "Use the source release identity")
set(GMSH_PACKAGER ZIMA-Engineering CACHE STRING "Dependency packager")
set(GMSH_HOST native-mesher-probe CACHE STRING "Reproducible build host label")
# Upstream resets its INTERNAL DEFAULT variable. Pin individual options so a
# clean cache and a reused cache enable exactly the same dependency closure.
foreach(option BUILD_SHARED MESH OCC ANN ALGLIB OPTHOM EIGEN TINYOBJLOADER
        RPATH PACKAGE_STRIP)
    set(ENABLE_${option} ON CACHE BOOL "Required by the mesher probe" FORCE)
endforeach()
foreach(option 3M BAMG BLAS_LAPACK BLOSSOM BUILD_LIB BUILD_DYNAMIC BUILD_ANDROID
        BUILD_IOS CGNS CGNS_CPEX0045 CAIRO PROFILE DINTEGRATION DOMHEX FLTK
        GEOMETRYCENTRAL GETDP GMM GMP GRAPHICS HXT KBIPACK MATHEX MED MESQUITE
        METIS MMG MPEG_ENCODE MPI MSVC_STATIC_RUNTIME MUMPS NETGEN NII2MESH
        NUMPY PETSC4PY OCC_CAF OCC_STATIC OCC_TBB ONELAB ONELAB_METAMODEL
        OPENACC OPENMP OS_SPECIFIC_INSTALL OSMESA P4EST PARSER PETSC PLUGINS
        POST POPPLER PRIVATE_API PRO QUADMESHINGTOOLS QUADTRI REVOROPT SLEPC
        SOLVER SYSTEM_CONTRIB TCMALLOC TESTS TINYXML2 TOUCHBAR UNTANGLE
        VISUDEV VOROPP WINSLOWUNTANGLER WRAP_JAVA WRAP_PYTHON ZIPPER)
    set(ENABLE_${option} OFF CACHE BOOL "Outside the current probe" FORCE)
endforeach()

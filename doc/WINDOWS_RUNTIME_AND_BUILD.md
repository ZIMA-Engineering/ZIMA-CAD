# Windows runtime and release packaging

The active application is now the C++ `zima-cad-cpp` target. A supported C++
Windows portable-build pipeline has not yet been established.

Do not publish a Windows build by adapting or bypassing the former Python
packaging scripts. Before the first C++ Windows release, add a repository-owned
build and validation pipeline that verifies at least:

- the exact committed Git source used for the build;
- the C++ executable and all Qt/OCCT runtime DLL dependencies;
- archive path safety, collisions, CRC and SHA-256;
- extraction and GUI startup from a normal Windows user directory;
- a deterministic Part calculation smoke test.

The frozen Python/OpenBLAS packaging procedure and its scripts are retained
only for historical reference under `archive/python/`.

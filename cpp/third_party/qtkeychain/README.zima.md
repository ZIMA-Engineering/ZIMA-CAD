# QtKeychain dependency

This directory contains QtKeychain 0.15.0 from
<https://github.com/frankosterfeld/qtkeychain/releases/tag/0.15.0>.
The upstream implementation and license are retained. Only the build files,
platform backends and supporting CMake modules required to build the library
are included; example applications and translations are not distributed here.

ZIMA-CAD builds a static Qt 6 library on Windows and Linux. The Windows build
uses the native credential store. Linux uses the upstream credential-service
backends. Insecure file fallback is disabled for application credential jobs.
See `COPYING` for the upstream license.

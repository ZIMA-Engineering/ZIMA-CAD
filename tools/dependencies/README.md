# Shared native dependency patches

Both native SDKs use OCCT **8.0.0** plus the same
[`occt-gauss-large-spans.patch`](occt-gauss-large-spans.patch). The patch leaves
integration precision and quadrature unchanged. It removes the historical fixed
cap of 1,953 intervals (32 times the maximum quadrature order, plus one) from
adaptive refinement, as explicitly approved by the user on 2026-10-10. The
integrator uses the already allocated adaptive budget based on its initial knot
intervals, with its existing precision and allocation-bound termination. The
unpatched fixed cap can precede the initial intervals, making its stopping index
unreachable and allowing refinement to write beyond the allocated budget.

The allocation counts actual intervals, including bounds extending beyond the
knot range. It also clears the complete reused error vector, because its
maximum-error search includes retained capacity beyond the current budget.
These checks are necessary when the fixed cap no longer masks those ranges.

Windows uses the repository vcpkg overlay selected by `cpp/vcpkg.json`. Its
OCCT port is copied from vcpkg baseline
`e03dc9b29710050cd1018bc5674688108658d327`, port tree
`380113d2be95b9e41f2eb1740d809769edf41769`, with only the shared patch and its
SDK provenance record added. The five upstream packaging patches retain their
original content. vcpkg is distributed under the MIT license.

When editing the shared patch, update `ZIMA_GAUSS_PATCH_EXPECTED_SHA256` in the
portfile in the same change. vcpkg does not automatically hash a patch outside
the port directory; this explicit digest makes it part of the package ABI.
The port verifies the digest both before and after the SDK build.

Linux applies the identical patch in `tools/distribution/build-linux-sdk.sh`.
Re-running that script accepts only the original or already patched source.
Each installation writes `share/opencascade/zima-sdk.json` with the source
version and actual patch SHA-256. Rebuild the SDK before building the application;
an older SDK cannot provide the repaired integration contract.

OCCT source and the patch remain subject to its LGPL 2.1 license with the OCCT
exception. Portable distributions must retain the kernel license notices and
the complete repository source, including this patch and its build recipes.

Native Linux execution is pending the user's reboot and is recorded in
[`doc/LINUX_RELEASE_HANDOFF.md`](../../doc/LINUX_RELEASE_HANDOFF.md).

# A13 — cached SVG lookup before resource reads

## Scope

`svg_icon()` now checks its existing icon cache before opening an embedded SVG,
copying its bytes and replacing colours. The cache key, rasterization, supported
sizes and palette-dependent states are unchanged. Only immutable compiled Qt
resources call this private helper. No document data, geometry, reference identity,
transactions, user settings or translated strings change.

The outer `resource_icon()` cache already avoids most repeated raster preparation.
This change removes residual work for direct `application_icon()` requests and
reuse of SVG entries by separate icon engines; it is not a large scene/rendering
optimization. Cold misses still perform the same work.

## Measurement and equivalence

Windows x64, MSVC Release, the repository's native Qt dependencies. A temporary
C++ driver under `build/icon-cache-check` compiles the source from `2f612fa2`
under a separate namespace beside the modified implementation, linked to the same
repository Qt resources. Both implementations run in the same process. No other
build or test was running during measurement. The offscreen Qt platform removes
window interaction from the timing.

The driver compares exact pixels and device-pixel ratio for the application icon,
Origin variants, confirmation, combined protrusion/revolution, Sweep and Save As,
including surface badges and Tree icons. It covers sizes 16/18/20/24/32/48,
DPR 1/1.5/2, Normal/Disabled/Active/Selected, Off/On and light/dark/light palette
changes. All **9,504 comparisons passed**. This includes first-use and subsequent
cache reuse; no image tolerance or reduced raster quality is involved.

Five alternating before/after samples each request the cached application icon
100,000 times, consuming its cache key. Mean time: **117.280 ms before, 13.941 ms
after** (88.1% less time for this operation, about 1.03 microseconds saved per
request). These are warm-cache microbenchmark results, not overall application
speedups or a claim about large-model interaction latency.

[Raw results](20260930-icon-cache.txt). The current translation/catalog contract
also validates that this implementation-only change introduces no untranslated UI
text. Native GUI and CLI builds are rebuilt for the committed source.

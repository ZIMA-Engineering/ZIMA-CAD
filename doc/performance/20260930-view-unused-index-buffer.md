# A6 — remove an unused GPU index buffer

MeshView flattened opaque triangles into shaded vertex records and rendered them
with glDrawArrays. A separate original-index buffer was still created, uploaded
and destroyed, but was never consumed by a draw. Transparent triangles already
use their own depth-sorted index buffer. That live buffer remains intact.

Removed only the unused buffer member and its lifecycle/upload calls. CPU mesh,
topology, exact picking, normals, transparency, silhouettes and display modes
remain unchanged. This is a verified nonfunctional remnant, not reduced detail.

The desktop Assembly fixture compared all five display modes at opaque and
partially transparent settings for 256 and 1,024 box occurrences. All twenty
RGBA frame SHA-256 hashes matched exactly. Hover/click candidate identity and
bounds checks passed. Each upload avoids 36,864 or 147,456 index bytes,
respectively (12 bytes per triangle in general).

End-to-end set_mesh plus completed framebuffer painting averaged 59.166 →
55.210 ms and 209.206 → 206.090 ms in those runs. These small differences include
CPU preparation and rendering noise; they do not establish a general frame-rate
improvement. The avoided allocation/upload and identical output are the firm
result. No new UI strings or native-format changes.

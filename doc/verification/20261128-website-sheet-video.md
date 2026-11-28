# Sheet-from-solid website recording

The homepage slideshow and gallery include the complete user-supplied recording
**ZIMA-CAD – Sheet Metal from Solid**. The web copy is 1920 × 1044 H.264 with AAC
audio and MP4 fast-start metadata; the original recording is unchanged. Its
235.03-second duration is retained. The delivered file is 17,806,971 bytes.

Every preview keeps its visible caption. The slideshow caption now has larger
text, stable spacing and theme-aware foreground/background colors. The new title
is translated into Czech, English, German, French and Russian through the existing
metadata and translation catalogs. Media discovery and playback controls retain
their existing implementation.

Verification on Windows:

- Website generation and validation pass: 806 HTML pages, 17,106 local references,
  five languages and 1,944 explicitly allowed public files.
- Browser playback of the complete video reports 1920 × 1044, duration 235.033333
  seconds, decoded media ready and no media error; time advances during playback.
- Pause stops playback; hidden videos remain paused. Gallery videos have native
  controls and start paused. All five localized captions are present in the DOM.
- Desktop and 390 × 844 mobile layouts were inspected in Light and Dark themes.
  The title stays below the video and remains readable without covering the CAD.

Published to the existing website on 2026-11-28 from commit `cc65ab24f`.
Anonymous HTTPS retrieval verified all 1,944 public files against the committed
SHA-256 manifest, with no mismatches. The live homepage decoded the complete
235.033333-second video at 1920 × 1044, played it, and displayed the Czech caption
below the recording. A verified local backup of the preceding website is retained.

Delivery SHA-256:
`c7c6de569badcd68b6d14d89916388bf9bb967be500536b0f6c0967531121707`.

Original SHA-256:
`68dc71c275fa358a7f797dd859b9750e707494356bae486069c8cd3447da3e17`.

The recording also supplies a separate modeling investigation: editing a source
extrusion dimension around 2:10 spends substantial time updating. Source Fillet
radius, placement and size changes require follow-up verification of converted
sheet geometry, correction identities and unfolding. This website change does
not claim that modeling behavior has been repaired.

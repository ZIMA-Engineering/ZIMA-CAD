# Render

## Before you start

Calculate the source solid Bodies in a Part. Set their colors and appearance,
then arrange an initial View.

## Steps

1. Activate the Part root and choose **Create Render**.
2. Keep **Visible Bodies**, or use **Selected Bodies** to choose explicit
   sources. Set image dimensions and sampling quality, then confirm **OK**.
3. Add or edit cameras and lights as needed. Select the active camera in
   Render Body Properties.
4. Choose **Render image**. The separate Render tab displays progress and the
   result; **Render gallery** reopens that tab.
5. Export the completed image as PNG or JPEG. Use **Save to Part** when you also
   want to retain the snapshot inside the native Part.

## What to check

Rendering uses a snapshot of already calculated geometry. Source edits do not
rewrite an older rendered image. Closing the Render tab leaves its job running;
**Stop rendering** cancels it, and closing the owning document stops the job.

More samples can reduce visible noise. Retained images remain historical records
and increase document size. The initial renderer supports Part solid scenes;
Assembly scenes, animation, textures and glass refraction are outside this scope.

See [Render](../RENDER.md).

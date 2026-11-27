# Getting started

## Before you start

Choose a working folder for your native documents. ZIMA-CAD uses `.prtz` for
Parts, `.asmz` for Assemblies and `.drwz` for Drawings.

## Steps

1. Open **Tools > System Setup**. Select the metric or inch preset and matching
   Part and Assembly templates. Review the working folder and confirm **OK**.
2. Choose **File > New**, select **Part** in the dialog and enter a name. Choose **Modeling** or **Sheet Metal**. Both modes create an ordinary Part and select its initial tools; confirm **OK**.
3. Work inside the active editable Body. Start a modeling command, place its
   references and use **Sketch** when the command needs a profile.
4. Finish the Sketch, review the pending feature and confirm **OK**. **Cancel**
   discards the pending definition.
5. Use **File > Save** to store the Part. Create an Assembly or Drawing when you
   need components or a technical sheet.

## What to check

The Tree should show the new feature inside its Body. Orbit with the middle
mouse button; pan with middle and right buttons together. **Regenerate** is an
explicit calculation command. Switching document tabs does not regenerate models.

See [System Setup](../SYSTEM_SETUP.md) and the
[user manual](../USER_MANUAL.md#new-documents).

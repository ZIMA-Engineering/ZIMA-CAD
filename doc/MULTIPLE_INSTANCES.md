# Independent ZIMA-CAD instances

Double-clicking `.prtz`, `.asmz`, or `.drwz` in Explorer starts a new ZIMA-CAD
process and opens that document. The same applies to `.frmz` and `.tblz` templates.
The document determines its instance's working directory and local configuration;
Explorer's or a desktop shortcut's working directory does not override it.

Windows can sit side by side or on separate monitors for project comparison.
Each process has its own open documents, selection, camera, and Undo/Redo history.
Closing one process does not close the others.

Example title: **ZIMA-CAD - Instance 2 - ZE0001.asmz**. The number stays constant
when switching/closing documents or closing another instance. Exiting releases
that number for future launches; existing windows are not renumbered. If the
system cannot reserve a number, the title uses the PID and the application still starts.

**Window -> New Window** also starts an independent process, initially without
documents in the original window's working directory. **File -> Set Working
Directory** can choose another project for that instance only. **File -> Open**
and in-application component opening still use the current instance, allowing
an Assembly, its Parts, and related drawings to coexist in tabs.

## Windows registration

After building locally, run:

```powershell
./tools/register-windows-file-types.ps1
```

Optional `-Executable` selects a specific C++ EXE. Registration applies only to
the current user, requires no administrator, and points directly to the GUI.
The open command is `"path to EXE" "%1"`; paths containing spaces remain one
argument, and each launch creates an independent process without a console.
Rerun registration after moving the EXE.

The script does not change protected Windows `UserChoice`. If the user explicitly
selected another program previously, choose ZIMA-CAD through Explorer's
**Open with -> Choose another app**.

## Isolation scope

Instances isolate in-memory working documents. Disk files and global settings
remain shared. An instance number is neither a document lock nor synchronization
for concurrent edits to one file.

Number reservations use `QLockFile` in the user-data `ZIMA-CAD/instances` directory.
They do not expire while a process runs, are released automatically after process
termination, and never enforce a single-instance restriction.

## Verification

`zima_cpp_instance_startup_contract` runs actual GUI processes for a Part,
Assembly, and Drawing concurrently. It checks distinct PIDs/numbers, each process's
document and working directory, another process through New Window, stable titles
after document closure, and independent exit. Separate checks cover argument
handling, paths with spaces, extensions, and instance-number release.

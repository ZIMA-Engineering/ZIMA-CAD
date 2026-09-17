# Independent ZIMA-CAD instances

Double-clicking `.prtz`, `.asmz`, or `.drwz` in Explorer starts a new ZIMA-CAD
process and opens that document. The same applies to `.frmz` and `.tblz` templates.
The document determines its instance's working directory and local configuration;
Explorer's or a desktop shortcut's working directory does not override it.
If another process already reserves that directory or document, the new launch
reports the conflict instead of opening a second editable copy.

Windows can sit side by side or on separate monitors for project comparison.
Each process has its own open documents, selection, camera, and Undo/Redo history.
Closing one process does not close the others.

Example title: **ZIMA-CAD - Instance 2 - ZE0001.asmz**. The number stays constant
when switching/closing documents or closing another instance. Exiting releases
that number for future launches; existing windows are not renumbered. Failure to
reserve a number stops startup with an error; no unreserved fallback instance starts.

**Window -> New Window** asks for a different working directory and starts an
independent process without documents. **File -> Set Working
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

Application instances, including the standalone CLI, may not share a working
directory or an open document file. The integrated console belongs to its GUI
instance and shares that instance's reservations.
The reservations are acquired atomically, including concurrent startup.
Windows within a single process share that process's reservations. Explicit
directory changes to an occupied directory fail without changing the current
directory. If opening/saving a different available file would automatically select
an occupied directory, the current working directory is retained instead.

Native files are reserved when added to the workspace (including component-source
opening and hidden imported sources). Save, Save Copy, native rename publication
and template saves check their output paths before writing. Changing directory
does not release open file reservations. Closing a document releases its path once
no other open family member uses it. Opening an already open file in the same
instance activates its existing tab. Read-only Assembly dependency hydration does
not open an editable source document or reserve the dependency for editing.

Number, directory and file reservations use `QLockFile` under the user's temporary
`ZIMA-CAD-instance-locks` directory, shared across projects and installation versions.
They have no age-based expiration; Qt recovers stale reservations after process
termination. Failure to create a reservation is reported instead of silently
disabling protection. Canonical paths handle symlinks/junctions, relative spellings
and Windows case differences. Reservations are runtime coordination only: no
required files are added beside native documents. They coordinate cooperating
application processes on this host, not third-party editors or separate machines.

## Verification

`zima_cpp_instance_startup_contract` runs actual GUI processes for a Part,
Assembly, and Drawing concurrently. It checks distinct PIDs/numbers, each process's
document and working directory, another process through New Window, stable titles
after document closure, and independent exit. It rejects duplicate startup
directories, occupied directory changes and opening the same file after its owner
has changed directory; closing that file allows the other process to open it.
Separate checks cover argument handling, paths with spaces, extensions, canonical
directory aliases, file release and instance-number release. A forcibly terminated
GUI process is restarted against the same directory to check stale-lock recovery.

Windows verification on 2026-09-17: the real-process instance contract passed,
including concurrent numbering, directory/file exclusion, file release, New Window
and forced-termination recovery (`build/instance-isolation-process-tests.log`).
The current development GUI and CLI were rebuilt; `zima-cad.bat` retains its normal
repository-root entry point. This is a local build, not a published release.
The final process, component-properties GUI and Assembly-refresh GUI rerun passed
all three tests (`build/instance-isolation-final-tests.log`). Document operations,
component source opening, command-host and real CLI-process regression tests also
passed (`build/instance-isolation-regression-tests.log`; its initial GUI failures
were resolved by sharing reservations within a process and the final rerun).

# AI in the CAD console

The desktop console can use Codex as an engineering collaborator through the
official [Codex App Server](https://learn.chatgpt.com/docs/app-server).
The native provider follows ZIMA-CAD-Parts' account and process integration;
CAD work uses ZIMA-CAD's existing shared GUI/CLI command host.

## Start

1. Open **Settings > AI**. The panel uses the existing internal Settings window.
2. Select a native Codex executable. Windows also discovers the executable
   installed with the Codex desktop application. The installation help button
   opens the official CLI instructions. Shell wrappers such as `.cmd` and `.bat`
   are not accepted.
   When a Windows desktop update removes the previously selected version's
   `codex.exe`, Settings and Connect rediscover the installed native executable.
   An existing selected executable is retained; a missing custom path outside
   the managed desktop installation still requires correction. Opening Settings
   does not connect or save the recovered preference; OK saves it normally.
3. Click **Connect**, then **Sign in with ChatGPT** if requested. Complete sign-in
   in the browser. Each user uses their own account and its usage limits.
4. Select an available model, or keep **Codex default**. Click **OK** to save the
   executable and model preferences.
5. Open the CAD console with its toolbar button or **Ctrl+Shift+C**, type `codex`,
   and enter a request about the active document.

For example: "Inspect this part and tell me its dimensions" or "Create a box
30 by 20 by 10 mm in this part." Model changes are reviewed in the console before
execution. A rejected or failed command is reported as such.

`/new` starts a new conversation. `/exit` returns to ordinary CAD commands.
**Stop** cancels the request and discards pending approval; reconnect to continue.
Completed CAD operations remain in their normal history and may be undone where
the underlying CAD command supports Undo. Stop does not undo completed work.
While a request is running, the input remains editable. Its draft is never
submitted automatically after completion.

## Active document and review

Each request captures the active **Part**, **Assembly** or **Drawing** tab, the
displayed document, active occurrence, confirmed selection, working directory and
document revisions. In an Assembly, an activated component remains the editing
document while the top-level Assembly remains the displayed context.

The initial prompt contains document metadata and confirmed selection. The AI
can inspect further persisted model data through the command catalog. It cannot
invent references or recalculate bodies during ordinary inspection. Engineering
reasoning follows the bundled [ZIMA Engineering Reasoning](AI/ZIMA_ENGINEERING_REASONING.md).

The adapter exposes three tools:

| Tool | Behavior |
| --- | --- |
| `cad_help` | Searches the current shared command catalog in pages of 12. |
| `cad_context` | Reads the captured context and open-document revisions. |
| `cad_command` | Runs a catalog query or proposes one command that changes state. |

Commands marked `changes_state` require **Allow command** or **Deny** directly in
the console. The review includes the target document, reason, command and exact
arguments. Approval consumes that retained request once. Normal host validation,
ownership checks, explicit calculation, transactions and save behavior apply.
Saving is a separate command; an in-memory edit does not imply a saved file.

If the active/displayed document, occurrence, selection, directory or document
revision changes before execution, the pending command fails with
`context_changed`. A new user request captures the new context. Moving the mouse,
hovering or navigating the camera does not invalidate review. State produced by an
approved command becomes the baseline for subsequent commands in the same request.
Switching tabs or active occurrences starts a fresh provider conversation on the
next request, even when the documents share a directory.

## Account, settings and data

Opening CAD or Settings does not start Codex or sign in. Connect, sign-in and
sign-out are explicit immediate actions. OK saves preferences; Cancel discards
pending preference edits without reverting an already completed account action.

Requests, supplied context and requested command results are sent to OpenAI.
Model metadata and query results can contain project names, paths and geometry.
The provider uses a dedicated `ai/codex` profile below Qt's application-local data
directory. It does not copy credentials from Parts or the Codex desktop profile.
Authentication uses Codex's OS keyring storage. No API key or access token belongs
in CAD configuration or release archives. See [Codex authentication](https://learn.chatgpt.com/docs/auth).

The shared installation `config/config.ini` holds `AI/Model` and the platform
specific `AI/CodexExecutableWindows` or `AI/CodexExecutableLinux`. These are local
preferences; project configuration does not choose an executable. Runtime version
switches preserve this shared configuration. Codex is optional and installed
separately; it is not bundled into the CAD portable archive.

The App Server runs with a dedicated profile, a read-only sandbox, native tools
disabled and only the supplied dynamic CAD tools. Unsupported native-tool or
approval requests close the provider. Stderr is drained without displaying
possible credentials. Protocol responses, prompts, tool output and tool counts
are bounded. The adapter does not execute shell commands from AI output.

## Verification

`zima_ai_contract` uses a local stdio provider fixture and the real CAD command
host. It covers protocol initialization, dynamic tools, conversation separation,
sign-in notifications, malformed responses, unsupported native tools, Stop,
catalog discovery, retained approval, denial, single execution, Undo, revision and
selection changes, Part/Assembly/Drawing switching, editable drafts and settings.
No account or paid inference is needed for these tests.

The existing Updates GUI contract also checks the real internal AI Settings tab,
no automatic connection, Cancel and OK persistence. Native Codex
`0.154.0-alpha.6.2` accepted initialization, a signed-out account query and the exact
experimental `thread/start` payload in an isolated profile. This verifies the
installed protocol, not a live authenticated model response. The later live
verification below extends that initial acceptance.

Acceptance logs: `build/ai-contract.log`, `build/ai-final-contracts.log`,
`build/ai-windows-ui.log` and `build/ai-regression.log`. The Windows console review,
real AI Settings and document-switch hover/pressed captures were visually checked.
The related UI contract also verifies both wheel directions in every combination
of orthographic/perspective projection and ordinary/fly navigation.
The final console state/target-label follow-up passed `zima_ai_contract` again
(`build/ai-console-final.log`). Those initial GUI, CLI and updater checks used
build `2026091505`.

### Executable recovery and live verification — 2026-10-10

The user's saved Windows executable pointed into a retired Codex desktop version
directory and no longer existed. The installed runtime was Codex
`0.162.0-alpha.2`. The provider had rejected the saved path before initializing
the server. Configuration recovery is now limited to a missing native
`codex.exe` directly below a version directory in `%LOCALAPPDATA%/OpenAI/Codex/bin`.
The existing discovery path is reused; valid custom executables and missing
unmanaged custom paths retain their established behavior.

The current native provider connects successfully using the CAD's existing
dedicated ChatGPT profile. A live read-only request over a temporary Part named
`AI-Integration-Diagnostic` calls `cad_context` and `cad_help`, then completes
with a Czech model response naming that Part and confirming catalog access.
Both the explicit current executable and the formerly broken saved path pass
the live roundtrip. Each run records one successful context call, one successful
catalog call, zero proposed mutations, an unchanged complete Part serialization,
and no provider failure or timeout. Credentials are not copied from the desktop
profile, and the diagnostic does not sign in, sign out or edit user documents.

`zima_ai_contract` passes after extending its coverage with desktop discovery
without PATH, retired-version recovery, existing/custom-path preservation,
unmanaged path rejection, preserved model selection, no automatic connection,
Connect's actual selected path and no preference write on Settings close.
The original protocol, review, denial, single execution, Undo and context-binding
checks also pass. The actual internal Settings GUI contract passes Cancel/OK
and silent opening; source localization coverage, placeholder/catalog validation
and all five language checks pass. No product-visible strings were changed.

Developer diagnostics can use `zima_ai_contract_tests --probe-codex <executable>`
to report connection/account state without inference. Adding `--live` performs
the temporary read-only CAD roundtrip; it requires an already signed-in CAD
profile and uses that account's inference allowance. An optional profile path
may follow the executable. The live diagnostic is opt-in, not part of CTest;
ordinary regression tests still require no account or network.

Evidence: `build/ai-investigation-contract.log`, `build/ai-investigation-account.log`,
`build/ai-investigation-live.log`, `build/ai-recovery-contract.log`,
`build/ai-recovery-live.log`, `build/ai-recovery-ui.log` and
`build/ai-recovery-locales.log`. The local native application is rebuilt.
Live model mutation/review and Linux execution remain unverified by this
investigation; mutation/review behavior is covered by the local protocol fixture
and real CAD command host. The fix is included in the new Windows build
`2026101002`; previously released archives remain immutable.

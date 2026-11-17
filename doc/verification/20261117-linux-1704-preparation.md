# Linux 2026111704 preparation

Source baseline: `b4a4730b736430fa29159313bbec98eced8faae1` (Windows 2026111703
publication record). Debian 13.6 x86_64, GCC 14.2.0, Qt 6.8.2, OCCT 8.0.1 with
the existing shared Gauss patch. Application, CLI and updater compilation pass.
The committed-source candidate has a separate 2026111704 build identity.

## Native scope

The matrix includes the 36 native suites from the Windows current-OCCT API
record, the Gauss and thread catalog contracts, six Drawing/browser suites and
twelve additional Profile, history, reference, frame and copy suites. All 56
run without skips. The initial result is 53 passes and three failures in
265.05 seconds. This is a finite matrix, not the complete CTest inventory.

The Mirror command fixture rejected a source-only creation. Current specified
behavior chooses the new Mirror's own XY plane. The corrected fixture verifies
that owner/key and one-transaction Undo, then rejects an explicitly missing
source. All other invalid-reference, cycle, locking, persistence, geometry and
Undo/Redo assertions remain. The complete rerun passes, giving 54 passing
suites and two unresolved suites. This correction changes tests only.

- `zima_cpp_assembly_history_command_tests` reports `Metadata ordering changed
  component geometry, placement or mates`. The same assertion is present in
  `build/linux-20261105-native-tests.log` from the previous accepted Linux
  release. No claim is made that snapshot preservation is verified.
- `zima_cpp_bend_command_tests` reaches `expanding-profile-axis clearance` and
  reports `Assembly cut failed or produced an invalid solid`. This occurs in
  the conical clearance matrix, which independently subtracts a probe from the
  calculated result. The retained failed fixture is
  `/tmp/zima-bend-16406695671519`. The old Linux suite failed earlier at a
  different assertion; that is not proof of this case's baseline status.

Do not suppress these assertions, reduce precision or label them passing.
The planned publication is a prerelease excluded from stable update discovery.

## Package and desktop scope

The initial clean-source candidate passes dependency deployment and archive
validation, then loads all 1,185 factory native documents. Its CLI smoke passes
Part creation/save/reopen with measured volume, PDF/JPEG export and identical
factory Drawing rendering from project/source working directories. It aborts
at the required GUI smoke: `Failed to create wl_display (Operation not permitted)`.
The current execution sandbox denies the live Wayland socket. No signed or
public release is claimed. The candidate builder must resume on the real host
desktop and complete its unchanged full gates before signing/publication.

Fifteen desktop suites are selected for the host: circular inference, Mirror
save/Undo/camera/default plane, templates, translations, edge-treatment editing,
creation, fragments, method and symmetry, Drawing controls/dimensions/details,
retained feature frames, Sheet Profile and Line Bend. Fresh signed-package
circle, Mirror and template checks are also required. GNOME, other
distributions and saved private Windows model replays remain unverified.

Packaging tests: 15 run, one skipped. Publisher tests: 16 passed.
All five catalogs have matching key coverage (2,831 Qt JSON keys and 2,716 INI
keys per language under last-key-wins parsing). Native Qt desktop translation
validation remains required. Preparation adds no product strings or keys.

Factory/native assets and production algorithms are unchanged from the source
baseline. User settings, projects, screenshots, browser data and signing keys
are preserved outside the committed-source package. Current compatibility with
every older native document is not established; preserve original project files
when testing. Application rollback does not roll back saved native documents.

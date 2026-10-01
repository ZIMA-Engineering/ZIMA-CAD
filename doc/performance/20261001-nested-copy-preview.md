# Nested Pattern, Mirror and Body Scale preview publication

## Cause and change

Inputs are current calculated source geometry, the exact active occurrence and
pending command parameters. Outputs must preserve the same contextual Assembly,
rollback input, preview wire, origins and selectable references. The optimization
uses the existing command-specific scene builders; it changes neither calculation
precision nor the resulting meshes.

Nested Pattern/Mirror and Body Scale called the general scene refresh, which
built and published a complete scene. They then immediately built and published
their own occurrence override. Deleting the entire refresh would lose Tree,
action, appearance and selection preparation.

The general refresh now accepts an optional synchronous Assembly scene producer.
Part-owned Pattern/Mirror and Body Scale consumers supply their existing override builder. Source refresh,
Tree, actions and other preparation still run; the Assembly geometry branch calls
the producer once instead of constructing the intermediate scene. The callback
is not stored and runs after source refresh. During existing deferred reference
refresh it retains the former command-local publication after the early return. No cross-event cache or historical
source pinning is introduced. Other callers use the unchanged default branch.

The command's existing final mesh fitting policy is preserved. The scene-refresh
tail now observes the final scene directly; command-local selection filters,
reference offerings, inspection and transient wire are still installed by the
existing command callback afterward. Root Part commands and Assembly-owned copies are unchanged. Pattern,
Mirror and Scale body calculations, persistence and commit handlers are unchanged.

## Reproducible probe

`ZIMA_VERIFY_PREVIEW_REFRESH=1` adds four measured unchanged preview updates at
the existing nested-copy and nested-scale GUI checkpoints. Each report includes
elapsed time with GUI events, base-mesh publication count and an RGBA framebuffer
SHA-256 taken outside the timed interval. The existing GUI tests continue through
source/Origin selection, commits, reopen, Cancel and their other assertions.
The test helper is opt-in and does not affect ordinary application execution.

The fixtures contain native boxes and repeated/nested occurrences. They exercise
real command dialogs but are not a timing claim for a customer's production
Assembly. No new product strings or translation keys are introduced. Localization
coverage remains a required validation gate. No native schema, portable package
or Linux change is included.

## Scope decision from measurement

The initial experiment also used one publication for Assembly-owned copies inside
a nested Assembly. Its frames and functional tests matched, but the first two
Mirror checkpoints were slower in two measured runs and other timings were mixed.
That branch was excluded from the accepted optimization. It retains the previous
general refresh followed by its Assembly override. Only Part-owned copies inside
an Assembly and nested Body Scale consume the new scene-producer path.
The experiment is not presented as an application-wide speed improvement.

## Final measurements and checks

Baseline `e6977ca2` plus the opt-in probe; Windows Release, Qt Fusion, serial
runs with no concurrent build/test work. Each row is the mean of trials 1–3
after trial 0, in the existing GUI scenario's current state. These are refresh
latencies, not dialog-opening or body-calculation times.

| Checkpoint | Before ms | Final ms | Publications before → final |
| --- | ---: | ---: | --- |
| Assembly Mirror create | 165.777 | 219.228 | 2 → 2 |
| Assembly Mirror edit | 219.914 | 289.016 | 2 → 2 |
| Assembly linear Pattern create | 253.044 | 276.296 | 2 → 2 |
| Assembly linear Pattern edit | 402.740 | 433.322 | 2 → 2 |
| Assembly circular Pattern create | 459.811 | 457.508 | 2 → 2 |
| Assembly circular Pattern edit | 550.995 | 504.195 | 2 → 2 |
| Part Mirror in Assembly | 806.569 | 437.850 | 2 → 1 |
| Part linear Pattern in Assembly | 719.308 | 479.014 | 2 → 1 |
| Part circular Pattern in Assembly | 731.581 | 650.956 | 2 → 1 |
| Body Scale in Assembly | 50.749 | 43.931 | 2 → 1 |

The first six rows use the original production path in the final build; their
variation illustrates the limits of these short GUI timings. The accepted four
cases remove one full intermediate scene publication. All forty corresponding
final framebuffer hashes match the baseline exactly. No timing threshold is
used as a functional test. Raw baseline, exploratory runs and final run:
[measurement logs](20261001-nested-copy-preview.txt).

The complete final derived-copy and Body Scale GUI suites passed in 179.80 s.
They cover source/Origin entry, exact nested occurrence filtering, point/plane
references, inspection, commit, save/reopen, Cancel, Undo and Body context as
applicable. Seven dependent gates also passed in 32.04 s: scene refresh scope,
Assembly refresh, selection-filter GUI, derived-copy save GUI, Body Scale model,
Pattern dimensions and five-language catalogs. The dependent gates ran before
the final narrowing, which restores the already-tested original Assembly-owned
copy path; both affected complete GUI suites were rerun after narrowing.

The local GUI was rebuilt (`build/copy-preview-scoped-build.log`), all temporary
production profiling is absent, and `git diff --check` passes. The root
`zima-cad.bat` is unchanged. No portable Windows release was produced.

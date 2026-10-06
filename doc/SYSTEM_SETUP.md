# System Setup

Tools > System Setup opens the shared internal settings window. General,
Templates and Desktop integration reuse ordinary settings controls and the
existing registration service. OK validates and saves; Cancel discards pending
changes. There is no Apply transaction.

A new installed portable root offers setup automatically. Completing or
explicitly skipping the initial offer records `Setup/Status` in shared
installation-root `config/config.ini`, outside version directories. Existing
configured roots and version changes do not repeat setup. Local development
uses the menu command without an automatic offer.

The metric preset selects mm, deg, kg, MPa, C and s with native templates
`START_PART_mm.prtz` and `START_ASSEMBLY_mm.asmz`. The inch preset selects
in, deg, lb, psi, F and s with `START_PART_in.prtz` and
`START_ASSEMBLY_in.asmz`. Custom native templates remain selectable. Every
template unit must match the selected defaults before OK can save. Drawing
paper and title blocks are selected separately in the drawing workspace.

Registration is optional and defaults to keeping the current registration.
Register/repair and removal use the same current-user desktop-integration
service as Global Settings. Failed confirmation restores previous settings;
Cancel never registers the application. Linux desktop acceptance must be
performed on Linux.

`zima_cpp_system_setup_ui_contract` exercises internal presentation,
metric/inch template selection, OK persistence, unchanged menu Cancel and
persisted first-launch skipping with isolated settings and registration.

# Research

## Project context

aemlpc is a from-scratch LPC game driver written in C++20. It already has a lexer, parser, bytecode compiler, VM, object system, and network stack for telnet, TLS, and WebSocket.

The bundled mudlib in `mudlib/` is intentionally small. It exists to prove the driver, the language core, and the in-game building tools, not to become a full third-party mudlib.

The current codebase targets the FluffOS dialect first and also supports LDMud. DGD is comparison only.

## Goal

The goal is a modern C++ LPC mud driver that can replace the practical role of MudOS, FluffOS, or LDMud for real mudlibs.

That means:

- boot real mudlibs without hand-tuning them for this driver
- match the behaviors those mudlibs rely on most
- preserve compatibility with existing LPC code where possible
- improve the runtime and implementation quality where the old drivers are weak

## Non-goal

This is not a DGD replacement effort. DGD is useful as a reference, but it is not the target.

## Current foundation check

The foundation looks good enough to continue research and compatibility work, but not good enough to declare victory yet.

What is already encouraging:

- clean build and passing tests
- TMI-2 boots to "Ready for connections"
- the driver can load real mudlib code paths

What still needs proof:

- repeated boots against more real mudlibs
- more coverage of dialect quirks and preprocessing edge cases
- better evidence that the current object loading and startup path work across multiple libraries, not just the bundled one

Recent probe results already show why this matters:

- RiftsMUD reached a real source-level parse error in its master object
- Nightmare Residuum exposed a real driver preprocessing bug, which was fixed, then progressed to a mudlib compile failure

So the foundation is promising, but it still needs broader reference data.

## Reference material to gather

### Drivers

Collect and compare:

- FluffOS, current and historical trees
- LDMud, current tree and relevant older releases
- MudOS or equivalent ancestry for legacy behavior

Use these as implementation references for:

- boot order
- master and simul_efun loading
- preprocessing and macro behavior
- efun semantics
- object lifecycle
- callouts, heartbeats, and connection handling

### Driver hierarchy

Primary baseline:

- FluffOS for modern mudlib compatibility

Secondary baseline:

- LDMud for dialect differences and feature gaps

Historical reference only:

- MudOS for boot order, efun ancestry, and edge-case behavior

DGD stays comparison only.

### Reference driver corpus

Start with these live references:

- FluffOS. Official repo: `https://github.com/fluffos/fluffos`
- LDMud. Official site: `https://www.ldmud.eu/`, repo mirror: `https://github.com/ldmud/ldmud`
- MudOS. Historical mirror with version branches: `https://github.com/maldorne/mudos`

Use FluffOS first for modern compatibility, LDMud second for dialect and feature differences, and MudOS for ancestry and edge-case behavior.

### Mudlibs

Collect and boot probe:

- TMI-2
- RiftsMUD
- Nightmare Residuum
- Dead Souls
- Lima
- any other well known FluffOS or LDMud mudlib with public source

### Reference mudlib corpus

Start with these:

- TMI-2, already booted once on aemlpc
- Dead Souls, modern FluffOS mudlib and a strong real-world baseline
- Lima, FluffOS-based and actively documented
- Nightmare Residuum, modern FluffOS and already useful for boot-failure discovery
- RiftsMUD, useful for real themed content and old-line compatibility quirks
- Nightmare 3, for the classic Nightmare lineage and historical behavior

Use these as compatibility references for:

- startup files
- include layout
- master and simul_efun conventions
- room, item, and NPC patterns
- command layout
- persistence expectations
- real-world parser and preprocessor quirks

### Priority order for probes

1. Expand TMI-2 coverage
2. Dead Souls
3. Lima
4. Nightmare Residuum and RiftsMUD
5. One LDMud-native mudlib for dialect contrast

## Compatibility status and gap classification

Already encouraging:

- clean build and tests
- TMI-2 reaches "Ready for connections"
- real mudlib code paths load
- the Nightmare Residuum preprocessing bug was real and fixable in the driver

Still needs proof:

- repeated successful boots of Dead Souls, Lima, Nightmare Residuum, and RiftsMUD
- broader dialect and preprocessing coverage
- object loading and startup behavior across multiple libraries
- callouts, heartbeats, and connection handling across real mudlibs
- persistence expectations and binary object handling

Gap heuristic:

- driver bugs are failures that appear across multiple mudlibs or stem from the driver preprocessing and object lifecycle
- mudlib assumptions are hard-coded paths, driver-specific conditionals, deprecated MudOS behavior, or library-specific master applies

The RiftsMUD master parse error and the Nightmare Residuum preprocessing failure are good examples of that split.

## Working recommendations

- Mirror FluffOS boot order, master and simul_efun loading, and preprocessor behavior first
- Keep an LDMud test matrix for coroutine, efun, and dialect differences
- Maintain a small reference mudlib corpus and automate boot probes
- Use public FluffOS mudlib collections and mudlibs.fluffos.info demos as regression oracles
- Track FluffOS testsuite and LDMud tests for language-core coverage

## Working conclusion

Keep going. The current foundation is valid for research and iterative compatibility work, but we need more boots, more drivers, and more mudlibs before we can claim the driver is ready to stand in for MudOS, FluffOS, or LDMud in practice.

# 2026-09-11 Driver Compatibility Session

## Goal

Establish the current kjdmud baseline with a clean manual build, then
boot and exercise external LPC mudlibs as compatibility probes.

## Current checkpoint

- Live repository: `/home/thurtea/kjdmud`, `main` at `f057c53`.
- Live remote: `https://github.com/thurtea/kjdmud`.
- Backup snapshots: AMLP at `89741e4`; crysis at `f814bfc` and one local
  commit ahead of its remote.
- Current driver binary: `build/kjdmud`. Test target:
  `build/test/kjdmud_tests`.
- Toolchain dependencies are installed: CMake 4.3.0, GCC 16.2.1, PCRE2
  10.47, SQLite 3.51.2, OpenSSL 3.5.8, and libcrypt 4.5.2.
- A clean configure, build, and test run has not yet been performed in
  this session.

## Compatibility matrix

| Order | Mudlib | Location or source | Purpose | Result |
|---|---|---|---|---|
| 1 | TMI-2 | `/home/thurtea/Documents/backups/amlp/temp/tmi2_fluffos_v3_extracted/tmi2_fluffos_v3/lib` | Known-good control | Pending |
| 2 | RiftsMUD | `https://github.com/grav1tyzero/RiftsMUD.git` | Rifts lineage probe | Pending |
| 3 | Nightmare Residuum | `https://github.com/michaelprograms/nightmare-residuum.git` | Modern Nightmare lineage probe | Pending |
| 4 | LPUniversity | `http://dead-souls.net/files/lpuni_fluffos_v1.zip` | Broader compatibility probe | Pending |

External trees go under gitignored `temp/` and are never copied into the
bundled `mudlib/`. Each test uses a minimal kjdmud adapter config, not
the upstream FluffOS or MudOS driver configuration.

## Known context

- TMI-2 and the archived AetherMUD tree were previously booted and
  exercised. This session must confirm that remains true after the
  kjdmud rename.
- The locally archived Dead Souls 3.8.2 copy is not in this four-target
  sequence. Its last recorded blocker is `status` being treated as an
  unconditional type keyword.
- No target has been booted in this session. Record only observed output
  and the first blocker for each target below.

## Results

Pending.

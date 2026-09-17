# Next Session Handoff: 2026-09-11

## Objective

Manually establish a clean aemlpc build baseline, then test external LPC
mudlibs as separate compatibility probes. Do not copy external code into
the bundled `mudlib/`.

## Current status

- Canonical repository: `/home/thurtea/aemlpc`, branch `main`, commit
  `f057c53`, remote `https://github.com/thurtea/aemlpc`.
- Historical backups:
  - AMLP: `/home/thurtea/Documents/backups/amlp`, commit `89741e4`.
  - crysis: `/home/thurtea/Documents/backups/crysis`, commit `f814bfc`,
    one commit ahead of its `origin/main`.
- The aemlpc rename is complete for the C++ namespace, include directory,
  executable (`build/aemlpc`), and test executable
  (`build/test/aemlpc_tests`). Some legacy AMLP names intentionally remain
  in console output, `AMLP_MAX_ITERATIONS`, comments, historical configs,
  and subsystem notes.
- The current `build/` directory was not verified in this session. Run a
  clean configure, build, and CTest before relying on it.
- Installed dependencies confirmed: CMake 4.3.0, GCC 16.2.1, PCRE2
  10.47, SQLite 3.51.2, OpenSSL 3.5.8, and libcrypt 4.5.2.
- Ports 1122, 1124, 1126, 1128, 4200, 4201, and 4202 were free when
  checked.
- Session documentation created but not staged: `docs/dev/TODAY.md` and
  this file.

## First commands to run

Run from a terminal:

```bash
cd /home/thurtea/aemlpc
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DAEMLPC_BUILD_TESTS=ON
cmake --build build --parallel "$(nproc)"
ctest --test-dir build --output-on-failure
```

Expected historical baseline: CTest reports `1/1` passing. The aggregate
test executable was last recorded at 922 checks. Do not claim that result
for the current checkout until the commands above complete.

## First external-mudlib probe: TMI-2

TMI-2 is already present locally and was previously booted and exercised.
Use it first as the control after the clean baseline:

```bash
cd /home/thurtea/aemlpc
TMI_ROOT=/home/thurtea/Documents/backups/amlp/temp/tmi2_fluffos_v3_extracted/tmi2_fluffos_v3/lib
sed "s|^mudlib_root:.*|mudlib_root: $TMI_ROOT|" etc/driver_tmi2.cfg > /tmp/aemlpc-tmi2.cfg
./build/aemlpc /tmp/aemlpc-tmi2.cfg 3
```

If this reaches `Ready for connections`, run it without `3` and connect
from another terminal:

```bash
cd /home/thurtea/aemlpc
./build/aemlpc /tmp/aemlpc-tmi2.cfg
```

```bash
nc 127.0.0.1 4201
```

Record the login flow and at least `look`, `inventory`, movement, and
`say`. Stop the driver with Ctrl-C.

## Remaining compatibility matrix

Run targets one at a time. Keep new source trees in gitignored
`temp/external/`.

| Order | Target | Source | Adapter status |
|---|---|---|---|
| 2 | RiftsMUD | `https://github.com/grav1tyzero/RiftsMUD.git` | Config values derived, ready after clone |
| 3 | Nightmare Residuum | `https://github.com/michaelprograms/nightmare-residuum.git` | Config values derived, ready after clone |
| 4 | LPUniversity | `http://dead-souls.net/files/lpuni_fluffos_v1.zip` | Inspect its bundled config after extraction |

Fetch them only after TMI-2 has a recorded result:

```bash
cd /home/thurtea/aemlpc
mkdir -p temp/external
git clone --depth 1 https://github.com/grav1tyzero/RiftsMUD.git temp/external/RiftsMUD
git clone --depth 1 https://github.com/michaelprograms/nightmare-residuum.git temp/external/nightmare-residuum
curl --fail --location http://dead-souls.net/files/lpuni_fluffos_v1.zip --output /tmp/lpuni_fluffos_v1.zip
unzip -q /tmp/lpuni_fluffos_v1.zip -d temp/external
```

### RiftsMUD adapter

Upstream layout was verified: root `mudlib/`, master `/adm/obj/master`,
simul efun `/adm/obj/simul_efun`, and global header in `adm/include`.
Create a temporary adapter and run a bounded boot probe:

```bash
cd /home/thurtea/aemlpc
printf '%s\n' \
  'mud_name: RiftsMUD' \
  'mudlib_root: temp/external/RiftsMUD/mudlib' \
  'master_file: /adm/obj/master' \
  'simul_efun_file: /adm/obj/simul_efun' \
  'include_dir: adm/include' \
  'global_include_file: <global.h>' \
  'dialect: fluffos' \
  'port: 4203' \
  'heartbeat_interval_ms: 2000' \
  'max_eval_cost: 10000000' \
  'max_string_length: 2000000' > /tmp/aemlpc-riftsmud.cfg
./build/aemlpc /tmp/aemlpc-riftsmud.cfg 3
```

### Nightmare Residuum adapter

Upstream layout was verified: root `lib/`, master
`/secure/daemon/master`, simul efun `/secure/sefun/sefun`, and headers in
`secure/include` and `include`.

```bash
cd /home/thurtea/aemlpc
printf '%s\n' \
  'mud_name: Nightmare Residuum' \
  'mudlib_root: temp/external/nightmare-residuum/lib' \
  'master_file: /secure/daemon/master' \
  'simul_efun_file: /secure/sefun/sefun' \
  'include_dir: secure/include:include' \
  'global_include_file: <global.h>' \
  'dialect: fluffos' \
  'port: 4204' \
  'heartbeat_interval_ms: 2000' \
  'max_eval_cost: 100000000' \
  'max_string_length: 256000' > /tmp/aemlpc-nightmare.cfg
./build/aemlpc /tmp/aemlpc-nightmare.cfg 3
```

## Rules for each result

1. Save the full first boot output.
2. If boot succeeds, run a live telnet login and record a short command
   transcript.
3. If boot fails, stop at the first source-level compiler or runtime
   blocker. Record the exact file, line, and error before changing code.
4. Fix only a confirmed driver compatibility issue. Add a focused
   regression test before retesting the mudlib.
5. Update `TODAY.md` with observed results, not predictions.

## Known separate probe

Dead Souls 3.8.2 is locally available at
`/home/thurtea/Documents/backups/amlp/temp/ds3.8.2_extracted/ds3.8.2/lib`.
It is not in the immediate four-target matrix. Its last recorded blocker
is that the lexer treats `status` as an unconditional type keyword, while
Dead Souls disables that optional legacy keyword.

# Setup98: status and handover

Reusable single-file installer kit for Win98 programs: manifest INI -> `pack98` -> `<name>-setup.exe` (generic VC6
stub `bin/setup98.exe` + appended compressed payload). Public repo **HenriVSL/setup98** (MIT), release **v1.1**.
Built 2026-11-10 by session 86box-d1 (overseer) with Opus sub-agents, from Pica 1.0's installer (`dev/editor/setup`).
Use and reference: [README.md](README.md). Workshop notes (VM commands, adoption steps for other sessions):
[AGENTS.md](AGENTS.md). History: [CHANGES](CHANGES).

## State (2026-11-10)
- **1.1 released**: commits 48c59f5 (1.0), 5004b5a (1.1 DisplayVersion), 3eb332b (public README, screenshots,
  self-contained tests + demo). GitHub release v1.1 carries `setup98-1.1.zip` (`tools/mkrelease.sh`, 23 files,
  deterministic).
- **Users**: Lähiö (86box-86, VM B) ships a Setup98 installer with each release since 1.2 RC2 (its manifest:
  `dev/lahio/dist/lahio-setup.ini`; it uses `DisplayVersion` for RCs with `Version=1.1.99.N`). The kit's own examples
  package Retro Strike (`examples/retrostrike.ini`) and Pica (`examples/pica.ini`, takes over a Pica 1.0 install);
  Pica itself still ships its own `editor/setup` installer.
- **Committed binaries**: `bin/setup98.exe`, `bin/pack98.exe`, `bin/s98test.exe`, `examples/demo/s98demo.exe`
  (the Mac route needs no VM build). Rebuilding only changes the PE timestamp; don't commit a rebuild unless sources
  changed.

## How to verify a change (owner checklist)
1. Mac: `make test` (strict clang + ASan/UBSan; codec, manifests, packer round trips). Also from a lone copy
   (`git ls-files -co --exclude-standard | rsync -a --files-from=- . /tmp/x/ && make -C /tmp/x test`): the repo must
   not need the sibling projects.
2. VM A under the lock: `w98 build setup98 clean` then `w98 build setup98`: 0 warnings.
3. Mac `tools/pack98` and VC6 `bin\pack98.exe` give byte-identical setups (`md5`) for `examples/demo.ini` and
   `examples/retrostrike.ini`.
4. Install test on VM A (AGENTS.md section 3): `/S` rc 0, `s98test md5` matches the source, shortcuts, uninstall
   `/S /PURGE`, `regedit /e` HKCR + `HKLM\Software` before/after identical (compare on the Mac, never `fc` in DOS).
5. Manifests without new keys must pack byte-identically to the previous version (compatibility promise).
6. Bump `src/version.h`, CHANGES, rebuild `bin/*.exe`, `tools/mkrelease.sh`, commit, push, `gh release create`.
   Tell the sessions that use the kit (Lähiö) about changes.

## Open / ideas (none requested)
- Linux/gcc build of pack98 not verified (no Linux host); README says untested.
- Wizard Browse button never exercised in tests; real hardware, Win95/ME/NT untested.
- Pica 1.1 could switch to Setup98 (`examples/pica.ini` is ready; takeover of a 1.0 install tested, the reverse,
  Pica 1.0's own setup over a Setup98 install, is not).
- Release zip has no `src/`: building a branded stub needs a repo checkout.
- Wizard window is not centred (Windows places it).

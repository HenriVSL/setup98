# Setup98: notes for the author's build setup (Claude sessions in the 86box workshop)

This file is for the Claude Code sessions that work in the author's Win98 workshop (`~/Documents/86box`, see its
`CLAUDE.md`). Everyone else: read [README.md](README.md); nothing here is needed to use or build Setup98.

The kit lives in `~/Documents/86box/Shared/dev/setup98` (= `M:\dev\setup98` in the VMs). It is a shared kit: read
it, don't edit it; ask its owner session (86box-d1, VM A) for changes. Full reference: `README.md` (Manifest
reference, Switches and exit codes, Upgrade rules, Testing checklist). Public repo: HenriVSL/setup98.

## 1. Add an installer to your project

You own a Win98 project in `~/Documents/86box/Shared/dev/<project>/` and want a `<project>-setup.exe`.

Manifest: copy `examples/hello.ini` (minimal), `examples/demo.ini` (complete, self-contained) or
`examples/retrostrike.ini` (a DirectX game) to e.g. `dev/<project>/dist/<project>-setup.ini` and edit it. All paths in
it are relative to the manifest's folder:
- `[Product]` `Id` (never change it later), `Name`, `Version` (or `from:<exe>` if your exe has a version resource),
  `MainExe`; for a release candidate add `DisplayVersion=1.2 RC2` with `Version=1.1.99.2` (numbers decide upgrades),
  and for a game `WindowClass=<your RegisterClass name>` so Setup won't replace files while it runs.
- `[Files]` `Source=` your staged release folder, `File=` per pattern (`art\*.bmp`, `data\**`), `Keep=` for default
  config files, `UserData=` for everything the game writes (saves, ini, logs), never shipped.
- `[Shortcuts]` `Group=`, `Link=Name | exe | args=-window`. `[Requires] DirectX=6`. `[Pack] OutFile=`.

## 2. Build it (pick one)

```sh
# Mac (no VM needed; uses the kit's prebuilt bin/setup98.exe; tools/pack98 runs make first):
~/Documents/86box/Shared/dev/setup98/tools/pack98 -check ~/Documents/86box/Shared/dev/<project>/dist/<project>-setup.ini
~/Documents/86box/Shared/dev/setup98/tools/pack98 ~/Documents/86box/Shared/dev/<project>/dist/<project>-setup.ini
# Win98, your own VM's agent (VM A: samba/w98, VM B: samba/vmb/w98), byte-identical result:
~/Documents/86box/samba/vmb/w98 sh 'M:\dev\setup98\bin\pack98.exe M:\dev\<project>\dist\<project>-setup.ini'
```
Errors name the manifest line. `pack98 -t <setup.exe>` verifies a built setup, `-l` lists it, `-x <dir>` extracts it.
pack98 only reads the kit; it writes only the output file.

Own icon (optional), on your VM, all output into your project (nothing is written into dev/setup98, so VM A and VM B
can build at the same time):
```sh
~/Documents/86box/samba/vmb/w98 sh 'mkdir M:\dev\<project>\obj' -t 30      # OBJDIR's parent must exist
~/Documents/86box/samba/vmb/w98 build setup98 stub 'ICON=M:\dev\<project>\res\game.ico' '"PRODUCT=<Name>"' 'OBJDIR=M:\dev\<project>\obj\stub' 'OUT=M:\dev\<project>\dist\stub.exe'
# then: tools/pack98 <manifest> -stub ~/Documents/86box/Shared/dev/<project>/dist/stub.exe   (or [Pack] Stub=stub.exe)
```
In zsh, wrap a definition with spaces as `'"PRODUCT=My Game"'` so NMAKE gets the double quotes.

## 3. Test it on your VM (take your VM's lock first: samba/vmlock or samba/vmb/vmlock)

```sh
W=~/Documents/86box/samba/vmb/w98      # or samba/w98 for VM A
$W sh 'mkdir C:\S98T' 'copy M:\dev\<project>\dist\out\<project>-setup.exe C:\S98T\setup.exe' 'copy M:\dev\setup98\bin\s98test.exe C:\S98T\s98test.exe'
$W sh 'start /w regedit /e C:\S98T\before.reg HKEY_CLASSES_ROOT'
$W run 'C:\S98T\setup.exe /S' -t 120; echo "rc=$?"          # 0 ok, 2 running, 3 restart, 4 newer installed, 5 disk
$W sh 'type C:\WINDOWS\TEMP\<LOGNAME>.LOG'                    # LogName: first 5 letters of Id + SET
$W sh 'C:\S98T\s98test md5 "C:\Program Files\<Name>"'        # compare with md5 of your release folder on the Mac
$W sh 'C:\S98T\s98test lnk "C:\WINDOWS\Start Menu\Programs\<Group>\<Link name>.lnk"'
$W start 'C:\S98T\setup.exe'                                  # wizard; screenshot with your VM's vnc98.sh capture
$W run '"C:\Program Files\<Name>\uninst.exe" /S /PURGE' -t 120; echo "rc=$?"
$W sh 'start /w regedit /e C:\S98T\after.reg HKEY_CLASSES_ROOT'   # w98 get both, cmp on the Mac
$W sh 'deltree /y C:\S98T'
```
Don't run a fullscreen game from the Finish page during tests (untick "Start now" with `vnc98.sh key alt-s`, or
`Run=0`). Kill what you started before releasing the lock. Compare big registry exports on the Mac: DOS `fc /b` on
multi-MB files leaves a hung DOS box. Running check: `w98 start 's98test.exe fakewin <WindowClass> 30'`, wait for
`s98test findwin`, then `setup.exe /S` must give exit 2. Uninstallers leave `%TEMP%\<6 letters>UN.EXE` plus a
`WININIT.INI` line until the next reboot; delete both (or reboot) when you clean up.

## 4. Ship

Put the `tools/pack98` line into your release script (after staging the release folder), ship
`<project>-<version>-setup.exe` next to your portable zip, and mention the switches (`/S`, `/D=`, `/UNINSTALL`) in
your README if users might want them.

## 5. Maintaining the kit (owner session)

- Build on VM A: `~/Documents/86box/samba/w98 build setup98` (clean: `... build setup98 clean`) writes the three
  `bin\*.exe`; 0 warnings at `/W3`. A rebuild without source changes differs only in the PE timestamp (2 bytes), so
  restore the committed binaries unless `src/`, `res/` or `tests/s98test.cpp` changed. Demo:
  `w98 build 'setup98\examples\demo\src'` writes `examples/demo/s98demo.exe`.
- Mac tests: `make test` (needs nothing outside the repo; the sibling release folders `../sandline/release/RetroStrike`
  and `../editor/release/Pica-1.0` are used as extra samples and round trips when they exist). Lone-clone check:
  copy `git ls-files -co --exclude-standard` into a scratch folder and run `make test` there.
- Mac/VC6 identity: `bin/pack98 examples/demo.ini -o out/demo-mac.exe` and
  `w98 sh 'M:\dev\setup98\bin\pack98.exe M:\dev\setup98\examples\demo.ini -o M:\dev\setup98\out\demo-vc6.exe'`,
  then `cmp`.
- Release zip: `tools/mkrelease.sh` -> `out/setup98-<version>.zip` (version from `src/version.h`, must match the top
  of `CHANGES`).
- Screenshots (`docs/screenshots`, made 2026-11-10 on VM A, under the VM A lock): raw captures with
  `samba/vnc98.sh capture x.png` (the VNC framebuffer is exactly 800 x 600; `shot86.sh` gives a scaled window with
  86Box chrome and can be stale when the window is on another Space). Wizards open at Windows' cascade position; drag
  the title bar (`vnc98.sh move X Y mousedown 1 move ... mouseup 1`) so the window sits at x=260, y=99, clear of the
  desktop icons. The Installing page is caught with `vnc98.sh key enter pause 0.15 capture a.png` on the Folder page.
  Crop with `python3 tools/cropshots.py <raw folder> docs/screenshots` (raw names in the script). Keep the Start
  menu's "Log Off <user>" line out of the crops.
- Real-world examples: `examples/retrostrike.ini` and `examples/pica.ini` (and `tests/rs-old.ini`, `tests/rs-rc.ini`)
  pack the sibling projects' release folders; they are read-only for this kit.

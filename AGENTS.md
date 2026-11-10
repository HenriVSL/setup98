# Setup98 for Claude sessions: add an installer to your project

You own a Win98 project in `~/Documents/86box/Shared/dev/<project>/` and want a `<project>-setup.exe`. You don't touch
`dev/setup98` (it's a shared kit: read it, don't edit it; ask session 86box-d1 for changes). Full reference:
`README.md` (sections 3 manifest keys, 5 switches, 7 upgrade rules, 11 testing).

## 1. Manifest (in your project)

Copy `examples/hello.ini` (minimal) or `examples/retrostrike.ini` (game) to e.g. `dev/<project>/dist/<project>-setup.ini`
and edit it. All paths in it are relative to the manifest's folder:
- `[Product]` `Id` (never change it later), `Name`, `Version` (or `from:<exe>` if your exe has a version resource),
  `MainExe`, and for a game `WindowClass=<your RegisterClass name>` so Setup won't replace files while it runs.
- `[Files]` `Source=` your staged release folder, `File=` per pattern (`art\*.bmp`, `data\**`), `Keep=` for default
  config files, `UserData=` for everything the game writes (saves, ini, logs), never shipped.
- `[Shortcuts]` `Group=`, `Link=Name | exe | args=-window`. `[Requires] DirectX=6`. `[Pack] OutFile=`.

## 2. Build it (pick one)

```sh
# Mac (no VM needed; uses the kit's prebuilt bin/setup98.exe):
~/Documents/86box/Shared/dev/setup98/tools/pack98 -check ~/Documents/86box/Shared/dev/<project>/dist/<project>-setup.ini
~/Documents/86box/Shared/dev/setup98/tools/pack98 ~/Documents/86box/Shared/dev/<project>/dist/<project>-setup.ini
# Win98, your own VM's agent (VM A: samba/w98, VM B: samba/vmb/w98), byte-identical result:
~/Documents/86box/samba/vmb/w98 sh 'M:\dev\setup98\bin\pack98.exe M:\dev\<project>\dist\<project>-setup.ini'
```
Errors name the manifest line. `pack98 -t <setup.exe>` verifies a built setup, `-l` lists it, `-x <dir>` extracts it.

Own icon (optional), on your VM, all output into your project (nothing is written into dev/setup98):
```sh
~/Documents/86box/samba/vmb/w98 sh 'mkdir M:\dev\<project>\obj' -t 30
~/Documents/86box/samba/vmb/w98 build setup98 stub 'ICON=M:\dev\<project>\res\game.ico' '"PRODUCT=<Name>"' 'OBJDIR=M:\dev\<project>\obj\stub' 'OUT=M:\dev\<project>\dist\stub.exe'
# then: tools/pack98 <manifest> -stub ~/Documents/86box/Shared/dev/<project>/dist/stub.exe   (or [Pack] Stub=stub.exe)
```

## 3. Test it on your VM (take your VM's lock first: samba/vmlock or samba/vmb/vmlock)

```sh
W=~/Documents/86box/samba/vmb/w98      # or samba/w98 for VM A
$W sh 'mkdir C:\S98T' 'copy M:\dev\<project>\dist\out\<project>-setup.exe C:\S98T\setup.exe' 'copy M:\dev\setup98\bin\s98test.exe C:\S98T\s98test.exe'
$W sh 'start /w regedit /e C:\S98T\before.reg HKEY_CLASSES_ROOT'
$W run 'C:\S98T\setup.exe /S' -t 120; echo "rc=$?"          # 0 ok, 2 running, 3 restart, 4 newer installed, 5 disk
$W sh 'type C:\WINDOWS\TEMP\<LOGNAME>.LOG'                    # LogName: first 5 letters of Id + SET
$W sh 'C:\S98T\s98test md5 "C:\Program Files\<Name>"'        # compare with md5 of your release folder on the Mac
$W sh 'C:\S98T\s98test lnk "C:\WINDOWS\Start Menu\Programs\<Group>\<Link name>.lnk"'
$W start 'C:\S98T\setup.exe'                                  # wizard; screenshot with your VM's shot86.sh
$W run '"C:\Program Files\<Name>\uninst.exe" /S /PURGE' -t 120; echo "rc=$?"
$W sh 'start /w regedit /e C:\S98T\after.reg HKEY_CLASSES_ROOT'   # w98 get both, cmp on the Mac
$W sh 'deltree /y C:\S98T'
```
(`s98test.exe` ships prebuilt in the kit's `bin\`.)
Don't run a fullscreen game from the Finish page during tests (untick "Start now", or `Run=0`). Kill what you
started before releasing the lock.

## 4. Ship

Put the `tools/pack98` line into your release script (after staging the release folder), ship
`<project>-<version>-setup.exe` next to your portable zip, and mention the switches (`/S`, `/D=`, `/UNINSTALL`) in
your README if users might want them.

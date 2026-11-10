# Setup98

**Setup98** makes a single-file Windows 98 installer, `<name>-setup.exe`, for any program in this workshop.
You describe the installer in a small INI file (the **manifest**) and run `pack98` on it. You write no installer
code and don't recompile anything. It is all our own code, including the compressor. Setup98 is the Pica 1.0
installer turned into a generic engine.

What the installer does: a Win98 wizard (Welcome, License, Folder and options, File types, Installing, Finish),
silent mode, in-place upgrade, repair and downgrade detection, a disk-space check, a requirements check (DirectX,
Windows version), shortcuts, a "program is running" check, Pica-grade file associations, an uninstaller in Add/Remove
Programs, files-in-use handling through WININIT.INI, and a log. Payloads are compressed (LZ77 + Huffman) and every
file is CRC-checked before anything is replaced.

```
manifest.ini + your files --pack98--> setup98.exe + [manifest | compressed files | trailer] = mygame-1.0-setup.exe
```

Setup98 1.1. For the history, see `CHANGES`. For the short "do this" guide for Claude sessions, see `AGENTS.md`.

---

## 1. Quick start (5 minutes)

### 1.1 Write a manifest

Put it in **your** project, e.g. `dev/mygame/dist/mygame-setup.ini`. Paths in it are relative to the manifest's own
folder, wherever the kit is.

```ini
[Product]
Id=MyGame                      ; registry key name: letters, digits, _ and -
Name=My Game
Version=1.0                    ; or Version=from:mygame.exe (reads the exe's version resource)
MainExe=mygame.exe

[Files]
Source=../release/MyGame       ; folder that holds the files to install
File=mygame.exe
File=README.TXT
File=art\*.bmp
File=sfx\*.wav
Keep=mygame.ini                ; default settings: installed only if missing
UserData=*.sav                 ; written by the game: deleted only when the user asks

[Pack]
OutFile=../release/mygame-1.0-setup.exe
```

That's a complete manifest. Start menu shortcut, uninstaller, folder `C:\Program Files\My Game` and the log come by
default. The full game example is `examples/retrostrike.ini`; the app example with file types is `examples/pica.ini`;
the smallest template is `examples/hello.ini`.

### 1.2 Build the setup: route A, on the Mac (recommended)

```sh
~/Documents/86box/Shared/dev/setup98/tools/pack98 ~/Documents/86box/Shared/dev/mygame/dist/mygame-setup.ini
# check only (validate, list the files, write nothing):
~/Documents/86box/Shared/dev/setup98/tools/pack98 -check ~/Documents/86box/Shared/dev/mygame/dist/mygame-setup.ini
```
`tools/pack98` builds the Mac `bin/pack98` the first time (`make`, 2 s) and uses the kit's prebuilt stub
`bin/setup98.exe`. It needs no VM. Put this line into your `tools/mkzip.sh` or `package.sh`.

### 1.3 Build the setup: route B, inside Win98 (VC6 machine)

`bin\pack98.exe` is the same packer built by VC6. It makes **byte-identical** output. Run it through your VM's agent:

```sh
# VM A
~/Documents/86box/samba/w98 sh 'M:\dev\setup98\bin\pack98.exe M:\dev\mygame\dist\mygame-setup.ini'
# VM B
~/Documents/86box/samba/vmb/w98 sh 'M:\dev\setup98\bin\pack98.exe M:\dev\mygame\dist\mygame-setup.ini'
```
pack98 only reads `M:\dev\setup98`; it writes only the output file you name.

### 1.4 Test it in your VM (hold your VM's lock)

```sh
W=~/Documents/86box/samba/w98            # VM B: W=~/Documents/86box/samba/vmb/w98
$W sh 'mkdir C:\S98T' 'copy M:\dev\mygame\release\mygame-1.0-setup.exe C:\S98T\setup.exe'
$W run 'C:\S98T\setup.exe /S' -t 120; echo "rc=$?"                 # silent install, 0 = ok
$W sh 'type C:\WINDOWS\TEMP\MYGAMSET.LOG'                           # the log (name: section 6)
$W sh 'dir /s "C:\Program Files\My Game"'
$W run '"C:\Program Files\My Game\uninst.exe" /S /PURGE' -t 120; echo "rc=$?"   # remove everything
$W sh 'deltree /y C:\S98T'
```
For the wizard, run `$W start 'C:\S98T\setup.exe'` and take a screenshot with your VM's `shot86.sh`. The full
checklist is in section 11.

### 1.5 Optional: your own icon

See section 9. One `nmake` line on your VM builds a branded stub into your own project folder. Then pack with
`-stub`.

---

## 2. Example manifests

### 2.1 A game (`examples/retrostrike.ini`, packages `dev/sandline/release/RetroStrike` read-only)

```ini
[Product]
Id=RetroStrike
Name=Retro Strike
Version=1.0.2                         ; RETROSTR.EXE has no version resource
; a release candidate would add: DisplayVersion=1.0.2 RC2 with Version=1.0.1.99 (see tests/rs-rc.ini)
Publisher=Henri Lavikainen
MainExe=RETROSTR.EXE
Folder=%ProgramFiles%\Retro Strike
Description=Retro Strike is a 5 v 5 top-down tactical shooter ...\n\nNeeds DirectX 6 or later ...
Readme=README.TXT                     ; Finish page: "View the readme"
License=LICENSE                       ; License page
LicenseAgree=0                        ; MIT: show it, no "I accept" needed
Run=1                                 ; Finish page: "Start Retro Strike now"
WindowClass=Sirocco98                 ; running check: the game's window class
DataName=your settings, saved scores and logs
URL=https://github.com/HenriVSL/retro-strike

[Files]
Source=../../sandline/release/RetroStrike
File=RETROSTR.EXE
File=README.TXT
File=LICENSE
File=art\*.bmp
File=sfx\*.wav
File=data\**
Keep=retrostr.ini                     ; default config: never overwritten
UserData=*.log
UserData=bench.txt
UserData=shot.bmp

[Shortcuts]
StartMenu=1
Desktop=0
Group=Retro Strike                    ; Programs\Retro Strike\...
Link=Retro Strike | RETROSTR.EXE | desc=5 v 5 tactical shooter
Link=Retro Strike (window) | RETROSTR.EXE | args=-window | desc=Retro Strike in a window
Link=Read me | README.TXT
Link=Uninstall Retro Strike | uninst.exe
DesktopLink=Retro Strike | RETROSTR.EXE | desc=5 v 5 tactical shooter

[Requires]
DirectX=6
Windows=98

[Pack]
OutFile=../out/retrostrike-1.0.2-setup.exe
```

### 2.2 An app with file types (`examples/pica.ini`, packages `dev/editor/release/Pica-1.0` read-only)

```ini
[Product]
Id=Pica                               ; same Id as Pica's own setup: takes over a Pica 1.0 install (section 12)
Name=Pica
Version=from:pica.exe
MainExe=pica.exe
Description=Pica is a text and hex editor for Windows 98: ...
Readme=README.TXT
WindowClass=PicaFrame
DataName=your Pica settings, the saved session and the backups of unsaved changes

[Files]
Source=../../editor/release/Pica-1.0
File=pica.exe
File=README.TXT
File=LICENSE.TXT | optional           ; ship it if it exists
UserData=pica.ini
UserData=session1.ini
UserData=session2.ini
UserData=backup\**
UserData=%AppData%\Pica\pica.ini      ; Pica falls back to Application Data\Pica ...
UserData=%AppData%\Pica\session?.ini
UserData=%AppData%\Pica\backup\**
UserData=%WinDir%\Pica\pica.ini       ; ... or <windows>\Pica
UserData=%WinDir%\Pica\session?.ini
UserData=%WinDir%\Pica\backup\**
Generated=Pica Help.md                ; regenerated by Pica: every uninstall deletes it
Generated=~pica.tst
Generated=%AppData%\Pica\Pica Help.md
Generated=%AppData%\Pica\~pica.tst
Generated=%WinDir%\Pica\Pica Help.md
Generated=%WinDir%\Pica\~pica.tst

[Shortcuts]
StartMenu=1
Desktop=0
Group=                                ; empty: Programs\Pica.lnk directly
Link=Pica | pica.exe | desc=Pica text and hex editor

[Types]
Group=Text files | .txt* .log* .ini* .nfo                  ; * = ticked by default
Group=Web pages (now in your browser) | .htm .html .xml
Group=Source code | .c* .cpp* .h* .hpp .rc .asm* .py* .css .md* .mak* .diff
Group=Scripts and setup files | .bat .cmd .reg .inf .vbs .js | edit | note=Double-click still runs these; Pica becomes their Edit command.
LegacyProgId=Pica.Document            ; ProgID of the first Pica 1.0 setups: migrated

[ContextMenu]
Text=Open with &Pica
Default=1

[Pack]
OutFile=../out/pica-1.0-setup.exe
```

---

## 3. Manifest reference

General syntax:
- `[Section]` and `Key=Value` lines. Section and key names ignore case. A line starting with `;` or `#` is a
  comment. **There are no comments at the end of a line**, because values may contain `;`. (The `;` annotations in
  the examples above are for reading only; the real files in `examples/` keep comments on their own lines.)
- Blanks around keys and values are trimmed. Any line ending works; CRLF is preferred, as for every Win98 text file.
- **Encoding:** ASCII or UTF-8 (a BOM is fine). Text values ("Lähiö", “quotes”, €) are converted to Windows-1252,
  Win98's ANSI code page. A file that isn't valid UTF-8 is taken as Windows-1252 already (Notepad on Win98). A
  character with no Windows-1252 equivalent is an error. **Paths, patterns and Ids must be plain ASCII.**
- Validation is strict. An unknown section or key, a duplicate single key or a bad value stops pack98 with
  `pack98: <manifest>: line N: <message>`. Cross-checks that need the file list come after the files are collected
  (e.g. "MainExe x.exe is not one of the packed files").
- Keys marked *list* may appear several times; their order matters only in `[Files]`.

### [Product] (required)

| Key | Default | Meaning and validation |
|---|---|---|
| `Id` | **required** | Registry name: `HKLM\Software\<Id>`, `...\Uninstall\<Id>`, ProgIDs `<Id>.<ext>`, mutex names. 1-40 characters, letters, digits, `_`, `-`. Never change it between versions (that would be a different product). |
| `Name` | **required** | Display name (wizard, shortcuts, Add/Remove). A valid file name (no `\ / : * ? " < > \|`), at most 80 characters. |
| `Version` | **required** | `1`, `1.0`, `1.0.2` or `1.2.3.4` (each part 0-65535), or `from:<file>`: the VS_FIXEDFILEINFO file version of a packed file (an error if it has none). Used for upgrade, repair and downgrade detection and stored in the registry. |
| `DisplayVersion` | none (= `Version`) | Free text people see instead of `Version`: wizard titles and texts, Add/Remove (`DisplayName "<Name> <DisplayVersion>"`, `DisplayVersion`), uninstall messages, the log, pack98's summary. Plain ASCII, 1-40 characters. **Comparisons still use only the numeric `Version`**, so give a release candidate a number below the final one: `Version=1.1.99.2` + `DisplayVersion=1.2 RC2`; the final `Version=1.2` then upgrades it ("Upgrade X 1.2 RC2 -> 1.2"). The default `OutFile` name uses the numeric version. (Setup98 1.1) |
| `Publisher` | none | Add/Remove `Publisher`. |
| `MainExe` | **required** | The program, relative to the install folder. Must be a packed `.exe`. Used for the running check, Run now, the default shortcut, the Add/Remove icon and the file-type command. |
| `Folder` | `%ProgramFiles%\<Name>` | Default install folder: `%ProgramFiles%\...` (the registry's ProgramFilesDir), `%WinDrive%\...` (the Windows drive, e.g. `%WinDrive%\Games\X`) or an absolute `C:\...`. At most 200 characters, not a drive root. Browse appends the last folder name. |
| `Description` | "This wizard installs ..." | Welcome page text, at most 400 characters; `\n` is a line break. |
| `Readme` | none | A packed file; the Finish page offers "View the readme" (opens it with its program). |
| `License` | none | A packed text file shown on a License page. |
| `LicenseAgree` | `1` | `1`: Next stays disabled until "I accept" is ticked. `0`: the license is only shown. Ignored without `License`. |
| `Run` | `1` | `1`: the Finish page offers "Start <Name> now" (ticked). `0`: no such box. `/RUN` starts it after a silent install either way. |
| `RunArgs` | none | Arguments for Run now and `/RUN` (e.g. `-window`). |
| `WindowClass` | none | Running check: a top-level window of this class means the program runs. |
| `WindowTitle` | none | Running check: exact window title (with `WindowClass` both must match). |
| `Uninstaller` | `uninst.exe` | Uninstaller file name in the install folder, an 8.3 `.exe` name. The install record is the same name with `.ini`. Don't pack files with these names. |
| `DataName` | `your settings and saved data` | Wording of the uninstall question "Also delete <DataName>?". |
| `LogName` | first 5 letters/digits of `Id` + `SET` | Log file `%TEMP%\<LogName>.LOG` (8.3: 1-8 letters, digits, `_`, `-`). |
| `URL` | none | Add/Remove `URLInfoAbout`. |

Also the running check always tests whether `<install folder>\<MainExe>` is in use: an exe Windows has loaded can't
be opened for writing.

### [Files] (required)

Lines are processed in order:

| Key | Meaning |
|---|---|
| `Source=<folder>` (*list*) | Folder that the following `File=` and `Keep=` lines read from: relative to the manifest's folder, or absolute. `/` and `\` both work. Default: the manifest's folder. Change it again for files from elsewhere. |
| `File=<pattern> [\| optional] [\| as=<name>]` (*list*) | Program files: installed every time and **replaced on upgrade**. The installed path is the file's path relative to `Source`. `optional`: no error if nothing matches. `as=<path>` installs a single file under another name or path (`File=LICENSE \| as=docs\LICENSE.TXT`). |
| `Keep=<pattern> [\| optional] [\| as=<name>]` (*list*) | Default config: installed **only if missing**, never overwritten. A plain uninstall deletes it only if it is unchanged (same CRC as installed); a purge always does. If a file matches both, Keep wins over File. |
| `Exclude=<pattern>` (*list*) | Files to leave out, matched against the path relative to their `Source` (applies to every File/Keep line). |
| `UserData=<pattern>` (*list*) | Files the program writes (saves, settings, logs, scores). **Never shipped** (packing one is an error). Deleted **only** by a purge (uninstall answered "Yes, delete", or `/PURGE`). Their existence triggers the uninstall question. |
| `Generated=<pattern>` (*list*) | Files the program recreates by itself (help text, probes, caches): deleted by **every** uninstall, so they never keep the folder alive. |

**Patterns** (case-insensitive; `\` or `/` between folders):

| Pattern | Matches |
|---|---|
| `README.TXT` | exactly that path |
| `*.bmp` | `.bmp` files at the top level only (`*` stays inside one folder) |
| `art\*.bmp` | `.bmp` files directly in `art` |
| `art\**` | everything below `art`, any depth |
| `**\*.log` | `.log` files in any folder, including the top level (`**\` = zero or more folders) |
| `session?.ini` | `?` = one character |

`UserData` and `Generated` patterns are relative to the install folder, or start with `%AppData%\` (the user's
Application Data) or `%WinDir%\`. After a purge, empty folders below the install folder and the first folder after
`%AppData%\X` / `%WinDir%\X` are removed. pack98 skips files whose names start with `.` (`.DS_Store`) and
`Thumbs.db`. Packed file names must be plain ASCII, at most 200 characters including folders, at most 20000
files of at most 256 MB each.

### [Shortcuts] (optional)

| Key | Default | Meaning |
|---|---|---|
| `StartMenu` | `1` | Wizard checkbox: `1` ticked, `0` offered unticked, `off` no Start menu shortcuts at all. |
| `Desktop` | `0` | The same for desktop shortcuts. |
| `Group` | empty with one link, else `Name` | Folder under Start menu\Programs; empty = the links go directly into Programs. Setup creates it and removes it when it is empty again. |
| `Link=<name> \| <target> [\| args=..] [\| desc=..] [\| icon=<file>[,n]]` (*list*) | `<Name> \| <MainExe>` | A Start menu shortcut. `<target>` is a packed file or the uninstaller (`uninst.exe`), relative to the install folder. The working folder is the install folder. `.exe` targets show their own icon. `icon=` takes a packed file and index. Names must be unique. |
| `DesktopLink=...` (*list*) | the first `Link` | Desktop shortcuts, same syntax. |

### [Requires] (optional)

| Key | Meaning |
|---|---|
| `DirectX=<n>` | Minimum DirectX major version 1-9. Read from `HKLM\Software\Microsoft\DirectX\Version` (`4.06.xx` = 6, `4.07` = 7; `4.04` = 3). Without that value, `ddraw.dll` present means "unknown version". |
| `Windows=95\|98\|98SE\|ME` | Minimum Windows (98 SE = build 2222 or later). NT-based Windows counts as newer. |

When a requirement isn't met, the wizard warns ("Retro Strike needs DirectX 6 or later. This computer has DirectX 5.
Install it first, or continue anyway?"; default No). Silent mode logs the warning and continues.

### [Types] (optional: file associations, Pica semantics)

| Key | Default | Meaning |
|---|---|---|
| `Group=<title> \| .ext .ext* ... [\| edit] [\| note=<text>]` (*list*) | **at least one** | A box on the File types page. `*` after an extension = ticked by default. `edit` = script types: Windows' `open` stays (double-click still runs them) and the program becomes their `edit` verb (if nothing runs the type, it is opened instead). `note=` is one line of text in the box. At most 8 groups and 48 extensions, and they must fit on the page (pack98 checks). |
| `Intro` | "Ticked types open in <Name> when you double-click them. ..." | The line under the page title. |
| `Icon` | `<MainExe>,0` | DefaultIcon of the ProgIDs (a packed file, optional index). |
| `EditVerb` | `&Edit with <Name>` | Name of an `edit` verb Setup has to add. |
| `LegacyProgId` | none | An older shared ProgID of your product, treated as yours and deleted (Pica: `Pica.Document`). |

Each ticked type gets its own ProgID `<Id>.<ext>`. It copies the previous ProgID's name ("Text Document") and all
its other verbs (print, printto, "Open with MSDev" with its DDE keys), so Explorer's Type column, the New menu
(`.ext\ShellNew` is never touched) and Print stay as they were. Only `open` and the icon change. Everything Setup
replaces is saved under `HKLM\Software\<Id>\Previous\...` and restored on uninstall, but only while the entry still
points at us. Keys Setup created are deleted once they are empty again.

### [ContextMenu] (optional: "Open with <Name>" on every file)

| Key | Default | Meaning |
|---|---|---|
| `Text` | `Open with &<Name>` | Menu text (`HKCR\*\shell\<Id>`). |
| `Default` | `1` | Wizard checkbox ticked by default. |

The section's presence enables the option (an empty `[ContextMenu]` is fine).

### [Pack] (pack98 only; never in the setup)

| Key | Default | Meaning |
|---|---|---|
| `OutFile` | `<id>-<version>-setup.exe` next to the manifest | Output path, relative to the manifest. `-o` overrides. |
| `Stub` | see below | Stub exe (a branded one, section 9). |

pack98 looks for the stub in this order: `-stub <file>`, `[Pack] Stub=`, environment `SETUP98_STUB`,
`setup98.exe` next to the pack98 binary (`bin/`).

---

## 4. pack98 command line

```
pack98 <manifest.ini> [-o <out.exe>] [-stub <setup98.exe>] [-q] [-v]   build (-v: per-file sizes and filters)
pack98 -check <manifest.ini>     validate and list the files; writes nothing
pack98 -l <setup.exe>            list a setup's files (size, packed size, filter, keep flag)
pack98 -t <setup.exe>            test: payload CRC, decompress every file, check every file CRC
pack98 -x <setup.exe> <dir>      extract the files (and the canonical manifest.ini)
pack98 -m <setup.exe>            print the embedded canonical manifest
```
Exit codes: 0 ok, 1 error, 2 usage. The output never depends on the host: the Mac and VC6 builds produce the same
bytes (tested), so a release can be packed anywhere.

---

## 5. Switches and exit codes of the setup

| Switch | Meaning |
|---|---|
| `/S` | silent: no UI; the defaults, or the stored choices of the installed copy |
| `/D=<folder>` | install folder; must be the **last** switch; no quotes needed (`/D=C:\Games\My Game`) |
| `/STARTMENU=0\|1`, `/DESKTOP=0\|1` | shortcut options |
| `/CONTEXT=0\|1` | "Open with" option (`[ContextMenu]` only) |
| `/TYPES=<list>` | `default`, `none`, `all`, a list `.txt,.c` (replaces the set) or `+.bat,-.ini` (changes it) (`[Types]` only) |
| `/RUN` | start the program after a silent install |
| `/FORCE` | silent downgrade allowed |
| `/LOG=<file>` | log file (default `%TEMP%\<LogName>.LOG`) |
| `/UNINSTALL` | uninstall (the installed `uninst.exe` always does); with `/S` silent; `/PURGE` also deletes UserData |
| `/_LOCKTEST` | test only: holds the old files open so the WININIT.INI path runs |

Exit codes: **0** ok, **1** error / bad switch / cancelled, **2** the program is running (silent mode never waits and
never kills it), **3** done but restart needed (files in use), **4** a newer version is installed (silent without
`/FORCE`), **5** not enough disk space (silent). `uninst.exe` returns the code of its temp copy. An unknown switch
shows the switch list.

---

## 6. What gets installed where

| Where | What |
|---|---|
| `<install folder>\...` | the packed files (with their folders) |
| `<install folder>\uninst.exe` | the uninstaller: the stub plus the manifest, no files (about 120 KB) |
| `<install folder>\uninst.ini` | the **install record**: the canonical manifest plus `[InstalledFiles]` (kind `p` program / `k` keep, CRC, path), `[InstalledDirs]` (folders Setup created), `[InstalledLinks]` (shortcuts; `>` = a folder Setup created), `[InstalledRenames]` (see section 8). The uninstaller and the next upgrade use it. |
| `HKLM\Software\<Id>` | `InstallDir`, `Version` (numeric), `DisplayVersion` (only when the manifest has it), `StartMenu`, `Desktop` (DWORD choices), `Files` (program files, `;`, only when it fits in 8 KB), `StartMenuLnk`, `DesktopLnk` (first shortcut paths), plus `Types` and `ContextMenu` when the manifest has them, and `Previous\Open\|Edit\|Created` for associations |
| `HKLM\...\CurrentVersion\Uninstall\<Id>` | `DisplayName "<Name> <shown version>"`, `DisplayVersion` (the shown version: `DisplayVersion` if set, else `Version`), `UninstallString`, `InstallLocation`, `DisplayIcon`, `NoModify`, `NoRepair`, and `Publisher`, `URLInfoAbout` when given |
| Start menu, desktop | the shortcuts (`Programs\<Group>\<name>.lnk`, `Desktop\<name>.lnk`) |
| `HKCR\...` | `[Types]` and `[ContextMenu]` entries (section 3) |
| `%TEMP%\<LogName>.LOG` | the log of the last run: every file, registry change and decision (`RETROSET.LOG`, `PICASET.LOG`) |
| `%TEMP%\<6 letters>UN.EXE` | the uninstaller's temp copy, deleted at the next restart (`RETROSUN.EXE`) |

---

## 7. Upgrade, repair, downgrade and uninstall rules

Setup compares its numeric version with the installed one: `HKLM\Software\<Id>\Version`, else the installed
MainExe's version resource. Texts show the installed copy's stored `DisplayVersion` (Setup98 1.1+), else its numeric
version, so an install made by a 1.0 stub reads as before.

Release candidates: `Version=1.1.99.2` + `DisplayVersion=1.2 RC2`, then the final `Version=1.2` (no `DisplayVersion`
needed). The wizard says "Upgrade Lähiö 1.2 RC2 -> 1.2"; the final install removes the RC's stored display text.

- **Fresh install**: the user picks the folder.
- **Upgrade** (newer), **repair** (same) and **downgrade** (older; the wizard asks first, silent mode refuses with
  exit 4 unless `/FORCE`): the folder is fixed to the installed one (uninstall to move it; `/D` to another folder is
  refused). The stored choices (shortcuts, context menu, types) are the wizard's defaults.
- **Program files** (`File=`) are replaced every time. All new files are first written next to their targets as
  `~S98nnnn.TMP` and checked against their CRC. A damaged setup or a full disk changes nothing (the temp files and
  any new folders are removed). Then they are swapped in.
- **Keep files** are installed only if missing; an existing one is never touched.
- **Obsolete files**: program files the old version installed (its record, or for a Pica 1.0 install the registry
  `Files` value) that the new version doesn't ship are deleted, and folders that become empty are removed. Old keep
  files and anything matching the new `UserData` are never deleted. They stay recorded, so a later purge finds them.
- **UserData** files are never shipped, never replaced and never deleted except by a purge.
- **Shortcuts** are rebuilt from the new manifest; shortcuts the new version no longer has are deleted.
- **Plain uninstall** deletes program files, unchanged keep files, Generated files, the record, shortcuts, registry
  entries and the folders Setup created (when empty). If UserData or changed keep files exist it first asks "Also
  delete <DataName>?" (default **No**). If they are kept, the folder stays and the final message says so.
- **Purge** (`Yes` or `/PURGE`) also deletes UserData, changed keep files and empty folders, so the folder goes. Files
  the user created that match no pattern are never deleted.
- The installed `uninst.exe` copies itself to `%TEMP%`, runs the copy and waits; the copy deletes `uninst.exe`
  afterwards and schedules its own deletion in WININIT.INI. The same flow as Pica.

---

## 8. Files in use, restarts and long names

A file that can't be deleted (Windows has it loaded) stays as `~S98nnnn.TMP` and gets two `WININIT.INI [rename]`
lines (`NUL=<old>`, `<old>=<temp>`, 8.3 paths). Win9x replaces it at the next start. Setup then asks to restart
(silent: exit 3). WININIT.INI only knows 8.3 names, so the file would come back as `PLANT_~1.WAV` instead of
`plant_start.wav`. **Setup98 fixes this**: it records the pairs in `[InstalledRenames]` and registers a RunOnce
command (`uninst.exe /_FIXNAMES`). After the restart that command renames the files back to their long names (tested:
13 long names restored). Names that are already 8.3 come back in capitals (`DECALS.BMP`), which is harmless.

---

## 9. Your own icon (branded stub)

The default stub has a neutral "box with an arrow" icon and version strings "Setup98 installer". To show your
game's icon in Explorer and in the wizard, build your own stub **on your VM** from the kit sources. All output goes
to `OBJDIR` and `OUT` in your project: nothing is written inside `dev/setup98`, so sessions on VM A and VM B can
build at the same time.

```sh
# VM A: ~/Documents/86box/samba/w98      VM B: ~/Documents/86box/samba/vmb/w98
~/Documents/86box/samba/vmb/w98 sh 'mkdir M:\dev\mygame\obj' -t 30      # OBJDIR's parent must exist
~/Documents/86box/samba/vmb/w98 build setup98 stub 'ICON=M:\dev\mygame\res\game.ico' '"PRODUCT=My Game"' '"DESC=My Game setup"' 'OBJDIR=M:\dev\mygame\obj\stub' 'OUT=M:\dev\mygame\dist\mygame-stub.exe'
~/Documents/86box/Shared/dev/setup98/tools/pack98 mygame-setup.ini -stub ../dist/mygame-stub.exe    # or [Pack] Stub=
```
Variables: `ICON` (a BMP-format `.ico`; 16 and 32 px, 16 or 256 colours; PNG icons don't work on Win98), `PRODUCT`
(ProductName), `DESC` (FileDescription, default "<PRODUCT> installer"), `COMPANY`, `OBJDIR` (default `obj`), `OUT`.
A value with spaces must reach NMAKE in double quotes around the whole definition (`"PRODUCT=My Game"`; in zsh
wrap that in single quotes as shown). Avoid `& < > | ^ "` inside the values. The uninstaller it installs carries the same icon. `tools/mkicon.py` shows
how the default icon is drawn (Pillow plus a hand-written ICO writer).

---

## 10. Building the kit itself

| Output | Built by | Committed? |
|---|---|---|
| `bin/setup98.exe` (stub, about 120 KB) | VC6: `~/Documents/86box/samba/w98 build setup98` | **yes**: the Mac route needs it without a VM build |
| `bin/pack98.exe` (Win98 packer, about 90 KB) | VC6, same command | **yes** |
| `bin/pack98` (Mac packer) | `make` on the Mac (`tools/pack98` does it on demand) | no (built in 2 s, host-specific) |
| `bin/s98test.exe` (VM test helper) | VC6, same command | **yes**: other projects test without building in the kit |
| `obj/codectest.exe` | `w98 build setup98 codectest` | no |

`Makefile` is the NMAKE file (CRLF), `GNUmakefile` the Mac one (`make` picks it). Rebuild and recommit the two
`bin\*.exe` after changing `src/`. VC6 builds at `/W3` with zero warnings. Mac tests: `make test` (strict
`-std=c++98 -pedantic -Wall -Wextra -Wshadow -Werror` with ASan and UBSan: codec round trips, corrupt input,
manifest corpus, pack98 errors, example round trips); `make fuzz FUZZ=20000` for a longer codec fuzz run.

Sources: `src/setup.cpp` (command line, wizard, flows), `src/inst.cpp` (install, upgrade, uninstall work),
`src/assoc.cpp` (file types, context menu), `src/payload.cpp` (own payload, uninstaller, install record),
`src/util.cpp` (paths, log, registry, WININIT), and the portable code shared with pack98: `src/manifest.cpp`
(parser, validator, canonical writer, File types layout), `src/lzhenc.cpp` / `src/lzhdec.cpp` (codec),
`src/crc32.cpp`, `src/wild.cpp` (patterns), `src/pformat.h` (payload format), `src/pack98.cpp`.

---

## 11. Testing checklist (for your package)

Do this on your own VM, under its lock. The helper `M:\dev\setup98\bin\s98test.exe` (shipped prebuilt) gives MD5 lists, shortcut contents, fake windows and file locks.

1. `tools/pack98 -check` lists exactly the files you expect; `pack98 -t <setup>` says "all CRCs ok".
2. Registry before: `start /w regedit /e C:\S98T\b_hkcr.reg HKEY_CLASSES_ROOT` and the same for
   `HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Uninstall`. **Use `start /w`**: regedit is a GUI
   program and command.com doesn't wait for it otherwise. Compare large exports on the Mac (`w98 get`, `cmp`): DOS `fc /b`
   fails on multi-MB files and leaves a hung DOS box.
3. Silent install: `setup.exe /S` returns 0. Read the log. `s98test md5 "C:\Program Files\X"` must match the source
   (compare with `md5` on the Mac). Check the shortcuts with `s98test lnk "<path>.lnk"` (target, arguments, folder).
4. Wizard install: `w98 start`, screenshots with `shot86.sh`, keyboard over VNC (`vnc98.sh key enter` = Next,
   `key alt-s` toggles "Start now"). Untick "Start now" for fullscreen games, or start them with `-window`.
5. Upgrade: change a keep file, add a UserData file, run the setup again (repair). Both survive. Build an older test
   package (`Version=0.9`, plus an extra file, see `tests/rs-old.ini`), install it, then the new one: the extra file
   goes, your edits stay. Running the old one again gives exit 4.
6. Running check: `w98 start 's98test.exe fakewin <WindowClass> 30'` (wait until `s98test findwin` finds it), then
   `setup.exe /S` gives exit 2.
7. Uninstall plain (`uninst.exe /S`): UserData and changed keep files stay. Then reinstall and `uninst.exe /S /PURGE`:
   the folder is gone. Registry exports equal the "before" ones. After a reboot only the two `"Implementing"`
   timestamps under HKCR differ (Windows rewrites them at boot).
8. Clean up: nothing installed, `C:\S98T` deleted, no `WININIT.INI` left from your tests (or reboot once), no
   leftover processes (`w98 ps`).

---

## 12. Pica 1.0 compatibility

`examples/pica.ini` (`Id=Pica`) uses exactly the registry layout of Pica's own `setup.exe` 1.0
(`HKLM\Software\Pica` with `InstallDir`, `Version`, `Files`, `Types`, `StartMenu`, `Desktop`, `ContextMenu`,
`StartMenuLnk`, `DesktopLnk`, `Previous\Open|Edit|Created`; `HKCR\Pica.<ext>`; `HKCR\*\shell\Pica`; `Uninstall\Pica`
with the same values). Tested on VM A:
- Pica 1.0 installed by `editor/release/pica-1.0-setup.exe /S /TYPES=+.bat`, then the Setup98 Pica `/S`: repair
  mode. It read the old file list from the registry, kept every association, including the user's extra `.bat` edit
  verb and its saved previous Notepad command, replaced `uninst.exe` and wrote `uninst.ini`. Its `uninst.exe /S`
  left HKCR and the Uninstall key identical to the clean baseline (apart from the two boot-time "Implementing"
  values) and `HKLM\Software\Pica` absent.
- Setup98 Pica `/UNINSTALL /S /PURGE` directly over a plain Pica 1.0 install (no record) cleaned up just as well.
- Fresh Setup98 Pica: the same associations as Pica 1.0 (`Pica.txt` "Text Document" with Notepad's `print`,
  `Pica.c` with "Open with MSDev", 10 default types, `*\shell\Pica`). Double-click and `edit` opened Pica.
  `/TYPES=+.bat,+.reg,-.ini` restored `.ini` and made Pica the Edit verb of batfile and regfile. Plain uninstall kept
  `pica.ini` and `session1/2.ini`; purge removed them. The registry was identical to the baseline afterwards.

So Pica can switch to Setup98 later (with `Id=Pica`) and upgrade existing 1.0 installs in place. Going back (Pica
1.0's own setup over a Setup98 install) should work too, since it reads the same `Files` and `Types` values, but it
was not tested.

---

## 13. Format and compression

- Payload (`src/pformat.h`): a 16-byte header, the canonical manifest, a table of contents (sizes, CRC-32, offset,
  method, filter, keep flag, name per file), the file data, then a 32-byte trailer at the end of the exe (magic
  `Setup98\x1A`, offsets, payload CRC-32, its own CRC). The stub checks the whole-payload CRC before it shows
  anything ("This setup file is damaged"), and every file's CRC after decompression, before anything is replaced.
- Codec LZH1 (`src/lzh.h`): LZ77 with hash chains, a 64 KB window and matches of 3-258 bytes with one-step lazy
  matching, plus canonical Huffman coding per block (code lengths limited to 15 bits). Before compressing, pack98
  may apply a filter: byte delta (stride 1-4, for 24-bit BMP) or a 16-bit sample delta (mono/stereo WAV). It tries
  no filter and the two most promising filters and keeps the smallest result; incompressible files are stored. The
  decoder is about 200 lines, uses table lookup and no heap, and rejects corrupt input safely (fuzzed under
  ASan/UBSan).

| Package | Files | Original | Packed | Ratio | Setup size |
|---|---|---|---|---|---|
| Retro Strike 1.0.2 | 51 | 3,002,302 | 1,019,499 (34.0%) | 2.9:1 (gzip -9 per file: 36.8%) | 1,141,151 |
| Pica 1.0 | 2 | 796,580 | 399,047 (50.1%) | 2.0:1 (Pica's zip: 50.3%) | 519,658 (Pica's own setup: 937,984) |
| Project Lähiö draft | 106 | 33,474,127 | 3,777,851 (11.3%) | 8.9:1 | 3,901,845 |

Install speed on VM A (Pentium II 233, emulated; `s98test ms` around `start /w setup.exe /S`): Retro Strike fresh
install 0.62 s, repair 0.67 s; the 33.5 MB Lähiö package 6.4 s.

---

## 14. Known limits

- Installed files get the install time as their date. File dates aren't stored, so the Mac and VC6 packers stay
  byte-identical.
- One install per machine and Id (all users share it, as on Win98); no per-user installs, no custom actions, no
  scripting, no localisation (English UI).
- No component selection; the only choices are the folder, shortcuts, context menu and file types.
- Moving an installed product means uninstall plus install (as with Pica).
- The `Files` registry value is omitted above 8 KB (Win9x value-size limits); the install record has the full list.
- The temp copy of the uninstaller stays in `%TEMP%` until the next restart (WININIT.INI deletes it).
- The ProgID verb copies for `[Types]` are a snapshot (as in Pica): a verb added to `txtfile` later shows on `.txt`
  only after a repair run or uninstall. `shellex` handlers aren't copied.
- Tested on Win98 SE (VM A) only; Win95/ME/NT untested.

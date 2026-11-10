Setup98 @VERSION@ - single-file installers for Windows 98
=====================================================

Setup98 turns a folder of program files and a small INI file (the
"manifest") into one self-extracting setup.exe with a classic Win98
wizard, an uninstaller in Add/Remove Programs, Start menu shortcuts,
upgrades that keep the user's settings, and silent mode. No installer
scripting, no recompiling. Home page and full reference:
https://github.com/HenriVSL/setup98  (README.md in this zip, too)

What is in this zip
-------------------
BIN\PACK98.EXE    the packer (console program, Win95/98/NT)
BIN\SETUP98.EXE   the installer stub pack98 builds every setup from
BIN\S98TEST.EXE   test helper (MD5 lists, shortcut contents, ...)
EXAMPLES\         manifests: DEMO.INI (self-contained, with the demo
                  program in EXAMPLES\DEMO), HELLO.INI (the smallest),
                  RETROSTRIKE.INI and PICA.INI (real-world examples;
                  they need those programs' release folders)
README.md         the full manual (plain text, open it in Notepad)
CHANGES, LICENSE  history and the MIT license

Keep BIN\PACK98.EXE and BIN\SETUP98.EXE together: pack98 looks for the
stub next to itself.

Try it in 1 minute (MS-DOS Prompt)
----------------------------------
  cd C:\SETUP98                      (wherever you unzipped it)
  bin\pack98 examples\demo.ini
  out\s98demo-1.0-setup.exe          (the wizard)

Package your own program
------------------------
1. Copy EXAMPLES\HELLO.INI (or DEMO.INI) next to your program's
   release folder, e.g. C:\MYGAME\MYGAME.INI, and edit it:

     [Product]
     Id=MyGame              (registry name; never change it later)
     Name=My Game
     Version=1.0
     MainExe=mygame.exe

     [Files]
     Source=release         (folder with the files, relative to the INI)
     File=mygame.exe
     File=art\*.bmp
     File=data\**           (** = everything below data)
     Keep=mygame.ini        (default settings: installed only if missing)
     UserData=*.sav         (written by the game: never shipped)

   Write comments on their own lines (; or #), never after a value.

2. Check it, then pack it:

     C:\SETUP98\BIN\PACK98 -check C:\MYGAME\MYGAME.INI
     C:\SETUP98\BIN\PACK98 C:\MYGAME\MYGAME.INI

   Errors name the manifest line. The setup is written to
   <Id>-<Version>-setup.exe next to the INI ([Pack] OutFile= changes it).
   PACK98 -t <setup.exe> tests a setup, -l lists it, -x <dir> extracts it.

3. Test it:

     mygame-1.0-setup.exe /S              silent install (exit code 0 = ok)
     "C:\Program Files\My Game\uninst.exe" /S /PURGE    remove everything

   The log of the last run is in your TEMP folder (first 5 letters of the
   Id + SET.LOG, e.g. C:\WINDOWS\TEMP\MYGAMSET.LOG).

Setup switches
--------------
/S                 silent install with the defaults
/D=C:\Games\X      install folder (must be the last switch, no quotes)
/STARTMENU=0|1     /DESKTOP=0|1     shortcut options
/RUN               start the program after a silent install
/UNINSTALL         uninstall; with /S silent, /PURGE also deletes UserData
Exit codes: 0 ok, 1 error or cancelled, 2 the program is running,
3 restart needed, 4 a newer version is installed, 5 not enough space.

The same pack98 runs on macOS and Linux (build it with "make" from the
source on GitHub) and writes byte-identical setups.

Setup98 is MIT-licensed. Designed and directed by Henri Lavikainen;
developed with Claude (Anthropic) coding agents, 2026.

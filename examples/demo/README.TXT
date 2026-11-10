Setup98 Demo 1.0
================

A tiny Windows program that exists to be installed. It is the self-contained
example of Setup98, the single-file installer kit for Windows 98:
https://github.com/HenriVSL/setup98

What it shows
-------------
- A window with a picture (data\logo.bmp), a greeting and a tip
  (data\tips.txt). These are program files: every upgrade replaces them.
- s98demo.ini holds the settings (title, greeting, background colour).
  Setup installs it only if it is missing, so your edits survive upgrades.
- s98demo.sav counts how often the demo was started. The demo writes it;
  the setup never ships it. Because it exists, uninstall asks "Also delete
  the demo's start counter?". Answer No and it stays.
- .greet files open in the demo (try data\hello.greet). Setup registered
  the file type and uninstall puts everything back.

Files
-----
s98demo.exe      the program (built with Visual C++ 6.0 from src\)
s98demo.ini      default settings (a Keep file)
data\            logo.bmp, tips.txt, hello.greet
README.TXT       this file
LICENSE.TXT      MIT license

Build and pack it yourself (from the Setup98 folder)
---------------------------------------------------
  cd examples\demo\src
  nmake                       (VC6; rebuilds ..\s98demo.exe)
  cd ..\..\..
  bin\pack98.exe examples\demo.ini     (Win98/Windows)
  bin/pack98 examples/demo.ini         (macOS/Linux, after "make")

The setup lands in out\s98demo-1.0-setup.exe. Run it, or silently:
  s98demo-1.0-setup.exe /S
  "C:\Program Files\Setup98 Demo\uninst.exe" /S /PURGE

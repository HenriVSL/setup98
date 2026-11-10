# Makefile - Setup98 Demo, NMAKE (Visual C++ 6.0). Run in examples\demo\src: nmake  (writes ..\s98demo.exe)
#   nmake clean    removes the objects (the prebuilt ..\s98demo.exe stays)
# Any Win32 compiler works; e.g. MinGW: windres s98demo.rc -O coff -o s98demo.o
#   gcc -O2 -mwindows -o ../s98demo.exe s98demo.cpp s98demo.o
CFLAGS = /nologo /W3 /O2 /G6 /MT /DWIN32 /D_WINDOWS

all: ..\s98demo.exe

..\s98demo.exe: s98demo.cpp s98demo.res
	cl $(CFLAGS) /Fos98demo.obj /Fe..\s98demo.exe s98demo.cpp s98demo.res user32.lib gdi32.lib kernel32.lib /link /subsystem:windows

s98demo.res: s98demo.rc s98demo.ico
	rc /r /fos98demo.res s98demo.rc

clean:
	-del s98demo.obj
	-del s98demo.res

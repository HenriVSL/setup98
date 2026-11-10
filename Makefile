# Makefile - Setup98 kit, NMAKE (VC6). Run in M:\dev\setup98 (the Mac builds pack98 with GNUmakefile instead):
#   ~/Documents/86box/samba/w98 build setup98              -> bin\setup98.exe (stub), bin\pack98.exe (packer),
#                                                             bin\s98test.exe (VM test helper)
#   ~/Documents/86box/samba/w98 build setup98 codectest    -> obj\codectest.exe (codec tests under VC6)
#   ~/Documents/86box/samba/w98 build setup98 clean
#
# Branded stub (own icon / version strings) for one project, without touching the default build:
#   ... build setup98 stub ICON=M:\dev\mygame\res\game.ico "PRODUCT=My Game" "DESC=My Game setup" ^
#       OBJDIR=M:\dev\mygame\obj\stub OUT=M:\dev\mygame\dist\stub.exe
# then pack with:  pack98 mygame.ini -stub <that OUT>   (or [Pack] Stub= in the manifest).
# Point OBJDIR and OUT into your own project: the stub target then writes nothing inside M:\dev\setup98, so two
# sessions (VM A, VM B) can build at the same time. OBJDIR's parent folder must exist (Win98 md makes one level).

OBJDIR = obj
OUT = bin\setup98.exe
ICON =
PRODUCT = Setup98
DESC = $(PRODUCT) installer
COMPANY =

CFLAGS = /nologo /W3 /O2 /G6 /MT /DWIN32 /D_WINDOWS /Isrc /Ires
LIBS = user32.lib gdi32.lib kernel32.lib comctl32.lib shell32.lib advapi32.lib ole32.lib version.lib

!IF "$(ICON)" != ""
BRANDDIR = $(OBJDIR)
BRANDH = $(OBJDIR)\brand.h
!ELSE
BRANDDIR = res\default
BRANDH = res\default\brand.h
!ENDIF

COMMON_H = src\s98types.h src\version.h src\port.h src\crc32.h src\lzh.h src\lzhtab.h src\manifest.h src\pformat.h src\wild.h
STUB_H = $(COMMON_H) src\s98.h res\ids.h
STUB_OBJS = $(OBJDIR)\setup.obj $(OBJDIR)\inst.obj $(OBJDIR)\assoc.obj $(OBJDIR)\payload.obj $(OBJDIR)\util.obj \
            $(OBJDIR)\manifest.obj $(OBJDIR)\wild.obj $(OBJDIR)\crc32.obj $(OBJDIR)\lzhdec.obj
PACK_OBJS = $(OBJDIR)\pack98.obj $(OBJDIR)\manifest.obj $(OBJDIR)\wild.obj $(OBJDIR)\crc32.obj $(OBJDIR)\lzhdec.obj \
            $(OBJDIR)\lzhenc.obj

all: objdir bindir bin\setup98.exe bin\pack98.exe bin\s98test.exe

# a branded stub writes only into $(OBJDIR) and $(OUT): nothing inside M:\dev\setup98 when both point elsewhere
stub: objdir $(OUT)

objdir:
	@if not exist $(OBJDIR)\nul mkdir $(OBJDIR)

bindir:
	@if not exist bin\nul mkdir bin

$(OBJDIR)\setup.obj: src\setup.cpp $(STUB_H)
	cl $(CFLAGS) /c /Fo$(OBJDIR)\setup.obj src\setup.cpp

$(OBJDIR)\inst.obj: src\inst.cpp $(STUB_H)
	cl $(CFLAGS) /c /Fo$(OBJDIR)\inst.obj src\inst.cpp

$(OBJDIR)\assoc.obj: src\assoc.cpp $(STUB_H)
	cl $(CFLAGS) /c /Fo$(OBJDIR)\assoc.obj src\assoc.cpp

$(OBJDIR)\payload.obj: src\payload.cpp $(STUB_H)
	cl $(CFLAGS) /c /Fo$(OBJDIR)\payload.obj src\payload.cpp

$(OBJDIR)\util.obj: src\util.cpp $(STUB_H)
	cl $(CFLAGS) /c /Fo$(OBJDIR)\util.obj src\util.cpp

$(OBJDIR)\manifest.obj: src\manifest.cpp $(COMMON_H)
	cl $(CFLAGS) /c /Fo$(OBJDIR)\manifest.obj src\manifest.cpp

$(OBJDIR)\wild.obj: src\wild.cpp $(COMMON_H)
	cl $(CFLAGS) /c /Fo$(OBJDIR)\wild.obj src\wild.cpp

$(OBJDIR)\crc32.obj: src\crc32.cpp $(COMMON_H)
	cl $(CFLAGS) /c /Fo$(OBJDIR)\crc32.obj src\crc32.cpp

$(OBJDIR)\lzhdec.obj: src\lzhdec.cpp $(COMMON_H)
	cl $(CFLAGS) /c /Fo$(OBJDIR)\lzhdec.obj src\lzhdec.cpp

$(OBJDIR)\lzhenc.obj: src\lzhenc.cpp $(COMMON_H)
	cl $(CFLAGS) /c /Fo$(OBJDIR)\lzhenc.obj src\lzhenc.cpp

$(OBJDIR)\pack98.obj: src\pack98.cpp $(COMMON_H)
	cl $(CFLAGS) /c /Fo$(OBJDIR)\pack98.obj src\pack98.cpp

$(OBJDIR)\brand.h: force
	echo #define S98_ICON_FILE "$(ICON:\=\\)" > $(OBJDIR)\brand.h
	echo #define S98_DESC "$(DESC)" >> $(OBJDIR)\brand.h
	echo #define S98_PRODUCT "$(PRODUCT)" >> $(OBJDIR)\brand.h
	echo #define S98_COMPANY "$(COMPANY)" >> $(OBJDIR)\brand.h

$(OBJDIR)\setup98.res: res\setup98.rc res\ids.h src\version.h res\setup98.ico $(BRANDH)
	rc /r /i$(BRANDDIR) /isrc /ires /fo$(OBJDIR)\setup98.res res\setup98.rc

!IF "$(OUT)" != "bin\setup98.exe"
$(OUT): $(STUB_OBJS) $(OBJDIR)\setup98.res
	link /nologo /subsystem:windows /out:$(OUT) $(STUB_OBJS) $(OBJDIR)\setup98.res $(LIBS)
!ENDIF

bin\setup98.exe: $(STUB_OBJS) $(OBJDIR)\setup98.res
	link /nologo /subsystem:windows /out:bin\setup98.exe $(STUB_OBJS) $(OBJDIR)\setup98.res $(LIBS)

bin\pack98.exe: $(PACK_OBJS)
	link /nologo /subsystem:console /out:bin\pack98.exe $(PACK_OBJS) kernel32.lib

force:

s98test: objdir bindir bin\s98test.exe

bin\s98test.exe: tests\s98test.cpp
	cl $(CFLAGS) /Fo$(OBJDIR)\s98test.obj /Febin\s98test.exe tests\s98test.cpp user32.lib shell32.lib advapi32.lib ole32.lib version.lib /link /subsystem:console

codectest: objdir $(OBJDIR)\codectest.exe

$(OBJDIR)\codectest.exe: tests\codectest.cpp $(OBJDIR)\lzhenc.obj $(OBJDIR)\lzhdec.obj $(OBJDIR)\crc32.obj
	cl $(CFLAGS) /Fo$(OBJDIR)\codectest.obj /Fe$(OBJDIR)\codectest.exe tests\codectest.cpp $(OBJDIR)\lzhenc.obj $(OBJDIR)\lzhdec.obj $(OBJDIR)\crc32.obj /link /subsystem:console

clean:
	-del $(OBJDIR)\*.obj
	-del $(OBJDIR)\*.res
	-del $(OBJDIR)\brand.h
	-del $(OBJDIR)\codectest.exe
	-del bin\setup98.exe
	-del bin\pack98.exe
	-del bin\s98test.exe

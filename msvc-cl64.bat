@ECHO OFF
rem This shim lets us run the MSVC toolchain from inside a bash session that is presumably from MSYS.
call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86_amd64
cl /GS /TC /GL /analyze /Wall /Gy /Gm- /O2 /sdl /fp:precise /DWIN32 /DNDEBUG /D_CONSOLE /D_LIB /D_UNICODE /DUNICODE /errorReport:prompt /WX /Zc:forScope /Gd /Oy- /Oi /MD /nologo /Za /Fobin/ %*
exit




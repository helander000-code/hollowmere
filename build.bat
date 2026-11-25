@echo off
rem Builds hollowmere.exe with MSVC. Run from anywhere.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 /utf-8 hollowmere.cpp /Fe:hollowmere.exe /link user32.lib gdi32.lib d3d12.lib dxgi.lib d3dcompiler.lib winmm.lib shell32.lib windowscodecs.lib ole32.lib /SUBSYSTEM:WINDOWS

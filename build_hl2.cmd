@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "GM_ROOT=%~dp0"
set "GM_WHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%GM_WHERE%" goto :missing
for /f "usebackq delims=" %%I in (`"%GM_WHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GM_VS=%%I"
if not defined GM_VS goto :missing
set "GM_VERSION_FILE=%GM_VS%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.v143.default.txt"
if not exist "%GM_VERSION_FILE%" set "GM_VERSION_FILE=%GM_VS%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
if not exist "%GM_VERSION_FILE%" goto :missing
set /p GM_VC=<"%GM_VERSION_FILE%"
echo %GM_VC%| findstr /r "^14\.3 ^14\.4" >nul
if errorlevel 1 goto :missing
call "%GM_VS%\VC\Auxiliary\Build\vcvarsall.bat" x86 -vcvars_ver=%GM_VC%
if errorlevel 1 exit /b 1
if not exist "%GM_ROOT%build" mkdir "%GM_ROOT%build"
if errorlevel 1 exit /b 1
pushd "%GM_ROOT%build"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE /LD "%GM_ROOT%src\server_plugin.cpp" "%GM_ROOT%src\runtime_patches.cpp" "%GM_ROOT%src\cheat_commands.cpp" "%GM_ROOT%src\sprint_movement.cpp" "%GM_ROOT%src\keyvalues_settings.cpp" "%GM_ROOT%src\retail_entity_api.cpp" "%GM_ROOT%src\player_tools_runtime.cpp" "%GM_ROOT%src\weapon_admin.cpp" "%GM_ROOT%src\sc_clean.cpp" "%GM_ROOT%src\sc_remove.cpp" /Fe:gunmaster.dll /link /DEF:"%GM_ROOT%src\gunmaster.def" bcrypt.lib /MACHINE:X86 /DYNAMICBASE /NXCOMPAT
if errorlevel 1 goto :failed
cl /nologo /std:c++17 /W4 /WX /O2 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE "%GM_ROOT%tests\runtime_tests.cpp" "%GM_ROOT%src\runtime_patches.cpp" "%GM_ROOT%src\cheat_commands.cpp" "%GM_ROOT%src\sprint_movement.cpp" "%GM_ROOT%src\retail_entity_api.cpp" /Fe:runtime_tests.exe /link bcrypt.lib /MACHINE:X86
if errorlevel 1 goto :failed
runtime_tests.exe "%GM_ROOT%build\gunmaster.dll"
if errorlevel 1 goto :failed
copy /y gunmaster.dll "%GM_ROOT%Half-Life 2\gunmaster\bin\gunmaster.dll" >nul
if errorlevel 1 goto :failed
popd
echo Built and tested with MSVC v143 %GM_VC% ^(x86^).
echo Ready to copy: "%GM_ROOT%Half-Life 2"
exit /b 0

:failed
popd
echo ERROR: build or standalone tests failed. The copy-ready DLL was not updated.
exit /b 1

:missing
echo ERROR: Install Visual Studio C++ tools with MSVC v143 x86 and a Windows SDK.
exit /b 1

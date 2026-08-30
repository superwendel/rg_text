@echo off
setlocal

set "TARGET=%~1"
if not defined TARGET set "TARGET=test"
if not defined RG_CORE_DIR set "RG_CORE_DIR=%~dp0..\rg_core"

if /I "%TARGET%"=="clean" goto clean
if /I "%TARGET%"=="test" goto test
if /I "%TARGET%"=="test_ci" goto test_ci
if /I "%TARGET%"=="test_release" goto test_release
if /I "%TARGET%"=="test_text" goto test_text
if /I "%TARGET%"=="test_gpu" goto test_gpu
if /I "%TARGET%"=="test_gpu_compile" goto test_gpu_compile
if /I "%TARGET%"=="test_gpu_device_build" goto test_gpu_device_build
if /I "%TARGET%"=="test_gpu_device" goto test_gpu_device
if /I "%TARGET%"=="test_baker" goto test_baker
if /I "%TARGET%"=="shaders" goto shaders
if /I "%TARGET%"=="rg_text_bake" goto rg_text_bake

echo Unknown target: %TARGET%
exit /b 1

:ensure_compiler
where cl >nul 2>nul
if errorlevel 1 goto ensure_compiler_find
if not errorlevel 0 goto ensure_compiler_find
exit /b 0

:ensure_compiler_find
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
	echo Could not find cl.exe or vswhere.exe.
	exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
	echo Could not find a Visual Studio C++ installation.
	exit /b 1
)

call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:validate_core
if not exist "%RG_CORE_DIR%\src\rg_defs.h" (
	echo rg_core not found. Set RG_CORE_DIR to the rg_core repository root.
	exit /b 1
)
exit /b 0

:find_sdl
if defined SDL3_INCLUDE_DIR if defined SDL3_LIB_DIR goto find_sdl_validate
if defined SDL3_DIR goto find_sdl_from_root
for /d %%i in ("C:\libs\SDL3-*") do set "SDL3_DIR=%%i"
if not defined SDL3_DIR if exist "C:\libs\SDL3" set "SDL3_DIR=C:\libs\SDL3"

:find_sdl_from_root
if not defined SDL3_INCLUDE_DIR set "SDL3_INCLUDE_DIR=%SDL3_DIR%\include"
if not defined SDL3_LIB_DIR if exist "%SDL3_DIR%\lib\x64\SDL3.lib" set "SDL3_LIB_DIR=%SDL3_DIR%\lib\x64"
if not defined SDL3_LIB_DIR if exist "%SDL3_DIR%\lib\SDL3.lib" set "SDL3_LIB_DIR=%SDL3_DIR%\lib"
if not defined SDL3_BIN_DIR if exist "%SDL3_DIR%\bin" set "SDL3_BIN_DIR=%SDL3_DIR%\bin"
if not defined SDL3_BIN_DIR if exist "%SDL3_DIR%\lib\x64\SDL3.dll" set "SDL3_BIN_DIR=%SDL3_DIR%\lib\x64"

:find_sdl_validate
if not exist "%SDL3_INCLUDE_DIR%\SDL3\SDL.h" exit /b 1
if not exist "%SDL3_LIB_DIR%\SDL3.lib" exit /b 1
exit /b 0

:set_freetype_root
if not defined FREETYPE_INCLUDE_DIR if exist "%~1\include\ft2build.h" set "FREETYPE_INCLUDE_DIR=%~1\include"
if not defined FREETYPE_INCLUDE_DIR if exist "%~1\include\freetype2\ft2build.h" set "FREETYPE_INCLUDE_DIR=%~1\include\freetype2"
if not defined FREETYPE_LIB_DIR if exist "%~1\lib\freetype.lib" set "FREETYPE_LIB_DIR=%~1\lib"
if not defined FREETYPE_BIN_DIR if exist "%~1\bin" set "FREETYPE_BIN_DIR=%~1\bin"
exit /b 0

:set_harfbuzz_root
if not defined HARFBUZZ_INCLUDE_DIR if exist "%~1\include\harfbuzz\hb.h" set "HARFBUZZ_INCLUDE_DIR=%~1\include\harfbuzz"
if not defined HARFBUZZ_INCLUDE_DIR if exist "%~1\include\hb.h" set "HARFBUZZ_INCLUDE_DIR=%~1\include"
if not defined HARFBUZZ_LIB_DIR if exist "%~1\lib\harfbuzz.lib" set "HARFBUZZ_LIB_DIR=%~1\lib"
if not defined HARFBUZZ_BIN_DIR if exist "%~1\bin" set "HARFBUZZ_BIN_DIR=%~1\bin"
exit /b 0

:set_baker_prefix
call :set_freetype_root "%~1"
call :set_harfbuzz_root "%~1"
exit /b 0

:find_baker_deps
if defined FREETYPE_DIR call :set_freetype_root "%FREETYPE_DIR%"
if defined HARFBUZZ_DIR call :set_harfbuzz_root "%HARFBUZZ_DIR%"
if defined RG_TEXT_BAKER_PREFIX call :set_baker_prefix "%RG_TEXT_BAKER_PREFIX%"
if defined VCPKG_INSTALLED_DIR if exist "%VCPKG_INSTALLED_DIR%\include" call :set_baker_prefix "%VCPKG_INSTALLED_DIR%"
if defined VCPKG_INSTALLED_DIR if exist "%VCPKG_INSTALLED_DIR%\x64-windows\include" call :set_baker_prefix "%VCPKG_INSTALLED_DIR%\x64-windows"
if exist "vcpkg_installed\x64-windows\include" call :set_baker_prefix "%CD%\vcpkg_installed\x64-windows"

if not exist "%FREETYPE_INCLUDE_DIR%\ft2build.h" (
	echo FreeType headers not found. Set FREETYPE_DIR or FREETYPE_INCLUDE_DIR.
	exit /b 1
)
if not exist "%FREETYPE_LIB_DIR%\freetype.lib" (
	echo FreeType library not found. Set FREETYPE_DIR or FREETYPE_LIB_DIR.
	exit /b 1
)
if not exist "%HARFBUZZ_INCLUDE_DIR%\hb.h" (
	echo HarfBuzz headers not found. Set HARFBUZZ_DIR or HARFBUZZ_INCLUDE_DIR.
	exit /b 1
)
if not exist "%HARFBUZZ_INCLUDE_DIR%\hb-ft.h" (
	echo HarfBuzz FreeType integration header not found: %HARFBUZZ_INCLUDE_DIR%\hb-ft.h
	exit /b 1
)
if not exist "%HARFBUZZ_LIB_DIR%\harfbuzz.lib" (
	echo HarfBuzz library not found. Set HARFBUZZ_DIR or HARFBUZZ_LIB_DIR.
	exit /b 1
)
exit /b 0

:test
call "%~f0" test_text
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo rg_text renderer-neutral tests passed. GPU checks were not run; use test_ci or test_release.
exit /b 0

:test_ci
call "%~f0" test
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" test_gpu_compile
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" shaders
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" test_gpu_device_build
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo rg_text CI suite passed. The optional baker and GPU device execution were not run; use test_release.
exit /b 0

:test_release
call "%~f0" test_ci
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" test_baker
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call "%~f0" test_gpu_device
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo All rg_text release tests passed.
exit /b 0

:test_text
call :ensure_compiler
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :validate_core
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

echo Building rg_text tests...
cl /nologo /std:c11 /W4 /WX /O2 /I "%RG_CORE_DIR%\src" tests\test_text.c /Fe:test_text.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
test_text.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:test_gpu
:test_gpu_compile
call :ensure_compiler
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :validate_core
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :find_sdl
if errorlevel 1 (
	echo SDL3 not found. Set SDL3_DIR to the SDL3 development package root.
	exit /b 1
)
if not errorlevel 0 (
	echo SDL3 not found. Set SDL3_DIR to the SDL3 development package root.
	exit /b 1
)

set "PATH=%SDL3_BIN_DIR%;%SDL3_LIB_DIR%;%PATH%"
echo Building rg_text_gpu compile and layout checks; no GPU device will be used...
cl /nologo /std:c11 /W4 /WX /O2 /I "%RG_CORE_DIR%\src" /I "%SDL3_INCLUDE_DIR%" tests\test_text_gpu.c /Fe:test_text_gpu.exe /link /LIBPATH:"%SDL3_LIB_DIR%" SDL3.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
test_text_gpu.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:test_gpu_device
call "%~f0" shaders
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
rem Keep SDL3_BIN_DIR on this batch's PATH while the device test runs.
call :test_gpu_device_build
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
test_text_gpu_device.exe
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:test_gpu_device_build
call :ensure_compiler
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :validate_core
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :find_sdl
if errorlevel 1 (
	echo SDL3 not found. Set SDL3_DIR or the explicit SDL3 include and library directories.
	exit /b 1
)
if not errorlevel 0 (
	echo SDL3 not found. Set SDL3_DIR or the explicit SDL3 include and library directories.
	exit /b 1
)
set "PATH=%SDL3_BIN_DIR%;%SDL3_LIB_DIR%;%PATH%"
echo Building rg_text SDL_GPU device test...
cl /nologo /std:c11 /W4 /WX /O2 /I "%RG_CORE_DIR%\src" /I "%SDL3_INCLUDE_DIR%" tests\test_text_gpu_device.c /Fe:test_text_gpu_device.exe /link /LIBPATH:"%SDL3_LIB_DIR%" SDL3.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:find_shadercross
if defined SHADERCROSS_EXE goto find_shadercross_validate
for /d %%i in ("C:\libs\SDL3_shadercross-*") do set "SHADERCROSS_EXE=%%i\bin\shadercross.exe"
if not defined SHADERCROSS_EXE for %%i in (shadercross.exe) do set "SHADERCROSS_EXE=%%~$PATH:i"

:find_shadercross_validate
if not exist "%SHADERCROSS_EXE%" exit /b 1
exit /b 0

:shaders
call :find_shadercross
if errorlevel 1 (
	echo SDL_shadercross not found. Set SHADERCROSS_EXE or add shadercross.exe to PATH.
	exit /b 1
)
if not errorlevel 0 (
	echo SDL_shadercross not found. Set SHADERCROSS_EXE or add shadercross.exe to PATH.
	exit /b 1
)

if not exist "shaders\Compiled\DXIL" mkdir "shaders\Compiled\DXIL"
if not exist "shaders\Compiled\SPIRV" mkdir "shaders\Compiled\SPIRV"
if not exist "shaders\Compiled\MSL" mkdir "shaders\Compiled\MSL"

for %%s in (vert frag) do (
	set "SHADER_STAGE=vertex"
	if "%%s"=="frag" set "SHADER_STAGE=fragment"
	call :compile_shader %%s
	if errorlevel 1 exit /b 1
	if not errorlevel 0 exit /b 1
)
echo rg_text shaders compiled successfully.
exit /b 0

:compile_shader
"%SHADERCROSS_EXE%" "shaders\rg_text.%1.hlsl" -s HLSL -d DXIL -t %SHADER_STAGE% -e main -o "shaders\Compiled\DXIL\rg_text.%1.dxil"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
"%SHADERCROSS_EXE%" "shaders\rg_text.%1.hlsl" -s HLSL -d SPIRV -t %SHADER_STAGE% -e main -o "shaders\Compiled\SPIRV\rg_text.%1.spv"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
"%SHADERCROSS_EXE%" "shaders\rg_text.%1.hlsl" -s HLSL -d MSL -t %SHADER_STAGE% -e main -o "shaders\Compiled\MSL\rg_text.%1.msl"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:rg_text_bake
call :ensure_compiler
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :validate_core
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
call :find_baker_deps
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

if defined FREETYPE_BIN_DIR set "PATH=%FREETYPE_BIN_DIR%;%PATH%"
if defined HARFBUZZ_BIN_DIR set "PATH=%HARFBUZZ_BIN_DIR%;%PATH%"

echo Building rg_text_bake...
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS ^
	/I "%RG_CORE_DIR%\src" ^
	/external:W0 /external:I"%FREETYPE_INCLUDE_DIR%" /external:I"%HARFBUZZ_INCLUDE_DIR%" ^
	tools\rg_text_bake.c /Fe:rg_text_bake.exe ^
	/link /LIBPATH:"%FREETYPE_LIB_DIR%" /LIBPATH:"%HARFBUZZ_LIB_DIR%" harfbuzz.lib freetype.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
rg_text_bake.exe --self-test
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
exit /b 0

:test_baker
if not defined RG_TEXT_TEST_FONT (
	echo RG_TEXT_TEST_FONT must point to the Inter Medium 4.1 TTF for the baker integration test.
	exit /b 1
)
if not exist "%RG_TEXT_TEST_FONT%" (
	echo Test font not found: %RG_TEXT_TEST_FONT%
	exit /b 1
)
rem Keep the compiler environment initialized for the integration verifier below.
call :rg_text_bake
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "BAKE_TEST_DIR=%TEMP%\rg_text_bake_%RANDOM%_%RANDOM%"
mkdir "%BAKE_TEST_DIR%"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
rg_text_bake.exe "%RG_TEXT_TEST_FONT%" "%BAKE_TEST_DIR%\inter_medium_16" 16 32-126 1 256
if errorlevel 1 goto test_baker_fail
if not errorlevel 0 goto test_baker_fail
rg_text_bake.exe --no-kerning "%RG_TEXT_TEST_FONT%" "%BAKE_TEST_DIR%\inter_medium_16_no_kerning" 16 32-126 1 256
if errorlevel 1 goto test_baker_fail
if not errorlevel 0 goto test_baker_fail
rg_text_bake.exe "%RG_TEXT_TEST_FONT%" "%BAKE_TEST_DIR%\inter_medium_16_wide_rejected" 16 32-2047 1 512
if errorlevel 2 goto test_baker_fail
if not errorlevel 1 goto test_baker_fail
if exist "%BAKE_TEST_DIR%\inter_medium_16_wide_rejected.font" goto test_baker_fail
if exist "%BAKE_TEST_DIR%\inter_medium_16_wide_rejected.rgba" goto test_baker_fail
rg_text_bake.exe --no-kerning "%RG_TEXT_TEST_FONT%" "%BAKE_TEST_DIR%\inter_medium_16_wide" 16 32-2047 1 512
if errorlevel 1 goto test_baker_fail
if not errorlevel 0 goto test_baker_fail
cl /nologo /std:c11 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS /I "%RG_CORE_DIR%\src" tests\test_bake_output.c /Fe:test_bake_output.exe
if errorlevel 1 goto test_baker_fail
if not errorlevel 0 goto test_baker_fail
if /I "%RG_TEXT_BAKER_GOLDEN%"=="1" (
	test_bake_output.exe "%BAKE_TEST_DIR%\inter_medium_16" --golden
) else (
	test_bake_output.exe "%BAKE_TEST_DIR%\inter_medium_16"
)
if errorlevel 1 goto test_baker_fail
if not errorlevel 0 goto test_baker_fail
test_bake_output.exe "%BAKE_TEST_DIR%\inter_medium_16" --simulate-truncated-atlas
if errorlevel 1 goto test_baker_fail
if not errorlevel 0 goto test_baker_fail
test_bake_output.exe "%BAKE_TEST_DIR%\inter_medium_16_no_kerning" --no-kerning
if errorlevel 1 goto test_baker_fail
if not errorlevel 0 goto test_baker_fail
test_bake_output.exe "%BAKE_TEST_DIR%\inter_medium_16" --compare-no-kerning "%BAKE_TEST_DIR%\inter_medium_16_no_kerning"
if errorlevel 1 goto test_baker_fail
if not errorlevel 0 goto test_baker_fail
test_bake_output.exe "%BAKE_TEST_DIR%\inter_medium_16_wide" --no-kerning-large
if errorlevel 1 goto test_baker_fail
if not errorlevel 0 goto test_baker_fail
del /q "%BAKE_TEST_DIR%\inter_medium_16.font" "%BAKE_TEST_DIR%\inter_medium_16.rgba" "%BAKE_TEST_DIR%\inter_medium_16_no_kerning.font" "%BAKE_TEST_DIR%\inter_medium_16_no_kerning.rgba" "%BAKE_TEST_DIR%\inter_medium_16_wide.font" "%BAKE_TEST_DIR%\inter_medium_16_wide.rgba" 2>nul
for %%f in (inter_medium_16.font inter_medium_16.rgba inter_medium_16_no_kerning.font inter_medium_16_no_kerning.rgba inter_medium_16_wide.font inter_medium_16_wide.rgba) do if exist "%BAKE_TEST_DIR%\%%f" goto test_baker_cleanup_fail
rmdir "%BAKE_TEST_DIR%" 2>nul
if exist "%BAKE_TEST_DIR%" goto test_baker_cleanup_fail
echo rg_text baker integration test passed.
exit /b 0

:test_baker_fail
set "BAKE_TEST_RESULT=%ERRORLEVEL%"
if "%BAKE_TEST_RESULT%"=="0" set "BAKE_TEST_RESULT=1"
del /q "%BAKE_TEST_DIR%\inter_medium_16.font" "%BAKE_TEST_DIR%\inter_medium_16.rgba" "%BAKE_TEST_DIR%\inter_medium_16_no_kerning.font" "%BAKE_TEST_DIR%\inter_medium_16_no_kerning.rgba" "%BAKE_TEST_DIR%\inter_medium_16_wide_rejected.font" "%BAKE_TEST_DIR%\inter_medium_16_wide_rejected.rgba" "%BAKE_TEST_DIR%\inter_medium_16_wide.font" "%BAKE_TEST_DIR%\inter_medium_16_wide.rgba" 2>nul
rmdir "%BAKE_TEST_DIR%" 2>nul
exit /b %BAKE_TEST_RESULT%

:test_baker_cleanup_fail
echo Failed to remove baker integration-test outputs: %BAKE_TEST_DIR%
exit /b 1

:clean
del /q test_text.exe test_text_gpu.exe test_text_gpu_device.exe test_bake_output.exe rg_text_bake.exe 2>nul
del /q test_text.obj test_text_gpu.obj test_text_gpu_device.obj test_bake_output.obj rg_text_bake.obj 2>nul
if exist "shaders\Compiled" rmdir /s /q "shaders\Compiled"
for %%f in (test_text.exe test_text_gpu.exe test_text_gpu_device.exe test_bake_output.exe rg_text_bake.exe test_text.obj test_text_gpu.obj test_text_gpu_device.obj test_bake_output.obj rg_text_bake.obj) do if exist "%%f" (
	echo Failed to remove build artifact: %%f
	exit /b 1
)
if exist "shaders\Compiled" (
	echo Failed to remove compiled shader directory: shaders\Compiled
	exit /b 1
)
exit /b 0

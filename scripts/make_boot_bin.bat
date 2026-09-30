@echo off
REM SPDX-License-Identifier: GPL-3.0-only
setlocal EnableExtensions DisableDelayedExpansion

REM Package one explicit Multitini Zynq image: FSBL + golden recovery app.
REM Usage:
REM   make_boot_bin.bat OUT_BIN FSBL_ELF BOOTLOADER_ELF
REM
REM There are deliberately no default inputs. A firmware image must always name
REM the exact hardware and software artifacts that it contains.

if "%~1"=="" goto :usage
if "%~2"=="" goto :usage
if "%~3"=="" goto :usage
if not "%~4"=="" goto :usage

set "OUT_BIN=%~f1"
set "FSBL_ELF=%~f2"
set "APP_ELF=%~f3"

if /i not "%~x1"==".bin" (
    echo ERROR: output must have a .bin extension: %~1
    exit /b 2
)
if /i not "%~x2"==".elf" (
    echo ERROR: FSBL must have an .elf extension: %~2
    exit /b 2
)
if /i not "%~x3"==".elf" (
    echo ERROR: golden application must have an .elf extension: %~3
    exit /b 2
)

if /i "%OUT_BIN%"=="%FSBL_ELF%" goto :output_collision
if /i "%OUT_BIN%"=="%APP_ELF%" goto :output_collision
if /i "%FSBL_ELF%"=="%APP_ELF%" (
    echo ERROR: FSBL and application must be different ELF files
    exit /b 2
)

if not exist "%~dp1" (
    echo ERROR: output directory does not exist: %~dp1
    exit /b 1
)
if not exist "%FSBL_ELF%" (
    echo ERROR: FSBL not found: %FSBL_ELF%
    exit /b 1
)
if not exist "%APP_ELF%" (
    echo ERROR: application ELF not found: %APP_ELF%
    exit /b 1
)

if not defined XILINX_VITIS (
    echo ERROR: XILINX_VITIS is not set
    exit /b 1
)

set "BOOTGEN=%XILINX_VITIS%\bin\bootgen.bat"
if not exist "%BOOTGEN%" set "BOOTGEN=%XILINX_VITIS%\bootgen.bat"
if not exist "%BOOTGEN%" (
    echo ERROR: bootgen.bat not found under XILINX_VITIS: %XILINX_VITIS%
    echo        Expected %%XILINX_VITIS%%\bin\bootgen.bat or %%XILINX_VITIS%%\bootgen.bat
    exit /b 1
)

set "PACKAGE_TOKEN=%RANDOM%%RANDOM%"
set "TMP_BIF=%TEMP%\multitini_golden_%PACKAGE_TOKEN%.bif"
set "TMP_OUT=%~dp1.%~n1.%PACKAGE_TOKEN%.tmp.bin"
set "TMP_RAW=%~dp1.%~n1.%PACKAGE_TOKEN%.raw.bin"
if exist "%TMP_RAW%" exit /b 1
if exist "%TMP_BIF%" (
    echo ERROR: temporary BIF already exists: %TMP_BIF%
    exit /b 1
)
if exist "%TMP_OUT%" (
    echo ERROR: temporary output already exists: %TMP_OUT%
    exit /b 1
)

(
echo the_ROM_image:
echo {
echo     [bootloader] "%FSBL_ELF%"
echo     "%APP_ELF%"
echo }
) > "%TMP_BIF%"
if errorlevel 1 (
    echo ERROR: could not create temporary BIF: %TMP_BIF%
    goto :fail
)

echo.
echo ==========================================
echo Create Multitini Golden Image
echo ==========================================
echo OUT : %OUT_BIN%
echo FSBL: %FSBL_ELF%
echo APP : %APP_ELF%
echo.

call "%BOOTGEN%" -arch zynq -image "%TMP_BIF%" -o "%TMP_RAW%" -w on
set "PACK_RC=%ERRORLEVEL%"
if not "%PACK_RC%"=="0" (
    echo ERROR: bootgen failed ^(errorlevel %PACK_RC%^)
    goto :fail_with_rc
)
if not exist "%TMP_RAW%" (
    echo ERROR: bootgen reported success but produced no image
    set "PACK_RC=1"
    goto :fail_with_rc
)
for %%I in ("%TMP_RAW%") do if %%~zI LEQ 0 (
    echo ERROR: bootgen produced an empty image: %TMP_OUT%
    set "PACK_RC=1"
    goto :fail_with_rc
)

python "%~dp0image_manifest.py" append "%TMP_RAW%" "%TMP_OUT%" --role golden --recovery-capable --max-size 0x100000
if errorlevel 1 goto :fail

del /q "%TMP_RAW%" >nul 2>nul
move /y "%TMP_OUT%" "%OUT_BIN%" >nul
if errorlevel 1 (
    echo ERROR: could not install packaged image at: %OUT_BIN%
    set "PACK_RC=1"
    goto :fail_with_rc
)
del /q "%TMP_BIF%" >nul 2>nul

echo SUCCESS: %OUT_BIN%
exit /b 0

:usage
echo Usage: %~nx0 OUT_BIN FSBL_ELF BOOTLOADER_ELF
echo ERROR: exactly three explicit artifact paths are required
exit /b 2

:output_collision
echo ERROR: output path must differ from every input path: %OUT_BIN%
exit /b 2

:fail
set "PACK_RC=1"

:fail_with_rc
del /q "%TMP_RAW%" >nul 2>nul
del /q "%TMP_BIF%" >nul 2>nul
del /q "%TMP_OUT%" >nul 2>nul
exit /b %PACK_RC%

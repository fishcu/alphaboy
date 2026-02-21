@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  AlphaBoy - Dependency Setup Script
REM  Downloads and extracts all external tools/references.
REM  Review each URL below and fix as needed before running.
REM ============================================================

REM ---- Configuration (edit versions/URLs here) ----
set GBDK_VERSION=4.5.0
set MESEN_VERSION=2.1.1

set BGB_URL=https://bgb.bircd.org/bgbw64.zip
set GBDK_SOURCE_URL=https://github.com/gbdk-2020/gbdk-2020.git
set GBDK_RELEASE_URL=https://github.com/gbdk-2020/gbdk-2020/releases/download/%GBDK_VERSION%/gbdk-win64.zip
set MESEN_URL=https://github.com/SourMesen/Mesen2/releases/download/%MESEN_VERSION%/Mesen_%MESEN_VERSION%_Windows.zip
set PANDOCS_URL=https://github.com/gbdev/pandocs.git

REM Emulicious uses WordPress Download Manager; wpdmdl=989 is the stable
REM file ID for "Emulicious with 64-bit Java for Windows".  The refresh
REM parameter is a cache-busting Unix timestamp we generate at runtime.
set EMULICIOUS_WPDMDL=989

set TEMPDIR=_dl_temp

REM ---- Create temp download directory ----
if not exist "%TEMPDIR%" mkdir "%TEMPDIR%"

REM ---- Generate a Unix-ish timestamp for the Emulicious cache-buster ----
for /f %%a in ('powershell -nologo -noprofile -command "[int](Get-Date -UFormat %%s)"') do set TSTAMP=%%a
set EMULICIOUS_URL=https://emulicious.net/download/emulicious-with-64-bit-java-for-windows/?wpdmdl=%EMULICIOUS_WPDMDL%^&refresh=%TSTAMP%

REM ============================================================
REM  1. BGB Emulator (64-bit)  ->  bgbw64/
REM ============================================================
if exist "bgbw64\" (
    echo [SKIP] bgbw64 already exists.
) else (
    echo [1/6] Downloading BGB emulator...
    curl -L -o "%TEMPDIR%\bgbw64.zip" "%BGB_URL%"
    if !errorlevel! neq 0 (
        echo [FAIL] Could not download BGB. Check URL: %BGB_URL%
        goto :emulicious
    )
    echo       Extracting...
    mkdir "bgbw64"
    tar -xf "%TEMPDIR%\bgbw64.zip" -C "bgbw64"
    echo [OK]  bgbw64 ready.
)

:emulicious
REM ============================================================
REM  2. Emulicious (with Java 64-bit)  ->  Emulicious-with-Java64/
REM ============================================================
if exist "Emulicious-with-Java64\" (
    echo [SKIP] Emulicious-with-Java64 already exists.
) else (
    echo [2/6] Downloading Emulicious...
    curl -L -o "%TEMPDIR%\Emulicious-with-Java64.zip" "!EMULICIOUS_URL!"
    if !errorlevel! neq 0 (
        echo [FAIL] Could not download Emulicious. Check URL: %EMULICIOUS_URL%
        goto :gbdk_source
    )
    echo       Extracting...
    mkdir "Emulicious-with-Java64"
    tar -xf "%TEMPDIR%\Emulicious-with-Java64.zip" -C "Emulicious-with-Java64"
    echo [OK]  Emulicious-with-Java64 ready.
)

:gbdk_source
REM ============================================================
REM  3. GBDK-2020 Source and Docs  ->  gbdk_source_and_docs/
REM ============================================================
if exist "gbdk_source_and_docs\" (
    echo [SKIP] gbdk_source_and_docs already exists.
) else (
    echo [3/6] Cloning GBDK-2020 source and docs...
    git clone --depth 1 "%GBDK_SOURCE_URL%" "gbdk_source_and_docs"
    if !errorlevel! neq 0 (
        echo [FAIL] Could not clone GBDK-2020. Check URL: %GBDK_SOURCE_URL%
    ) else (
        echo [OK]  gbdk_source_and_docs ready.
    )
)

REM ============================================================
REM  4. GBDK-2020 Release (prebuilt binaries)  ->  gbdk_release/
REM ============================================================
if exist "gbdk_release\" (
    echo [SKIP] gbdk_release already exists.
) else (
    echo [4/6] Downloading GBDK-2020 v%GBDK_VERSION% release...
    curl -L -o "%TEMPDIR%\gbdk-release.zip" "%GBDK_RELEASE_URL%"
    if !errorlevel! neq 0 (
        echo [FAIL] Could not download GBDK release. Check URL: %GBDK_RELEASE_URL%
        goto :mesen
    )
    echo       Extracting...
    tar -xf "%TEMPDIR%\gbdk-release.zip" -C "."
    REM The zip typically extracts to a folder named "gbdk"; rename it.
    if exist "gbdk\" (
        ren "gbdk" "gbdk_release"
    ) else (
        echo [WARN] Expected extracted folder "gbdk" not found.
        echo        Check the zip contents and rename manually to gbdk_release.
    )
    echo [OK]  gbdk_release ready.
)

:mesen
REM ============================================================
REM  5. Mesen Emulator  ->  Mesen_2.1.1_Windows/
REM ============================================================
if exist "Mesen_%MESEN_VERSION%_Windows\" (
    echo [SKIP] Mesen_%MESEN_VERSION%_Windows already exists.
) else (
    echo [5/6] Downloading Mesen v%MESEN_VERSION%...
    curl -L -o "%TEMPDIR%\mesen.zip" "%MESEN_URL%"
    if !errorlevel! neq 0 (
        echo [FAIL] Could not download Mesen. Check URL: %MESEN_URL%
        goto :pandocs
    )
    echo       Extracting...
    mkdir "Mesen_%MESEN_VERSION%_Windows"
    tar -xf "%TEMPDIR%\mesen.zip" -C "Mesen_%MESEN_VERSION%_Windows"
    echo [OK]  Mesen_%MESEN_VERSION%_Windows ready.
)

:pandocs
REM ============================================================
REM  6. Pan Docs (Game Boy technical reference)  ->  pandocs/
REM ============================================================
if exist "pandocs\" (
    echo [SKIP] pandocs already exists.
) else (
    echo [6/6] Cloning Pan Docs...
    git clone --depth 1 "%PANDOCS_URL%" "pandocs"
    if !errorlevel! neq 0 (
        echo [FAIL] Could not clone Pan Docs. Check URL: %PANDOCS_URL%
    ) else (
        echo [OK]  pandocs ready.
    )
)

REM ---- Cleanup temp downloads ----
echo.
echo Cleaning up temporary downloads...
if exist "%TEMPDIR%" rmdir /S /Q "%TEMPDIR%"

echo.
echo ============================================================
echo  Setup complete. Review any [FAIL] or [WARN] messages above.
echo ============================================================
pause
endlocal

@echo off
REM ============================================================================
REM  build-windows.bat — Compila o Kai no Windows 11 e empacota um instalável.
REM
REM  Pré-requisitos (ver BUILD-WINDOWS.md para detalhes):
REM    - Qt 6 (MSVC 2019/2022 64-bit) instalado, ex: C:\Qt\6.6.0\msvc2019_64
REM    - CMake 3.16+ e um compilador (Visual Studio 2019/2022 ou Build Tools)
REM    - (Opcional) Inno Setup 6 para gerar o instalador .exe (iscc no PATH)
REM
REM  Uso:
REM    set QT_DIR=C:\Qt\6.6.0\msvc2019_64
REM    build-windows.bat
REM
REM  Saída:
REM    build\bin\Release\kai.exe            (binário)
REM    dist\kai-windows\                    (pasta portável com DLLs do Qt)
REM    dist\kai-setup.exe                   (instalável, se Inno Setup presente)
REM ============================================================================
setlocal enabledelayedexpansion

if "%QT_DIR%"=="" (
    echo [ERRO] Defina QT_DIR apontando para o Qt MSVC, ex:
    echo        set QT_DIR=C:\Qt\6.6.0\msvc2019_64
    exit /b 1
)

set BUILD_DIR=build
set DIST_DIR=dist\kai-windows

echo ==^> Configurando com CMake...
cmake -B "%BUILD_DIR%" -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 exit /b 1

echo ==^> Compilando (Release)...
cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 exit /b 1

echo ==^> Preparando pasta de distribuicao...
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"

REM Localiza o kai.exe (o caminho varia entre geradores single/multi-config).
set EXE=%BUILD_DIR%\bin\Release\kai.exe
if not exist "%EXE%" set EXE=%BUILD_DIR%\bin\kai.exe
if not exist "%EXE%" (
    echo [ERRO] kai.exe nao encontrado apos o build.
    exit /b 1
)
copy "%EXE%" "%DIST_DIR%\kai.exe" >nul

REM Copia os assets (temas, language packs, logo) — resolvidos em runtime
REM relativos ao executavel (mesma estrategia do Linux).
xcopy /e /i /y assets "%DIST_DIR%\assets" >nul

echo ==^> Rodando windeployqt (empacota DLLs do Qt)...
"%QT_DIR%\bin\windeployqt.exe" --release --no-translations "%DIST_DIR%\kai.exe"
if errorlevel 1 exit /b 1

echo ==^> Pasta portavel pronta em: %DIST_DIR%

REM --- Instalador (opcional, requer Inno Setup: iscc no PATH) ---
where iscc >nul 2>nul
if %errorlevel%==0 (
    echo ==^> Gerando instalador com Inno Setup...
    iscc "/DKaiSrcDir=%CD%\%DIST_DIR%" "/DKaiOutDir=%CD%\dist" packaging\windows\kai.iss
) else (
    echo ==^> Inno Setup ^(iscc^) nao encontrado no PATH; pulei o instalador.
    echo     Instale de https://jrsoftware.org/isdl.php e rode: iscc packaging\windows\kai.iss
)

echo ==^> Concluido!
endlocal

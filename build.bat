@echo off
title Compilador Nebula Script - Build System
color 0F

echo ===================================================
echo             Nebula Script - Multi-Build            
echo ===================================================
echo(

:: Verifica se o g++ esta instalado no PATH
where g++ >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERRO] O compilador g++ nao foi encontrado no PATH do sistema.
    echo Por favor, instale o MinGW / GCC e configure as variaveis de ambiente.
    echo(
    pause
    exit /b 1
)

where windres >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERRO] O compilador de recursos windres nao foi encontrado no PATH.
    echo Instale o MinGW-w64 com suporte a recursos do Windows.
    echo(
    pause
    exit /b 1
)

:: Numero do build (sobe a cada execucao)
if not exist build_number.txt (>build_number.txt echo 0)
set /p BUILD_NUM=<build_number.txt
set /a BUILD_NUM+=1
>build_number.txt echo %BUILD_NUM%
echo [BUILD] Numero do build: %BUILD_NUM%
echo(

:: FLAGS DE OTIMIZAÇÃO ADICIONADAS: -Os (Tamanho) e -s (Strip - Remove lixo de Debug)
set "COMPILER_FLAGS=-std=c++17 -Wall -Os -s -DNEBULA_BUILD=%BUILD_NUM%"

echo [1/3] Compilando CLI: src\cli\main.cpp para nebula.exe
g++ %COMPILER_FLAGS% src\cli\main.cpp -o nebula.exe -Wl,-Bstatic -lffi -Wl,-Bdynamic
if %errorlevel% neq 0 goto erro
echo [SUCESSO] nebula.exe gerado com sucesso!
echo(

echo [2/3] Compilando Biblioteca: src\nebula.cpp para nebula.dll
g++ %COMPILER_FLAGS% -DNEBULA_BUILD_DLL -shared src\nebula.cpp -o nebula.dll -Wl,-Bstatic -lffi -Wl,-Bdynamic
if %errorlevel% neq 0 goto erro
echo [SUCESSO] nebula.dll gerada com sucesso!
echo(

echo [3/3] Compilando Stub: src\runtime\stub.cpp para stub.exe
set "ICON_RESOURCE=%TEMP%\nebula-stub-icon-%BUILD_NUM%.o"
windres icon.rc -O coff -o "%ICON_RESOURCE%"
if %errorlevel% neq 0 goto erro
g++ %COMPILER_FLAGS% src\runtime\stub.cpp "%ICON_RESOURCE%" -o stub.exe -Wl,-Bstatic -lffi -Wl,-Bdynamic
if %errorlevel% neq 0 goto erro
del "%ICON_RESOURCE%" >nul 2>nul
echo [SUCESSO] stub.exe gerado com sucesso!
echo(

echo ===================================================
echo [STATUS] Todos os binarios foram atualizados com sucesso!
echo ===================================================
echo Build %BUILD_NUM%
goto fim

:erro
if defined ICON_RESOURCE del "%ICON_RESOURCE%" >nul 2>nul
echo(
echo [ERRO] Ocorreu uma falha na compilacao de um dos modulos.

:fim
echo(
pause

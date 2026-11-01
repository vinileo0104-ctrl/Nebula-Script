@echo off
cd /d "%~dp0"
title Instalador Nebula Script

echo ===================================================
echo             Nebula Script - Instalador             
echo ===================================================
echo.

:: 0. Verificar Privilegios de Administrador
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERRO] Este script precisa ser executado como Administrador!
    echo Clique com o botao direito em 'install.bat' e selecione 'Executar como Administrador'.
    echo.
    goto error
)

:: 1. Criar diretorio de destino
echo [1/4] A criar pasta em Program Files...
if not exist "C:\Program Files\NebulaScript" (
    mkdir "C:\Program Files\NebulaScript" 2>nul
)

:: 2. Copiar o executavel nebula.exe, a DLL e o stub.exe
if not exist "nebula.exe" (
    echo [ERRO] O ficheiro nebula.exe nao foi encontrado nesta pasta!
    goto error
)
if not exist "stub.exe" (
    echo [ERRO] O ficheiro stub.exe nao foi encontrado nesta pasta! Execute o build.bat primeiro.
    goto error
)
if exist "icon.ico" (
    set "ICON_SOURCE=icon.ico"
) else if exist "..\icon.ico" (
    set "ICON_SOURCE=..\icon.ico"
) else (
    echo [ERRO] O ficheiro icon.ico nao foi encontrado nesta pasta nem na pasta do projeto.
    goto error
)

echo [2/4] A copiar binarios...
copy /y nebula.exe "C:\Program Files\NebulaScript\" >nul
copy /y stub.exe "C:\Program Files\NebulaScript\" >nul
copy /y "%ICON_SOURCE%" "C:\Program Files\NebulaScript\icon.ico" >nul
if %errorlevel% neq 0 (
    echo [ERRO] Nao foi possivel instalar o icon.ico.
    goto error
)
if exist "nebula.dll" (
    copy /y nebula.dll "C:\Program Files\NebulaScript\" >nul
)

echo Versao instalada:
"C:\Program Files\NebulaScript\nebula.exe" -version

:: 3. Adicionar ao PATH do sistema de forma segura via Registro (Evita truncar o PATH)
echo [3/4] A atualizar o PATH do sistema com seguranca...
for /f "tokens=2*" %%A in ('reg query "HKLM\System\CurrentControlSet\Control\Session Manager\Environment" /v Path 2^>nul') do set "OLD_PATH=%%B"

:: Verifica se a Nebula ja esta no PATH para nao duplicar
echo %OLD_PATH% | find /i "C:\Program Files\NebulaScript" >nul
if %errorlevel% neq 0 (
    reg add "HKLM\System\CurrentControlSet\Control\Session Manager\Environment" /v Path /t REG_EXPAND_SZ /d "%OLD_PATH%;C:\Program Files\NebulaScript" /f >nul
)

:: 4. Associar extensoes de ficheiro com as flags corretas da CLI
echo [4/4] A associar extensoes .ns e .cns...
assoc .ns=NebulaScript.Source >nul
ftype NebulaScript.Source="C:\Program Files\NebulaScript\nebula.exe" -nbc "%%1" >nul

assoc .cns=NebulaScript.Binary >nul
ftype NebulaScript.Binary="%ComSpec%" /k ""C:\Program Files\NebulaScript\nebula.exe" -run "%%1"" >nul
reg add "HKCR\NebulaScript.Binary\DefaultIcon" /ve /t REG_SZ /d "C:\Program Files\NebulaScript\icon.ico,0" /f >nul
if %errorlevel% neq 0 (
    echo [ERRO] Nao foi possivel associar o icon.ico aos ficheiros .cns.
    goto error
)

echo.
echo ===================================================
echo [SUCESSO] Instalacao concluida com sucesso!
echo ===================================================
echo Reinicie o seu terminal ou VS Code para usar o comando 'nebula'.
goto end

:error
echo.
echo [FALHA] A instalacao nao pôde ser concluida.

:end
echo.
pause

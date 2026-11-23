@echo off
setlocal
cd /d "%~dp0"
echo Buscando Visual Studio...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo No se encontro Visual Studio instalado.
  pause
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
  echo Falta instalar "Desarrollo para el escritorio con C++" en el Visual Studio Installer.
  pause
  exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
echo Compilando...
cl /nologo /EHsc /std:c++17 /utf-8 /O2 /W3 MinecraftSSTool.cpp /Fe:MinecraftSSTool.exe > compilacion_log.txt 2>&1
type compilacion_log.txt
echo.
if exist MinecraftSSTool.exe (
  echo LISTO: se creo MinecraftSSTool.exe en esta carpeta.
) else (
  echo ERROR al compilar. Mandale a Claude el archivo compilacion_log.txt
)
pause

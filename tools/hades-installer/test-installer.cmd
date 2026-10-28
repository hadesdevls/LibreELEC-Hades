@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Copyright (C) 2026-present Hades
rem LibreELEC Hades: test of the installer on a VIRTUAL disk (a .vhd file that Windows attaches as a disk).
rem It touches no real disk: it makes the file in the folder "pruebas", installs Hades on it in both ways,
rem updates it, removes it, checks that the test files are still intact and deletes the file.
rem It asks for administrator rights. The result is left in pruebas\prueba-vhd.txt
rem (Prueba del instalador en un disco virtual: no toca ningun disco real.)
net session >nul 2>&1
if errorlevel 1 (
  echo Asking for administrator rights...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
title LibreELEC Hades - test of the installer on a virtual disk
set IMG=
for %%f in ("%~dp0*.img.gz") do set IMG=%%f
if "%IMG%"=="" (echo There is no .img.gz image next to this file. & pause & exit /b 1)
echo Image: %IMG%
echo Testing on a virtual disk (a few minutes)...
start /wait "" "%~dp0LibreELEC-Hades-Installer.exe" --vhd-test "%~dp0pruebas" "%IMG%"
echo.
type "%~dp0pruebas\prueba-vhd.txt"
echo.
echo (You can close this window.)
pause >nul

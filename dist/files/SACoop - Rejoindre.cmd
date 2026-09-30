@echo off
rem SACoop : rejoint la partie coop d'un ami.
cd /d "%~dp0"
set /p SACOOP_IP=Adresse de l'hote (IP) : 
if "%SACOOP_IP%"=="" exit /b
start "" gta_sa.exe -sacoop invite %SACOOP_IP%

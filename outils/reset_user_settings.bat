@echo off
rem ===========================================================================
rem  reset_user_settings.bat - XPGAnalyser : reinitialisation volontaire des reglages (mis de cote, pas effaces)
rem ---------------------------------------------------------------------------
rem  Le travail est fait par lib\reset_user_settings.ps1 (Windows PowerShell 5.1, present dans
rem  Windows 10 et 11). Lance-le d'ici ou de n'importe quel dossier, par un
rem  double-clic ou dans un terminal ; les chemins avec des espaces ou des
rem  accents sont acceptes.
rem    reset_user_settings.bat /aide             les options
rem    reset_user_settings.bat /non-interactif   sans aucune question (valeurs par defaut)
rem  Journal : voir la premiere ligne affichee ; resume a la fin.
rem  Code de retour : 0 = reussi (la liste des codes : docs\GUIDE-COMPILATION.md).
rem ===========================================================================
setlocal EnableExtensions DisableDelayedExpansion
set "XPG_ICI=%~dp0"
set "XPG_NOM=%~nx0"
set "XPG_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%XPG_PS%" set "XPG_PS=powershell.exe"
if not exist "%XPG_ICI%lib\reset_user_settings.ps1" (
  echo [ERREUR] Script introuvable : "%XPG_ICI%lib\reset_user_settings.ps1"
  echo Le dossier lib\ doit rester a cote de reset_user_settings.bat.
  endlocal & exit /b 9009
)
rem Le script ecrit XPG_DEMARRE des qu'il demarre : sans cette marque, PowerShell n'a pas pu
rem le lancer (une strategie de groupe, le plus souvent), et on le dit en clair.
set "XPG_DEMARRE=%TEMP%\xpg-demarre-%RANDOM%%RANDOM%.tmp"
if exist "%XPG_DEMARRE%" del /q "%XPG_DEMARRE%" >nul 2>&1
"%XPG_PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%XPG_ICI%lib\reset_user_settings.ps1" %*
set "XPG_CODE=%ERRORLEVEL%"
if not exist "%XPG_DEMARRE%" if not "%XPG_CODE%"=="0" call :pas_demarre %*
if exist "%XPG_DEMARRE%" del /q "%XPG_DEMARRE%" >nul 2>&1
endlocal & exit /b %XPG_CODE%

:pas_demarre
echo.
echo [ERREUR] PowerShell n'a pas pu lancer lib\reset_user_settings.ps1 (code %XPG_CODE%) : voir le message ci-dessus.
"%XPG_PS%" -NoLogo -NoProfile -Command "$p = @(Get-ExecutionPolicy -Scope MachinePolicy; Get-ExecutionPolicy -Scope UserPolicy) | Where-Object { [string]$_ -ne 'Undefined' } | Select-Object -First 1; if ($p -and [string]$p -ne 'Bypass' -and [string]$p -ne 'Unrestricted') { Write-Host ('         Une strategie de groupe (GPO) impose ' + $p + ' : elle l''emporte sur -ExecutionPolicy Bypass,'); Write-Host '         et ces scripts ne la contournent pas. RemoteSigned : debloque le zip (Proprietes, Debloquer)'; Write-Host '         puis extrais-le a nouveau. Sinon : demande a l''administrateur du PC. Voir docs\DEPANNAGE.md.' } else { Write-Host '         Voir docs\DEPANNAGE.md ; collect_diagnostics.bat rassemble les journaux.' }"
rem Lance par un double-clic (ou un raccourci : cmd /c "...\reset_user_settings.bat") et pas en mode sans
rem surveillance : attendre Entree, sinon la fenetre se fermerait avant qu'on lise le message.
set "XPG_PAUSE="
setlocal EnableDelayedExpansion
set "XPG_LIGNE=!cmdcmdline!"
if /i not "!XPG_LIGNE:%XPG_NOM%=!"=="!XPG_LIGNE!" (endlocal & set "XPG_PAUSE=1") else endlocal
echo(%*| findstr /i /c:"non-interactif" >nul && set "XPG_PAUSE="
if defined XPG_PAUSE pause
exit /b 0

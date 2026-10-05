#Requires -Version 5.1
# =============================================================================
#  package.ps1 - lance par package.bat : TOUT, DANS L'ORDRE
#
#   1. les versions concordent (config.ini, CMakeLists.txt, Version.hpp, .rc) ;
#   2. la compilation Release (build.ps1) ;
#   3. les tests (test_build.ps1) ;
#   4. le dossier a livrer, dist\staging : l'exe, SDL3.dll, le runtime Visual C++
#      (deploiement local : un PC vierge n'a rien a installer), resources\,
#      libs\, maintenance\ (repair, verify...), licences\, LISEZ-MOI.txt ;
#   5. manifeste.json : chaque fichier, sa taille, son SHA-256, son role, et
#      les DLL dont l'exe a besoin (repair.bat et verify s'en servent) ;
#   6. l'installateur (build_installer.ps1) : XPGAnalyser-Setup-<version>.exe a la racine
#      du dossier, et son empreinte dans dist\SHA256SUMS.txt.
#
#  /exe:<fichier> /sdl:<SDL3.dll> /chaine:mingw : empaqueter un exe deja
#  compile ailleurs (c'est ainsi qu'a ete fait l'installateur livre avec la
#  chaine, sous Linux, avec MinGW : pas de runtime Visual C++ a y mettre).
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgOutils.psm1') -Force -Global -DisableNameChecking

$usage = @'
package.bat [options]
  /sans-tests          ne lance pas test_build (plus rapide ; déconseillé)
  /sans-installateur   s'arrête au dossier dist\staging (sans Inno Setup)
  /exe:<fichier>       empaquette cet exe au lieu de compiler (avec /sdl:<SDL3.dll>)
  /chaine:msvc|mingw   la chaîne qui a compilé l'exe (msvc par défaut : runtime VC++ livré)
'@
$X = Initialize-Xpg -Nom 'package' -Titre 'XPGAnalyser - compilation, tests et installateur' -Arguments $Arguments -Usage $usage
Save-XpgOrigine -Script 'package.ps1' -Arguments @($Arguments)

function Invoke-XpgEnfant {
    # Un script de lib\ (build, test_build, build_installer, install_build_tools) : ses questions
    # restent des questions (sauf si package.bat a lui-meme /non-interactif) ; /origine:package
    # lui dit qui reprendre apres un redemarrage de Windows.
    param([string] $Script, [string[]] $Args2, [string] $Libelle)
    $code = Start-XpgScriptEnfant -Script $Script -Arguments $Args2 -Origine 'package'
    if ($code -eq 3010) {
        Stop-Xpg -Code 3010 -Message 'Windows doit redémarrer pour finir l''installation des outils.' `
                 -Conseil 'Après le redémarrage, à l''ouverture de session, la procédure reprend toute seule et relance package.bat (ou relance-le toi-même).'
    }
    if ($code -ne 0) { Stop-Xpg -Code $code -Message "$Libelle a échoué (code $code)." -Conseil 'Son journal est dans outils\journaux\.' }
}

function Copy-XpgArbre {
    param([string] $De, [string] $Vers, [string[]] $Exclure = @())
    if (-not (Test-Path -LiteralPath $De)) { return 0 }
    New-Item -ItemType Directory -Path $Vers -Force | Out-Null
    $n = 0
    $base = (Get-Item -LiteralPath $De).FullName.TrimEnd('\', '/')
    foreach ($f in Get-ChildItem -LiteralPath $De -Recurse -File -Force) {
        $rel = $f.FullName.Substring($base.Length + 1)
        $sauter = $false
        foreach ($e in $Exclure) { if ($rel -like $e) { $sauter = $true } }
        if ($sauter) { continue }
        $cible = Join-Path $Vers $rel
        $d = Split-Path -Parent $cible
        if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Copy-Item -LiteralPath $f.FullName -Destination $cible -Force
        $n++
    }
    return $n
}

function Get-XpgDllImportees {
    # Les DLL qu'un exe PE importe (la table d'importation) : pour le manifeste et repair.bat.
    param([string] $Chemin)
    $b = [System.IO.File]::ReadAllBytes($Chemin)
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    $nbSections = [BitConverter]::ToUInt16($b, $pe + 6)
    $tailleOpt = [BitConverter]::ToUInt16($b, $pe + 20)
    $opt = $pe + 24
    $magic = [BitConverter]::ToUInt16($b, $opt)
    $dirs = $opt + $(if ($magic -eq 0x20b) { 112 } else { 96 })
    $rvaImport = [BitConverter]::ToUInt32($b, $dirs + 8)
    $sections = $opt + $tailleOpt
    function RvaVersFichier([uint32] $rva) {
        for ($i = 0; $i -lt $nbSections; $i++) {
            $s = $sections + 40 * $i
            $va = [BitConverter]::ToUInt32($b, $s + 12); $taille = [BitConverter]::ToUInt32($b, $s + 16); $brut = [BitConverter]::ToUInt32($b, $s + 20)
            $vtaille = [BitConverter]::ToUInt32($b, $s + 8); if ($vtaille -gt $taille) { $taille = $vtaille }
            if ($rva -ge $va -and $rva -lt ($va + $taille)) { return [int]($rva - $va + $brut) }
        }
        return -1
    }
    $noms = New-Object System.Collections.ArrayList
    if ($rvaImport -eq 0) { return @() }
    $off = RvaVersFichier $rvaImport
    while ($off -gt 0 -and $off + 20 -le $b.Length) {
        $rvaNom = [BitConverter]::ToUInt32($b, $off + 12)
        if ($rvaNom -eq 0) { break }
        $o = RvaVersFichier $rvaNom
        if ($o -lt 0) { break }
        $fin = $o; while ($fin -lt $b.Length -and $b[$fin] -ne 0) { $fin++ }
        [void]$noms.Add([System.Text.Encoding]::ASCII.GetString($b, $o, $fin - $o))
        $off += 20
    }
    return @($noms)
}

try {
    $racine = $X.Racine
    $produit = Get-XpgConfig -Section 'Produit' -Cle 'nom' -Obligatoire
    $exeNom = Get-XpgConfig -Section 'Produit' -Cle 'exe' -Defaut 'XpgAnalyzer.exe'
    $chaine = (Get-XpgOption -Noms @('chaine') -Defaut 'msvc').ToLowerInvariant()
    $exeFourni = Get-XpgOption -Noms @('exe')

    Start-XpgEtape 'Versions' | Out-Null
    $version = Test-XpgVersions
    if (-not $version) { Stop-Xpg -Code 2 -Message 'Versions incohérentes.' -Conseil 'configure.bat dit où les corriger.' }
    Complete-XpgEtape -Statut OK -Detail $version

    # Les outils d'abord, pour que les questions viennent au début : sans compilateur, build.ps1
    # pose la question du cahier des charges (six choix) et l'installation apporte aussi Inno
    # Setup ; avec un compilateur mais sans Inno Setup, on propose de l'installer ici.
    if (-not (Test-XpgOption @('sans-installateur'))) {
        $inno = Find-XpgInnoSetup
        $msvcLa = $null
        if (-not $exeFourni) { $msvcLa = Get-XpgChaineMsvc }
        if (-not $inno.Trouve -and ($exeFourni -or $msvcLa)) {
            Start-XpgEtape 'Inno Setup (pour l''installateur)' | Out-Null
            Invoke-XpgEnfant -Script 'install_build_tools.ps1' -Args2 @('/inno-seulement', '/depuis:package') -Libelle 'L''installation d''Inno Setup'
            Complete-XpgEtape -Statut OK
        }
    }

    if (-not $exeFourni) {
        Start-XpgEtape 'Compilation Release' | Out-Null
        Invoke-XpgEnfant -Script 'build.ps1' -Args2 @('-Action', 'build', '-Configuration', 'Release') -Libelle 'La compilation'
        Update-XpgConfig
        Complete-XpgEtape -Statut OK
    }
    if (-not (Test-XpgOption @('sans-tests'))) {
        Start-XpgEtape 'Tests' | Out-Null
        Invoke-XpgEnfant -Script 'test_build.ps1' -Args2 @('/sans-application') -Libelle 'Les tests'
        Complete-XpgEtape -Statut OK
    } else {
        Start-XpgEtape 'Tests' | Out-Null
        Complete-XpgEtape -Statut IGNORE -Detail '/sans-tests'
    }

    # ---- le dossier a livrer ------------------------------------------------------------
    Start-XpgEtape 'Le dossier à livrer (dist\staging)' | Out-Null
    $dist = Join-Path $racine (Get-XpgConfig -Section 'Installateur' -Cle 'sortie' -Defaut 'dist')
    $st = Join-Path $dist 'staging'
    if (Test-Path -LiteralPath $st) { Remove-Item -LiteralPath $st -Recurse -Force }
    New-Item -ItemType Directory -Path $st -Force | Out-Null
    $exe = $exeFourni
    if (-not $exe) { $exe = Join-Path $racine "build\Release\$exeNom" }
    if (-not (Test-Path -LiteralPath $exe)) { Stop-Xpg -Code 1 -Message "Exe introuvable : $exe" }
    Copy-Item -LiteralPath $exe -Destination (Join-Path $st $exeNom) -Force
    $sdl = Get-XpgOption -Noms @('sdl')
    if (-not $sdl) { $sdl = Join-Path $racine 'third_party\SDL3\lib\x64\SDL3.dll' }
    if (-not (Test-Path -LiteralPath $sdl)) { Stop-Xpg -Code 1 -Message "SDL3.dll introuvable : $sdl" }
    Copy-Item -LiteralPath $sdl -Destination (Join-Path $st 'SDL3.dll') -Force
    $runtime = @()
    if ($chaine -eq 'msvc') {
        $msvc = Get-XpgChaineMsvc
        $rt = $null
        if ($msvc) { $rt = Find-XpgRuntimeVc -Instance $msvc.Instance.Chemin }
        if (-not $rt -or -not $rt.Dll) { Stop-Xpg -Code 1 -Message 'Le runtime Visual C++ à livrer est introuvable (VC\Redist\MSVC\...\x64\Microsoft.VC143.CRT).' -Conseil 'Il vient avec les Build Tools : check_environment.bat le vérifie.' }
        foreach ($d in $rt.Dll) { Copy-Item -LiteralPath $d -Destination $st -Force; $runtime += [System.IO.Path]::GetFileName($d) }
        Write-XpgLog -Niveau INFO -Message ("Runtime Visual C++ (déploiement local) : {0}" -f ($runtime -join ', '))
    } else {
        Write-XpgLog -Niveau INFO -Message "Chaîne $chaine : pas de runtime Visual C++ à livrer (runtime C++ lié statiquement)."
    }
    $nRes = 0
    foreach ($f in @('schneider_library.txt', 'plc_catalog.txt')) {
        $s = Join-Path $racine "resources\$f"
        if (-not (Test-Path -LiteralPath $s)) { Stop-Xpg -Code 1 -Message "Ressource absente : $s" }
        New-Item -ItemType Directory -Path (Join-Path $st 'resources') -Force | Out-Null
        Copy-Item -LiteralPath $s -Destination (Join-Path $st "resources\$f") -Force; $nRes++
    }
    $nRes += Copy-XpgArbre -De (Join-Path $racine 'resources\fonts') -Vers (Join-Path $st 'resources\fonts')
    $nLibs = Copy-XpgArbre -De (Join-Path $racine 'libs') -Vers (Join-Path $st 'libs') -Exclure @('*.bak', '*~', '~$*')
    # La maintenance : les six scripts et ce dont ils ont besoin (pas ceux de la compilation).
    $m = Join-Path $st 'maintenance'
    New-Item -ItemType Directory -Path (Join-Path $m 'lib') -Force | Out-Null
    foreach ($b in @('repair', 'verify_installation', 'restore_backup', 'resume_installation', 'reset_user_settings', 'collect_diagnostics')) {
        Copy-Item -LiteralPath (Join-Path $X.Outils "$b.bat") -Destination $m -Force
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "$b.ps1") -Destination (Join-Path $m 'lib') -Force
    }
    foreach ($f in @('XpgCommun.psm1', 'XpgInstallation.psm1', 'installer_etape.ps1', 'empreintes-livrees.txt')) {
        $s = Join-Path $PSScriptRoot $f
        if (Test-Path -LiteralPath $s) { Copy-Item -LiteralPath $s -Destination (Join-Path $m 'lib') -Force }
    }
    # config.ini de la maintenance : ce qu'elle doit savoir du produit (pas les outils de compilation).
    $ini = @"
; maintenance\config.ini - ecrit par package.bat ($((Get-Date).ToString('yyyy-MM-dd HH:mm'))) depuis outils\config.ini.
; Ce que les scripts de maintenance doivent savoir de XPGAnalyser. Ne pas modifier.
[Produit]
nom = $produit
editeur = $(Get-XpgConfig -Section 'Produit' -Cle 'editeur')
version = $version
app_id = $(Get-XpgConfig -Section 'Produit' -Cle 'app_id' -Obligatoire)
exe = $exeNom
chaine = $chaine

[Maintenance]
sauvegardes_a_garder = $(Get-XpgConfig -Section 'Maintenance' -Cle 'sauvegardes_a_garder' -Defaut '3')
anciens_dossiers = $(Get-XpgConfig -Section 'Installateur' -Cle 'anciens_dossiers')
"@
    Write-XpgTexte -Chemin (Join-Path $m 'config.ini') -Texte ($ini -replace "`r?`n", "`r`n")
    # Les licences des bibliotheques tierces.
    $lic = Join-Path $st 'licences'
    New-Item -ItemType Directory -Path $lic -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $racine 'third_party\SDL3\LICENSE.txt') -Destination (Join-Path $lic 'SDL3-LICENSE.txt') -Force
    foreach ($f in Get-ChildItem -LiteralPath (Join-Path $racine 'third_party') -Filter '*.LICENSE' -File) { Copy-Item -LiteralPath $f.FullName -Destination $lic -Force }
    $stb = [System.IO.File]::ReadAllText((Join-Path $racine 'third_party\stb_image.h'))
    $i = $stb.IndexOf('This software is available under 2 licenses')
    if ($i -ge 0) { Write-XpgTexte -Chemin (Join-Path $lic 'stb.LICENSE') -Texte ($stb.Substring($i).Replace('*/', '').Trim() + "`r`n") -SansBom }
    $lisez = Join-Path $racine 'installateur\LISEZ-MOI.txt'
    if (Test-Path -LiteralPath $lisez) {
        $t = [System.IO.File]::ReadAllText($lisez, [System.Text.Encoding]::UTF8).Replace('{VERSION}', $version)
        Write-XpgTexte -Chemin (Join-Path $st 'LISEZ-MOI.txt') -Texte (($t -replace "`r?`n", "`n") -replace "`n", "`r`n")
    }
    Complete-XpgEtape -Statut OK -Detail ("exe, SDL3.dll, {0} DLL du runtime, {1} ressource(s), {2} fichier(s) de bibliothèque, 6 scripts de maintenance" -f $runtime.Count, $nRes, $nLibs)

    # ---- le manifeste ------------------------------------------------------------------------
    Start-XpgEtape 'Le manifeste (tailles et SHA-256)' | Out-Null
    $dll = @(Get-XpgDllImportees (Join-Path $st $exeNom))
    $systeme = @('kernel32', 'user32', 'gdi32', 'shell32', 'ole32', 'oleaut32', 'advapi32', 'comdlg32', 'ws2_32', 'iphlpapi', 'winmm', 'imm32', 'setupapi', 'version',
                 'dwmapi', 'uxtheme', 'shlwapi', 'bcrypt', 'winspool', 'uuid', 'msvcrt', 'ucrtbase', 'crypt32', 'dbghelp', 'hid', 'cfgmgr32', 'dinput8', 'xinput1_4', 'powrprof')
    $aLivrer = @($dll | Where-Object { $n = $_.ToLowerInvariant() -replace '\.dll$', ''; -not ($systeme -contains $n) -and $n -notlike 'api-ms-win-*' })
    foreach ($d in $aLivrer) { if (-not (Test-Path -LiteralPath (Join-Path $st $d))) { Stop-Xpg -Code 1 -Message "L'exe demande $d, qui n'est pas dans le dossier à livrer." -Conseil 'Un PC vierge ne l''aurait pas : voir le runtime (package.bat) et SDL3.dll.' } }
    $fichiers = New-Object System.Collections.ArrayList
    $base = (Get-Item -LiteralPath $st).FullName.TrimEnd('\', '/')
    foreach ($f in Get-ChildItem -LiteralPath $st -Recurse -File | Sort-Object FullName) {
        $rel = $f.FullName.Substring($base.Length + 1).Replace('/', '\')
        $role = 'programme'
        if ($rel -like 'resources\*' -or $rel -like 'libs\*') { $role = 'donnee-livree' }
        elseif ($rel -like 'maintenance\*') { $role = 'maintenance' }
        elseif ($rel -like 'licences\*' -or $rel -like '*.txt') { $role = 'document' }
        [void]$fichiers.Add([ordered]@{ chemin = $rel; taille = $f.Length; sha256 = (Get-XpgSha256 $f.FullName); role = $role })
    }
    $manifeste = [ordered]@{
        produit = $produit; version = $version; chaine = $chaine; date = (Get-Date).ToString('s')
        exe = $exeNom; dependances = @($aLivrer); runtime_local = @($runtime)
        fichiers = @($fichiers)
    }
    Write-XpgTexte -Chemin (Join-Path $st 'manifeste.json') -Texte ($manifeste | ConvertTo-Json -Depth 5) -SansBom
    $total = ($fichiers | ForEach-Object { $_.taille } | Measure-Object -Sum).Sum
    Complete-XpgEtape -Statut OK -Detail ("{0} fichiers, {1} ; DLL à livrer : {2}" -f $fichiers.Count, (Format-XpgTaille $total), ($aLivrer -join ', '))

    if (Test-XpgOption @('sans-installateur')) {
        Start-XpgEtape 'Installateur' | Out-Null
        Complete-XpgEtape -Statut IGNORE -Detail '/sans-installateur'
        Exit-Xpg -Code 0
    }
    Start-XpgEtape 'L''installateur (Inno Setup)' | Out-Null
    Invoke-XpgEnfant -Script 'build_installer.ps1' -Args2 @("/staging:$st", "/chaine:$chaine") -Libelle 'La fabrication de l''installateur'
    Complete-XpgEtape -Statut OK -Detail (Join-Path $racine ("{0}-Setup-{1}.exe" -f $produit, $version))
    Exit-Xpg -Code 0
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

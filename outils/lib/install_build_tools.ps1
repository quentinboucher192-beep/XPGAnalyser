#Requires -Version 5.1
# =============================================================================
#  install_build_tools.ps1 - lance par install_build_tools.bat (et par build.bat
#  quand il n'y a pas de compilateur) : l'installation ASSISTEE des outils.
#
#  Rien n'est telecharge ni installe sans ton accord explicite. Avant : le
#  nom, l'editeur, les composants, la raison, les droits, Internet, l'espace
#  disque, les conditions, les autres solutions. Les fichiers : sources
#  officielles de config.ini, en HTTPS, signature Authenticode verifiee (et le
#  SHA-256 quand config.ini en donne un) ; refuses sinon. Les droits
#  d'administrateur : demandes au moment de l'installation des Build Tools,
#  pour elle seule. Jamais : desactiver l'antivirus, contourner une strategie,
#  redemarrer sans ta confirmation.
#
#  Codes : 0 installe et verifie (ou rien a faire) ; 2 annule ; 3010 un
#  redemarrage est necessaire (la suite reprendra apres) ; 10 echec.
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgOutils.psm1') -Force -Global -DisableNameChecking

$usage = @'
install_build_tools.bat [options]
  /choix:1..6        la réponse au menu (1 installer, 2 autre chaîne, 3 compilateur déjà installé,
                     4 package hors ligne, 5 prérequis, 6 annuler)
  /oui               accepte l'installation ET les conditions de licence de Microsoft sans les
                     demander (mode sans surveillance : les Build Tools s'installent en --passive)
  /dossier:<chemin>  avec /choix:3 : le dossier d'une installation de Visual Studio / Build Tools ;
                     avec /choix:4 : le package hors ligne (sinon [HorsLigne] dossier de config.ini)
  /installateur:<f>  avec /choix:4 : un vs_BuildTools.exe que tu as déjà
  /reprendre         reprend après un redémarrage (lancé tout seul à l'ouverture de session),
                     puis relance ce qui avait été demandé (package.bat, build.bat, test_build.bat)
  /inno-seulement    seulement Inno Setup (pour fabriquer l'installateur), avec ton accord
  /forcer            propose l'installation même si tout est déjà là
'@
$X = Initialize-Xpg -Nom 'install_build_tools' -Titre 'XPGAnalyser - installation assistée des outils de compilation' -Arguments $Arguments -Usage $usage

$etatFichier = Join-Path $X.Outils 'etat\install_build_tools.json'
# /depuis:<script> : un script (build, package) nous a lance et continue apres nous.
# /origine:<script> : ce que tu as lance au depart (package, build, build_debug, rebuild,
# test_build) : c'est lui qu'on relance apres un redemarrage de Windows.
$depuis = Get-XpgOption -Noms @('depuis')
$origine = Get-XpgOption -Noms @('origine')
$script:reprise = $false
# Ce qu'il faudra relancer apres un redemarrage : le script d'origine ET ses options, tels que
# ce script les a notes au depart (etat\origine.json, voir Save-XpgOrigine).
$script:relance = $null
# Le package hors ligne du choix 4 (garde dans l'etat : la reprise apres un redemarrage en a besoin).
$script:dossierHorsLigne = ''
$script:innoLocal = ''
$composants = @((Get-XpgConfig -Section 'Outils' -Cle 'buildtools_composants' -Defaut 'Microsoft.VisualStudio.Workload.VCTools;Microsoft.VisualStudio.Component.VC.Tools.x86.x64;Microsoft.VisualStudio.Component.Windows11SDK.22621;Microsoft.VisualStudio.Component.VC.CMake.Project') -split ';' | Where-Object { $_ })
$nomsComposants = @{
    'Microsoft.VisualStudio.Workload.VCTools' = 'Développement Desktop en C++ (la charge de travail, sans ses options)'
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' = 'MSVC v143, outils de génération x64/x86 (cl, link, lib)'
    'Microsoft.VisualStudio.Component.Windows11SDK.22621' = 'SDK Windows 11 (10.0.22621) : en-têtes et bibliothèques de Windows'
    'Microsoft.VisualStudio.Component.Windows11SDK.26100' = 'SDK Windows 11 (10.0.26100)'
    'Microsoft.VisualStudio.Component.VC.CMake.Project' = 'Outils CMake C++ pour Windows (CMake et Ninja, pour les tests)'
}

function Show-XpgPrerequis {
    Write-Host ''
    Write-XpgLog -Niveau TITRE -Message 'Les prérequis pour compiler XPGAnalyser'
    Write-Host '  Windows 10 (1903 ou plus) ou 11, 64 bits ; PowerShell 5.1 (déjà dans Windows).'
    Write-Host '  Microsoft C++ Build Tools 2022, ces composants seulement :'
    foreach ($c in $composants) { $n = $nomsComposants[$c]; if (-not $n) { $n = '' }; Write-Host ("    - {0}  {1}" -f $c, $n) }
    Write-Host '    (MSBuild fait partie des Build Tools ; l''interface de Visual Studio n''est PAS installée.)'
    Write-Host ("  Inno Setup {0} (pour fabriquer l'installateur ; installé pour toi seul, sans droits)" -f (Get-XpgConfig -Section 'Outils' -Cle 'innosetup_version' -Defaut '6'))
    Write-Host ("  Espace disque : environ {0} Go sur C: ; Internet : environ 2 à 3 Go à télécharger." -f (Get-XpgConfig -Section 'Outils' -Cle 'buildtools_espace_go' -Defaut '7'))
    Write-Host '  Droits d''administrateur : pour installer les Build Tools seulement.'
    Write-Host ("  Sources : {0}" -f (Get-XpgConfig -Section 'Outils' -Cle 'buildtools_url'))
    Write-Host ("            {0}" -f (Get-XpgConfig -Section 'Outils' -Cle 'innosetup_url'))
    Write-Host '  Sans Internet : create_offline_toolchain.bat, sur un PC connecté, prépare un package hors ligne.'
}

function Show-XpgDetailsInstallation {
    Write-Host ''
    Write-XpgLog -Niveau TITRE -Message 'Avant d''installer'
    Write-Host '  Compilateur  Microsoft C++ Build Tools 2022 (MSVC v143, MSBuild, SDK Windows)'
    Write-Host '  Éditeur      Microsoft Corporation'
    Write-Host '  Composants   seulement ceux-ci (pas l''interface de Visual Studio) :'
    foreach ($c in $composants) { $n = $nomsComposants[$c]; if (-not $n) { $n = $c }; Write-Host "               - $n" }
    Write-Host '  Pourquoi     XPGAnalyser est un projet Visual Studio 2022 (v143, x64, C++20) et le paquet'
    Write-Host '               SDL3 fourni est compilé pour MSVC : même compilateur, même ABI, même runtime.'
    Write-Host '  Droits       administrateur, pour l''installation seulement (Windows le demandera)'
    Write-Host ("  Internet     oui : environ 2 à 3 Go depuis {0}" -f ([Uri](Get-XpgConfig -Section 'Outils' -Cle 'buildtools_url')).Host)
    $lib = (Get-PSDrive -Name 'C' -ErrorAction SilentlyContinue).Free
    $txt = ''; if ($lib) { $txt = " (libre : $(Format-XpgTaille $lib))" }
    Write-Host ("  Disque       environ {0} Go sur C:{1}" -f (Get-XpgConfig -Section 'Outils' -Cle 'buildtools_espace_go' -Defaut '7'), $txt)
    Write-Host ("  Conditions   licence de Microsoft Visual Studio Build Tools ({0}) :" -f (Get-XpgConfig -Section 'Outils' -Cle 'buildtools_licence' -Defaut 'https://visualstudio.microsoft.com/license-terms/'))
    Write-Host '               l''installateur de Microsoft l''affiche ; avec /oui, tu l''acceptes d''avance.'
    Write-Host ("  Et aussi     Inno Setup {0} (jrsoftware.org, libre), pour fabriquer l'installateur ;" -f (Get-XpgConfig -Section 'Outils' -Cle 'innosetup_version' -Defaut '6'))
    Write-Host '               pour toi seul, sans droits d''administrateur.'
    Write-Host '  Autres       [2] MinGW-w64 (GCC) ou LLVM/Clang ; [4] un package hors ligne préparé ailleurs'
}

function Save-XpgEtatOutils {
    param([string] $Etape)
    $relance = $script:relance
    $fo = Join-Path $X.Outils 'etat\origine.json'
    if (-not $relance -and $origine -and (Test-Path -LiteralPath $fo)) { $relance = Read-XpgEtat $fo }
    Save-XpgEtat -Chemin $etatFichier -Etat ([pscustomobject]@{ etape = $Etape; depuis = $depuis; origine = $origine; relance = $relance; hors_ligne = $script:dossierHorsLigne; journal = $X.Journal })
}

function Install-XpgBuildTools {
    # L'installation des Build Tools depuis un installateur verifie (telecharge, ou d'un
    # package hors ligne) ; eleve pour elle seule. Rend le code de l'installateur.
    param([string] $Installateur, [switch] $HorsLigne)
    $prov = Test-XpgProvenance -Chemin $Installateur -Editeur (Get-XpgConfig -Section 'Outils' -Cle 'buildtools_editeur' -Defaut 'Microsoft Corporation') `
                               -Sha256 (Get-XpgConfig -Section 'Outils' -Cle 'buildtools_sha256')
    if (-not $prov.Ok) {
        Stop-Xpg -Code 10 -Message "Installateur refusé : $($prov.Raison)." -Conseil 'Un fichier dont la provenance n''est pas validée n''est jamais lancé. Télécharge-le à nouveau depuis la source officielle.'
    }
    Write-XpgLog -Niveau OK -Message "Provenance validée : $($prov.Signataire)"
    $dossier = Get-XpgConfig -Section 'Outils' -Cle 'buildtools_dossier' -Chemin
    $a = @()
    foreach ($c in $composants) { $a += @('--add', $c) }
    if ($dossier) { $a += @('--installPath', "`"$dossier`"") }
    $a += @('--wait', '--norestart', '--nocache')
    if ($HorsLigne) { $a += '--noWeb' }
    if ($X.Oui -or $X.NonInteractif) { $a += '--passive' }
    Save-XpgEtatOutils -Etape 'installation-buildtools'
    Write-XpgLog -Niveau INFO -Message ("Lancement : {0} {1}" -f $Installateur, ($a -join ' '))
    Write-XpgLog -Niveau INFO -Message 'Windows va demander les droits d''administrateur (Contrôle de compte d''utilisateur) : pour cette installation seulement.'
    try {
        if (Test-XpgAdmin) { $p = Start-Process -FilePath $Installateur -ArgumentList $a -Wait -PassThru }
        else { $p = Start-Process -FilePath $Installateur -ArgumentList $a -Verb RunAs -Wait -PassThru }
    } catch {
        Stop-Xpg -Code 10 -Message "L'installation n'a pas pu démarrer : $($_.Exception.Message)" -Conseil 'Si tu as refusé les droits d''administrateur, relance quand tu voudras : rien n''a été installé.'
    }
    $code = $p.ExitCode
    Write-XpgLog -Niveau INFO -Message "Installateur des Build Tools : code de retour $code"
    return $code
}

function Invoke-XpgApresInstallation {
    # Paragraphe 6 du cahier des charges : actualiser, redetecter, versions, SDK, essai
    # (compiler, lier, lancer), enregistrer ; puis Inno Setup ; puis reprendre.
    Start-XpgEtape 'Actualiser l''environnement et détecter à nouveau' | Out-Null
    Update-XpgPathDepuisRegistre
    $msvc = Get-XpgChaineMsvc
    if (-not $msvc) { Complete-XpgEtape -Statut ECHEC; Stop-Xpg -Code 10 -Message 'Les Build Tools sont installés mais MSVC x64 n''est pas détecté.' -Conseil 'Lance check_environment.bat ; joins les journaux (collect_diagnostics.bat).' }
    $min = Get-XpgConfig -Section 'Compilateur' -Cle 'msvc_version_min' -Defaut '14.30'
    $vok = $true
    try { $vok = ([version]($msvc.Msvc.VersionOutils -replace '[^\d\.].*$', '')) -ge [version]$min } catch { }
    Complete-XpgEtape -Statut $(if ($vok) { 'OK' } else { 'ATTENTION' }) -Detail ("MSVC {0} (minimum {1}), MSBuild {2}" -f $msvc.Msvc.VersionOutils, $min, (Get-XpgVersionMsBuild $msvc.MsBuild))

    Start-XpgEtape 'SDK Windows' | Out-Null
    $sdk = Find-XpgSdkWindows
    if (-not $sdk.Trouve) { Complete-XpgEtape -Statut ECHEC; Stop-Xpg -Code 10 -Message 'Le SDK Windows est absent après l''installation.' -Conseil 'Ajoute le composant SDK Windows (config.ini, buildtools_composants).' }
    Complete-XpgEtape -Statut OK -Detail $sdk.Version

    Start-XpgEtape 'Compilation d''essai, édition des liens, lancement' | Out-Null
    Import-XpgEnvironnementVs -Instance $msvc.Instance.Chemin
    $essai = Test-XpgCompilation -Chaine msvc -Compilateur $msvc.Msvc.Cl
    if (-not $essai.Ok) { Complete-XpgEtape -Statut ECHEC -Detail $essai.Raison; Stop-Xpg -Code 10 -Message "L'essai du compilateur échoue : $($essai.Raison)" }
    Complete-XpgEtape -Statut OK -Detail ("code {0}, {1}" -f $essai.Code, $essai.Architecture)
    Save-XpgEtat -Chemin (Join-Path $X.Outils 'etat\compilateur.json') -Etat ([pscustomobject]@{ chaine = 'msvc'; instance = $msvc.Instance.Chemin; msvc = $msvc.Msvc.VersionOutils; sdk = $sdk.Version; essai = 'ok' })
    Write-XpgLog -Niveau INFO -Message 'Résultat enregistré : outils\etat\compilateur.json'
    Install-XpgInnoSetupSiAbsent
}

function Find-XpgInnoHorsLigne {
    # L'installateur d'Inno Setup d'un package hors ligne (create_offline_toolchain.bat) : celui du
    # choix 4, sinon le package de /dossier: ou de config.ini ([HorsLigne] dossier). Sa provenance
    # est verifiee comme celle d'un fichier telecharge (signature, SHA-256 de config.ini).
    if ($script:innoLocal -and (Test-Path -LiteralPath $script:innoLocal)) { return $script:innoLocal }
    foreach ($d in @($script:dossierHorsLigne, (Get-XpgOption -Noms @('dossier')), (Get-XpgConfig -Section 'HorsLigne' -Cle 'dossier' -Chemin))) {
        if (-not $d -or -not (Test-Path -LiteralPath $d)) { continue }
        $f = Get-ChildItem -LiteralPath (Join-Path $d 'innosetup') -Filter 'innosetup-*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($f) { return $f.FullName }
    }
    return ''
}

function Install-XpgInnoSetupSiAbsent {
    Start-XpgEtape 'Inno Setup (pour l''installateur)' | Out-Null
    $inno = Find-XpgInnoSetup
    if ($inno.Trouve) { Complete-XpgEtape -Statut OK -Detail "déjà là : $($inno.Version)"; return }
    $url = Get-XpgConfig -Section 'Outils' -Cle 'innosetup_url' -Obligatoire
    $fichier = Find-XpgInnoHorsLigne
    if ($fichier) { Write-XpgLog -Niveau INFO -Message "Inno Setup depuis le package hors ligne : $fichier" }
    if (-not $fichier) {
        if (-not (Test-XpgInternet $url)) { Complete-XpgEtape -Statut ATTENTION -Detail 'pas d''Internet, ni de package hors ligne ([HorsLigne] dossier de config.ini, ou /dossier:)'; return }
        $fichier = Join-Path ([System.IO.Path]::GetTempPath()) ('xpg-outils\' + [System.IO.Path]::GetFileName(([Uri]$url).AbsolutePath))
        Save-XpgFichierWeb -Url $url -Destination $fichier
    }
    $prov = Test-XpgProvenance -Chemin $fichier -Editeur (Get-XpgConfig -Section 'Outils' -Cle 'innosetup_editeur') -Sha256 (Get-XpgConfig -Section 'Outils' -Cle 'innosetup_sha256')
    if (-not $prov.Ok) { Complete-XpgEtape -Statut ECHEC -Detail $prov.Raison; Stop-Xpg -Code 10 -Message "Installateur d'Inno Setup refusé : $($prov.Raison)." }
    $code = Invoke-XpgProcessus -Fichier $fichier -Arguments @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/CURRENTUSER', "/LOG=`"$($X.DossierJournaux)\innosetup-installation.log`"") -Libelle 'installateur d''Inno Setup'
    Start-Sleep -Seconds 2
    $inno = Find-XpgInnoSetup
    if ($code -ne 0 -or -not $inno.Trouve) { Complete-XpgEtape -Statut ECHEC -Detail "code $code"; Stop-Xpg -Code 10 -Message 'Inno Setup ne s''est pas installé.' -Conseil 'Son journal : outils\journaux\innosetup-installation.log' }
    Complete-XpgEtape -Statut OK -Detail "$($inno.Version) : $($inno.Chemin)"
}

function Invoke-XpgFin {
    # La suite. Sans redemarrage, le script qui nous a lance (build, package) est toujours la :
    # il continue. Apres un redemarrage (ou lance seul), on relance ce qui avait ete demande.
    if (Test-Path -LiteralPath $etatFichier) { Remove-Item -LiteralPath $etatFichier -Force -ErrorAction SilentlyContinue }
    if ($depuis -and -not $script:reprise) { Write-XpgLog -Niveau OK -Message 'Outils prêts : la suite reprend.'; Exit-Xpg -Code 0 }
    $suite = 'build'
    if (@('package', 'test_build', 'build_debug', 'rebuild') -contains $origine) { $suite = $origine }
    $question = "Outils prêts. Reprendre $suite.bat ?"
    if ($suite -eq 'package') { $question = 'Outils prêts. Reprendre package.bat (compilation, tests, installateur) ?' }
    elseif ($suite -eq 'build') { $question = 'Outils prêts. Compiler XPGAnalyser maintenant (build.bat) ?' }
    if (Confirm-Xpg -Question $question -Defaut $true) {
        Start-XpgEtape "Reprise : $suite.bat" | Out-Null
        $cible = 'build.ps1'
        $a = @('-Action', 'build', '-Configuration', 'Release')
        if ($suite -eq 'package' -or $suite -eq 'test_build') { $cible = "$suite.ps1"; $a = @() }
        elseif ($suite -eq 'build_debug') { $a = @('-Action', 'build', '-Configuration', 'Debug') }
        elseif ($suite -eq 'rebuild') { $a = @('-Action', 'rebuild', '-Configuration', 'Release') }
        # Apres un redemarrage : ce que tu avais lance, avec tes options (/debug, /sans-tests, /filtre:...).
        if ($script:reprise -and $script:relance -and $script:relance.script -and (Test-Path -LiteralPath (Join-Path $X.Lib ([string]$script:relance.script)))) {
            $cible = [string]$script:relance.script
            $a = @(@($script:relance.arguments) | Where-Object { $_ } | ForEach-Object { [string]$_ })
            Write-XpgLog -Niveau INFO -Message ("Relance : {0} {1}" -f $cible, ($a -join ' '))
        }
        $code = Start-XpgScriptEnfant -Script $cible -Arguments $a
        Complete-XpgEtape -Statut $(if ($code -eq 0) { 'OK' } else { 'ECHEC' }) -Detail "code $code"
        Exit-Xpg -Code $code
    }
    Exit-Xpg -Code 0
}

function Request-XpgRedemarrage {
    Save-XpgEtatOutils -Etape 'redemarrage'
    # La reprise a l'ouverture de session suivante (RunOnce : une seule fois). L'etat garde
    # /depuis et /origine ; la ligne garde le mode (sans surveillance ou non).
    $bat = Join-Path $X.Outils 'install_build_tools.bat'
    $modes = ''
    if ($X.NonInteractif) { $modes += ' /non-interactif' }
    if ($X.Oui) { $modes += ' /oui' }
    $cmd = "cmd.exe /c `"`"$bat`" /reprendre$modes`""
    New-Item -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\RunOnce' -Force | Out-Null
    Set-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\RunOnce' -Name 'XPGAnalyser-outils' -Value $cmd
    Write-XpgLog -Niveau INFO -Message 'État enregistré ; la procédure reprendra toute seule à la prochaine ouverture de session.'
    if (Confirm-Xpg -Question 'Windows doit redémarrer pour finir. Redémarrer MAINTENANT ? (enregistre ton travail avant)' -Defaut $false) {
        Write-XpgLog -Niveau INFO -Message 'Redémarrage demandé par toi.'
        Write-XpgResume -Code 3010
        Restart-Computer
        exit 3010
    }
    Stop-Xpg -Code 3010 -Message 'Redémarrage à faire quand tu veux.' -Conseil 'Après le redémarrage, la procédure reprend toute seule (ou relance install_build_tools.bat).'
}

try {
    # ---- la reprise apres un redemarrage ---------------------------------------------
    if (Test-XpgOption @('reprendre')) {
        $script:reprise = $true
        $e = Read-XpgEtat $etatFichier
        if ($e -and $e.depuis) { $script:depuis = [string]$e.depuis; $depuis = [string]$e.depuis }
        if ($e -and $e.origine) { $script:origine = [string]$e.origine; $origine = [string]$e.origine }
        if ($e -and $e.relance) { $script:relance = $e.relance }
        if ($e -and $e.hors_ligne) { $script:dossierHorsLigne = [string]$e.hors_ligne }
        Write-XpgLog -Niveau INFO -Message "Reprise après redémarrage (étape enregistrée : $(if ($e) { $e.etape } else { 'inconnue' }) ; ensuite : $(if ($origine) { "$origine.bat" } else { 'build.bat' }))"
        Invoke-XpgApresInstallation
        Invoke-XpgFin
    }

    # ---- seulement Inno Setup (package.bat, build_installer.bat) ---------------------------
    if (Test-XpgOption @('inno-seulement')) {
        $inno = Find-XpgInnoSetup
        if (-not $inno.Trouve -and -not (Confirm-Xpg -Question 'Inno Setup manque (pour fabriquer l''installateur). L''installer pour toi seul, sans droits d''administrateur ?' -Defaut $true)) {
            Write-XpgLog -Niveau INFO -Message 'Rien n''est installé.'
            Exit-Xpg -Code 2
        }
        Install-XpgInnoSetupSiAbsent
        if (Test-Path -LiteralPath $etatFichier) { Remove-Item -LiteralPath $etatFichier -Force -ErrorAction SilentlyContinue }
        # Pas d'Internet (ou refus) : Inno Setup n'est toujours pas la ; on le dit tout de suite,
        # plutot qu'au bout de la compilation et des tests.
        if (-not (Find-XpgInnoSetup).Trouve) {
            Stop-Xpg -Code 11 -Message 'Inno Setup n''a pas pu être installé : l''installateur ne pourra pas être fabriqué.' `
                     -Conseil 'Sans Internet : un package hors ligne (create_offline_toolchain.bat, sur un PC connecté), indiqué dans config.ini ([HorsLigne] dossier) ou par /dossier:<package>. Pour compiler et tester sans l''installateur : package.bat /sans-installateur.'
        }
        Exit-Xpg -Code 0
    }

    Start-XpgEtape 'Ce qui est déjà là' | Out-Null
    $msvc = Get-XpgChaineMsvc
    $inno = Find-XpgInnoSetup
    $cmake = Find-XpgCMake -Instance $(if ($msvc) { $msvc.Instance.Chemin } else { '' })
    if ($msvc) { Write-XpgLog -Niveau OK -Message "MSVC $($msvc.Msvc.VersionOutils) : $($msvc.Instance.Chemin)" }
    if ($inno.Trouve) { Write-XpgLog -Niveau OK -Message "Inno Setup $($inno.Version)" }
    Complete-XpgEtape -Statut OK
    if ($msvc -and $inno.Trouve -and $cmake.Trouve -and -not (Test-XpgOption @('forcer'))) {
        Write-XpgLog -Niveau OK -Message 'Rien à installer : les outils sont là. (/forcer pour réinstaller.)'
        Invoke-XpgFin
    }
    if ($msvc -and -not (Test-XpgOption @('forcer'))) {
        # Le compilateur est la : seulement ce qui manque (Inno Setup ; CMake/Ninja : composant).
        if (-not $cmake.Trouve) { Write-XpgLog -Niveau ATTENTION -Message 'CMake/Ninja manquent (pour test_build.bat) : ajoute le composant « C++ CMake tools for Windows » avec Visual Studio Installer, ou relance avec /forcer.' }
        if (-not $inno.Trouve) {
            if (-not (Confirm-Xpg -Question 'Inno Setup manque (pour fabriquer l''installateur). L''installer pour toi seul, sans droits d''administrateur ?' -Defaut $true)) { Exit-Xpg -Code 2 }
            Install-XpgInnoSetupSiAbsent
        }
        Invoke-XpgFin
    }

    # ---- le message et les six choix ---------------------------------------------------
    $message = 'Aucun compilateur C++ compatible n''a été détecté sur cet ordinateur. Un compilateur est nécessaire pour générer le logiciel. Voulez-vous installer automatiquement les outils de compilation recommandés ?'
    if ($msvc) { $message = 'Réinstallation demandée (/forcer). Voulez-vous installer automatiquement les outils de compilation recommandés ?' }
    $options = @('Installer automatiquement le compilateur recommandé', 'Choisir une autre chaîne compatible', 'Sélectionner manuellement un compilateur déjà installé',
                 'Utiliser un package hors ligne', 'Afficher les prérequis', 'Annuler')
    $choixLigne = Get-XpgOption -Noms @('choix')
    while ($true) {
        if ($choixLigne) { $choix = [int]$choixLigne; $choixLigne = ''; Write-XpgLog -Niveau INFO -Message "Choix donné sur la ligne de commande : $choix" }
        else { $choix = Read-XpgChoix -Question $message -Options $options -Defaut $(if ($X.Oui) { 1 } else { 0 }) -OptionLigne '/choix:1 à 6' }
        switch ($choix) {
            1 {
                Show-XpgDetailsInstallation
                if (-not (Confirm-Xpg -Question 'Installer les Microsoft C++ Build Tools et Inno Setup ?' -Defaut $false)) { Write-XpgLog -Niveau INFO -Message 'Rien n''est installé.'; continue }
                Start-XpgEtape 'Connexion Internet' | Out-Null
                $url = Get-XpgConfig -Section 'Outils' -Cle 'buildtools_url' -Obligatoire
                if (-not (Test-XpgInternet $url)) {
                    Complete-XpgEtape -Statut ATTENTION -Detail "pas de réponse de $(([Uri]$url).Host) en 8 s"
                    Write-XpgLog -Niveau ATTENTION -Message 'Pas de connexion Internet (ou un proxy la bloque).'
                    Write-XpgLog -Niveau INFO -Message 'Choisis [4] : un package hors ligne (create_offline_toolchain.bat sur un PC connecté), ou un installateur que tu as déjà.'
                    Show-XpgPrerequis
                    if ($X.NonInteractif) { Stop-Xpg -Code 10 -Message 'Pas d''Internet.' -Conseil 'Relance avec /choix:4 /dossier:<package hors ligne>.' }
                    continue
                }
                Complete-XpgEtape -Statut OK
                Start-XpgEtape 'Espace disque' | Out-Null
                $besoin = [double](Get-XpgConfig -Section 'Outils' -Cle 'buildtools_espace_go' -Defaut '7') * 1GB
                $lib = (Get-PSDrive -Name 'C' -ErrorAction SilentlyContinue).Free
                if ($lib -and $lib -lt $besoin) { Complete-XpgEtape -Statut ECHEC -Detail ("{0} libres, {1} nécessaires" -f (Format-XpgTaille $lib), (Format-XpgTaille $besoin)); Stop-Xpg -Code 10 -Message 'Pas assez d''espace sur C:.' }
                Complete-XpgEtape -Statut OK -Detail $(if ($lib) { "$(Format-XpgTaille $lib) libres" } else { 'inconnu' })
                Start-XpgEtape 'Téléchargement des Build Tools' | Out-Null
                $dest = Join-Path ([System.IO.Path]::GetTempPath()) 'xpg-outils\vs_BuildTools.exe'
                Save-XpgFichierWeb -Url $url -Destination $dest
                Complete-XpgEtape -Statut OK
                Start-XpgEtape 'Installation des Build Tools (administrateur)' | Out-Null
                $code = Install-XpgBuildTools -Installateur $dest
                switch ($code) {
                    0 { Complete-XpgEtape -Statut OK }
                    3010 { Complete-XpgEtape -Statut OK -Detail 'redémarrage nécessaire'; Request-XpgRedemarrage }
                    1641 { Complete-XpgEtape -Statut OK -Detail 'redémarrage lancé par l''installateur'; Request-XpgRedemarrage }
                    1602 { Complete-XpgEtape -Statut ECHEC -Detail 'annulé'; Stop-Xpg -Code 2 -Message 'Installation annulée dans l''installateur de Microsoft.' }
                    8006 { Complete-XpgEtape -Statut ECHEC -Detail 'Visual Studio est ouvert'; Stop-Xpg -Code 10 -Message 'Un programme de Visual Studio tourne : ferme-le et relance.' }
                    default { Complete-XpgEtape -Statut ECHEC -Detail "code $code"; Stop-Xpg -Code 10 -Message "L'installation des Build Tools a échoué (code $code)." -Conseil 'Les journaux de Microsoft : %TEMP%\dd_*.log ; collect_diagnostics.bat les rassemble.' }
                }
                Invoke-XpgApresInstallation
                Invoke-XpgFin
            }
            2 {
                Write-Host ''
                Write-XpgLog -Niveau TITRE -Message 'Les autres chaînes'
                Write-Host '  MinGW-w64 (GCC) : j''ai fabriqué avec elle l''installateur du lot 8 (compilation et démarrage vérifiés).'
                Write-Host '    Mais : une autre ABI C++, un autre runtime, SDL3 dans sa version MinGW ; le projet Visual Studio'
                Write-Host '    ne sert plus. Cette chaîne n''est pas automatisée ici : ne la prends que si MSVC est impossible.'
                Write-Host '  LLVM/Clang (clang-cl) : garde l''ABI Microsoft, mais demande quand même le SDK et l''éditeur de'
                Write-Host '    liens de Microsoft (les mêmes Build Tools) : aucun gain pour ce projet.'
                $mingw = Find-XpgMinGW
                if ($mingw.Trouve) {
                    Write-XpgLog -Niveau INFO -Message "MinGW-w64 trouvé : $($mingw.Chemin) ($($mingw.Version)) ; essai..."
                    $essai = Test-XpgCompilation -Chaine mingw -Compilateur $mingw.Chemin
                    if ($essai.Ok) { Write-XpgLog -Niveau OK -Message 'L''essai MinGW passe (compiler, lier, lancer, x64) ; le projet, lui, reste à valider (voir docs\RAPPORT-ANALYSE.md).' }
                    else { Write-XpgLog -Niveau ATTENTION -Message "L'essai MinGW échoue : $($essai.Raison)" }
                }
                if ($X.NonInteractif) { Exit-Xpg -Code 2 }
            }
            3 {
                $d = Get-XpgOption -Noms @('dossier')
                if (-not $d) { $d = Read-XpgTexte -Question 'Dossier de l''installation (ex. C:\Program Files\Microsoft Visual Studio\2022\Community)' -OptionLigne '/dossier:<chemin>' }
                if (-not $d -or -not (Test-Path -LiteralPath $d)) { Write-XpgLog -Niveau ATTENTION -Message "Dossier introuvable : $d"; if ($X.NonInteractif) { Exit-Xpg -Code 10 }; continue }
                $msvcM = Find-XpgMsvc $d; $msbM = Find-XpgMsBuild $d
                if (-not $msvcM -or -not $msbM) { Write-XpgLog -Niveau ATTENTION -Message 'Pas de MSVC x64 et de MSBuild dans ce dossier (VC\Tools\MSVC, MSBuild\Current\Bin).'; if ($X.NonInteractif) { Exit-Xpg -Code 10 }; continue }
                Start-XpgEtape 'Essai du compilateur choisi' | Out-Null
                Import-XpgEnvironnementVs -Instance $d
                $essai = Test-XpgCompilation -Chaine msvc -Compilateur $msvcM.Cl
                if (-not $essai.Ok) { Complete-XpgEtape -Statut ECHEC -Detail $essai.Raison; if ($X.NonInteractif) { Exit-Xpg -Code 10 }; continue }
                Complete-XpgEtape -Statut OK -Detail "MSVC $($msvcM.VersionOutils)"
                Set-XpgIniValeur -Chemin $X.ConfigPath -Section 'Compilateur' -Cle 'compilateur_manuel' -Valeur $d
                Write-XpgLog -Niveau OK -Message "Retenu dans config.ini : [Compilateur] compilateur_manuel = $d"
                $X.Config = Read-XpgIni $X.ConfigPath
                Install-XpgInnoSetupSiAbsent
                Invoke-XpgFin
            }
            4 {
                $d = Get-XpgOption -Noms @('dossier')
                if (-not $d) { $d = Get-XpgConfig -Section 'HorsLigne' -Cle 'dossier' -Chemin }
                $inst = Get-XpgOption -Noms @('installateur')
                if (-not $d -and -not $inst) {
                    $d = Read-XpgTexte -Question 'Dossier du package hors ligne (fait par create_offline_toolchain.bat), ou vide pour choisir un vs_BuildTools.exe' -OptionLigne '/dossier:<chemin> ou /installateur:<fichier>'
                    if (-not $d) { $inst = Read-XpgTexte -Question 'Chemin de vs_BuildTools.exe' -OptionLigne '/installateur:<fichier>' }
                }
                $horsLigne = $false
                if ($d) {
                    $sums = Join-Path $d 'SHA256SUMS.txt'
                    $inst = Join-Path $d 'buildtools\vs_BuildTools.exe'
                    if (-not (Test-Path -LiteralPath $inst)) { Write-XpgLog -Niveau ATTENTION -Message "Package incomplet : $inst absent."; if ($X.NonInteractif) { Exit-Xpg -Code 10 }; continue }
                    Start-XpgEtape 'Vérification du package hors ligne' | Out-Null
                    if (Test-Path -LiteralPath $sums) {
                        $mauvais = 0
                        foreach ($l in [System.IO.File]::ReadAllLines($sums)) {
                            if ($l -notmatch '^([0-9a-f]{64})\s+\*?(.+)$') { continue }
                            $f = Join-Path $d $Matches[2]
                            if (-not (Test-Path -LiteralPath $f) -or (Get-XpgSha256 $f) -ne $Matches[1]) { $mauvais++; Write-XpgLog -Niveau ERREUR -Message "Empreinte fausse ou fichier absent : $($Matches[2])" }
                        }
                        if ($mauvais) { Complete-XpgEtape -Statut ECHEC; Stop-Xpg -Code 10 -Message "$mauvais fichier(s) du package ne correspondent pas à SHA256SUMS.txt." }
                    }
                    $innoHl = Get-ChildItem -LiteralPath (Join-Path $d 'innosetup') -Filter 'innosetup-*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
                    if ($innoHl) { $script:innoLocal = $innoHl.FullName }
                    $script:dossierHorsLigne = $d
                    Complete-XpgEtape -Statut OK
                    $horsLigne = $true
                }
                if (-not $inst -or -not (Test-Path -LiteralPath $inst)) { Write-XpgLog -Niveau ATTENTION -Message "Installateur introuvable : $inst"; if ($X.NonInteractif) { Exit-Xpg -Code 10 }; continue }
                Show-XpgDetailsInstallation
                if (-not (Confirm-Xpg -Question 'Installer depuis ce package ?' -Defaut $false)) { continue }
                Start-XpgEtape 'Installation des Build Tools (administrateur)' | Out-Null
                $code = Install-XpgBuildTools -Installateur $inst -HorsLigne:$horsLigne
                if ($code -eq 3010 -or $code -eq 1641) { Complete-XpgEtape -Statut OK -Detail 'redémarrage nécessaire'; Request-XpgRedemarrage }
                if ($code -ne 0) { Complete-XpgEtape -Statut ECHEC -Detail "code $code"; Stop-Xpg -Code 10 -Message "L'installation a échoué (code $code)." -Conseil 'Un package hors ligne peut demander ses certificats : voir docs\DEPANNAGE.md, « Package hors ligne ».' }
                Complete-XpgEtape -Statut OK
                Invoke-XpgApresInstallation
                Invoke-XpgFin
            }
            5 { Show-XpgPrerequis; if ($X.NonInteractif) { Exit-Xpg -Code 0 } }
            6 { Write-XpgLog -Niveau INFO -Message 'Annulé : rien n''a été installé.'; Exit-Xpg -Code 2 }
        }
    }
} catch {
    Stop-Xpg -Code 10 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

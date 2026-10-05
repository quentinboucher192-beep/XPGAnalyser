#Requires -Version 5.1
# =============================================================================
#  build_installer.ps1 - lance par build_installer.bat (et par package.bat) :
#  l'installateur Windows, avec Inno Setup (ISCC.exe), depuis dist\staging.
#  Toutes les valeurs viennent de config.ini (nom, editeur, version, AppId,
#  dossier des donnees par defaut, anciens dossiers a reprendre) : elles sont
#  passees a installateur\XPGAnalyser.iss par /D.
#  Sortie : XPGAnalyser-Setup-<version>.exe a la RACINE du dossier (le seul fichier a
#  lancer : il installe tout), et son empreinte dans dist\SHA256SUMS.txt. Un installateur
#  d'une autre version, a la racine, part dans dist\anciens\.
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgOutils.psm1') -Force -Global -DisableNameChecking

$usage = @'
build_installer.bat [options]
  /staging:<dossier>  le dossier à livrer (par défaut dist\staging, fait par package.bat)
  /iscc:<ISCC.exe>    un Inno Setup précis (sinon : détecté)
'@
$X = Initialize-Xpg -Nom 'build_installer' -Titre 'XPGAnalyser - fabrication de l''installateur (Inno Setup)' -Arguments $Arguments -Usage $usage

try {
    $racine = $X.Racine
    $dist = Join-Path $racine (Get-XpgConfig -Section 'Installateur' -Cle 'sortie' -Defaut 'dist')
    $st = Get-XpgOption -Noms @('staging') -Defaut (Join-Path $dist 'staging')

    Start-XpgEtape 'Le dossier à livrer' | Out-Null
    $manif = Join-Path $st 'manifeste.json'
    if (-not (Test-Path -LiteralPath $manif)) { Stop-Xpg -Code 1 -Message "Pas de manifeste dans $st." -Conseil 'Lance package.bat (il compile, teste et prépare ce dossier).' }
    $m = [System.IO.File]::ReadAllText($manif) | ConvertFrom-Json
    $version = Get-XpgConfig -Section 'Produit' -Cle 'version' -Obligatoire
    if ($m.version -ne $version) { Stop-Xpg -Code 2 -Message "Le dossier à livrer est en $($m.version), config.ini en $version." -Conseil 'Relance package.bat.' }
    $absents = @($m.fichiers | Where-Object { -not (Test-Path -LiteralPath (Join-Path $st $_.chemin)) })
    if ($absents.Count) { Stop-Xpg -Code 1 -Message "$($absents.Count) fichier(s) du manifeste absents de $st." }
    Complete-XpgEtape -Statut OK -Detail ("{0} fichiers, version {1}, chaîne {2}" -f @($m.fichiers).Count, $m.version, $m.chaine)

    Start-XpgEtape 'Inno Setup' | Out-Null
    $iscc = Get-XpgOption -Noms @('iscc')
    if (-not $iscc) {
        $inno = Find-XpgInnoSetup
        if (-not $inno.Trouve) {
            # Absent : on propose de l'installer (pour toi seul, sans droits), puis on continue.
            Write-XpgLog -Niveau ATTENTION -Message 'Inno Setup (ISCC.exe) est introuvable.'
            $code = Start-XpgScriptEnfant -Script 'install_build_tools.ps1' -Arguments @('/inno-seulement', '/depuis:build_installer') -Origine 'build_installer'
            if ($code -ne 0) { Stop-Xpg -Code 11 -Message "Inno Setup n'est pas installé (code $code)." -Conseil 'install_build_tools.bat l''installe (pour toi seul, sans droits d''administrateur).' }
            $inno = Find-XpgInnoSetup
        }
        if ($inno.Trouve) { $iscc = $inno.Chemin }
    }
    if (-not $iscc -or -not (Test-Path -LiteralPath $iscc)) { Stop-Xpg -Code 11 -Message 'Inno Setup (ISCC.exe) est introuvable.' -Conseil 'install_build_tools.bat l''installe (pour toi seul, sans droits d''administrateur).' }
    Complete-XpgEtape -Statut OK -Detail $iscc

    Start-XpgEtape 'Compilation du script d''installation' | Out-Null
    $iss = Join-Path $racine 'installateur\XPGAnalyser.iss'
    $guid = (Get-XpgConfig -Section 'Produit' -Cle 'app_id' -Obligatoire).Trim('{', '}')
    $defs = [ordered]@{
        Nom = Get-XpgConfig -Section 'Produit' -Cle 'nom' -Obligatoire
        Editeur = Get-XpgConfig -Section 'Produit' -Cle 'editeur' -Obligatoire
        Version = $version
        AppGuid = $guid
        Exe = Get-XpgConfig -Section 'Produit' -Cle 'exe' -Defaut 'XpgAnalyzer.exe'
        Description = Get-XpgConfig -Section 'Produit' -Cle 'description' -Defaut ''
        UrlSupport = Get-XpgConfig -Section 'Produit' -Cle 'url_support' -Defaut ''
        Staging = (Get-Item -LiteralPath $st).FullName
        Sortie = $racine
        DonneesDefaut = Get-XpgConfig -Section 'Installateur' -Cle 'dossier_donnees_par_defaut' -Defaut '{Documents}\XPGAnalyser'
        AnciensDossiers = Get-XpgConfig -Section 'Installateur' -Cle 'anciens_dossiers' -Defaut ''
        Chaine = [string]$m.chaine
        Racine = $racine
    }
    # Un installateur d'une autre version, a la racine : range dans dist\anciens\ (un seul .exe a la racine).
    $nomSetup = "{0}-Setup-{1}.exe" -f $defs.Nom, $version
    foreach ($ancien in @(Get-ChildItem -LiteralPath $racine -Filter ("{0}-Setup-*.exe" -f $defs.Nom) -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne $nomSetup })) {
        $rangement = Join-Path $dist 'anciens'
        New-Item -ItemType Directory -Path $rangement -Force | Out-Null
        Move-Item -LiteralPath $ancien.FullName -Destination (Join-Path $rangement $ancien.Name) -Force
        Write-XpgLog -Niveau INFO -Message "Ancien installateur rangé : dist\anciens\$($ancien.Name)"
    }
    $a = @('/Q')
    foreach ($k in $defs.Keys) { $a += ("/D{0}={1}" -f $k, $defs[$k]) }
    $a += $iss
    $code = Invoke-XpgProcessus -Fichier $iscc -Arguments $a -Afficher -Libelle 'ISCC'
    $setup = Join-Path $racine $nomSetup
    if ($code -ne 0 -or -not (Test-Path -LiteralPath $setup)) { Stop-Xpg -Code 1 -Message "Inno Setup a échoué (code $code)." -Conseil "Les erreurs du script sont dans le journal : $($X.Journal)" }
    Complete-XpgEtape -Statut OK -Detail ("{0} ({1})" -f $setup, (Format-XpgTaille (Get-Item -LiteralPath $setup).Length))

    Start-XpgEtape 'Empreinte (SHA256SUMS.txt)' | Out-Null
    $h = Get-XpgSha256 $setup
    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    Write-XpgTexte -Chemin (Join-Path $dist 'SHA256SUMS.txt') -Texte ("{0}  {1}`r`n" -f $h, [System.IO.Path]::GetFileName($setup)) -SansBom
    Complete-XpgEtape -Statut OK -Detail $h
    if (Get-XpgConfig -Section 'Installateur' -Cle 'certificat') {
        Write-XpgLog -Niveau ATTENTION -Message 'config.ini donne un certificat, mais la signature n''est pas automatisée dans cette version : l''installateur N''EST PAS signé (voir docs\CONFIG-INI.md).'
    } else {
        Write-XpgLog -Niveau INFO -Message 'Pas de certificat de signature (config.ini) : Windows SmartScreen avertira au premier lancement ; voir docs\GUIDE-INSTALLATION.md.'
    }
    Exit-Xpg -Code 0
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

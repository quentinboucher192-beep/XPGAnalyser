#Requires -Version 5.1
# =============================================================================
#  create_offline_toolchain.ps1 - lance par create_offline_toolchain.bat
#  Sur un PC CONNECTE : prepare un package hors ligne pour un PC sans Internet.
#
#    <dossier>\buildtools\   une "disposition" des Build Tools (vs_BuildTools.exe
#                            --layout : les composants de config.ini, fr-FR et en-US)
#    <dossier>\innosetup\    l'installateur d'Inno Setup
#    <dossier>\SHA256SUMS.txt, composants.txt, LISEZ-MOI.txt
#
#  Puis, sur le PC hors ligne : install_build_tools.bat /choix:4 /dossier:<dossier>
#  (les fichiers sont reverifies : signatures et empreintes).
#  Taille : 2 a 4 Go selon les composants.
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgOutils.psm1') -Force -Global -DisableNameChecking

$usage = @'
create_offline_toolchain.bat [/dossier:<chemin>] [/langues:fr-FR,en-US]
  Prépare, sur un PC connecté, le package hors ligne des outils de compilation.
'@
$X = Initialize-Xpg -Nom 'create_offline_toolchain' -Titre 'XPGAnalyser - package hors ligne des outils de compilation' -Arguments $Arguments -Usage $usage

try {
    $dossier = Get-XpgOption -Noms @('dossier')
    if (-not $dossier) { $dossier = Get-XpgConfig -Section 'HorsLigne' -Cle 'dossier' -Chemin }
    if (-not $dossier) { $dossier = Read-XpgTexte -Question 'Dossier du package hors ligne (2 à 4 Go ; une clé USB, un partage)' -Defaut (Join-Path $X.Racine 'outils-hors-ligne') -OptionLigne '/dossier:<chemin>' }
    $dossier = [System.IO.Path]::GetFullPath($dossier)
    $composants = @((Get-XpgConfig -Section 'Outils' -Cle 'buildtools_composants' -Obligatoire) -split ';' | Where-Object { $_ })
    $langues = @((Get-XpgOption -Noms @('langues') -Defaut 'fr-FR,en-US') -split ',' | Where-Object { $_ })

    Start-XpgEtape 'Connexion et espace disque' | Out-Null
    $urlBt = Get-XpgConfig -Section 'Outils' -Cle 'buildtools_url' -Obligatoire
    $urlInno = Get-XpgConfig -Section 'Outils' -Cle 'innosetup_url' -Obligatoire
    if (-not (Test-XpgInternet $urlBt)) { Stop-Xpg -Code 10 -Message 'Pas de connexion Internet : ce script se lance sur un PC connecté.' }
    New-Item -ItemType Directory -Path $dossier -Force | Out-Null
    $lecteur = [System.IO.Path]::GetPathRoot($dossier)
    try { $libre = (New-Object System.IO.DriveInfo $lecteur).AvailableFreeSpace } catch { $libre = 0 }
    if ($libre -and $libre -lt 5GB) { Stop-Xpg -Code 10 -Message ("Pas assez de place sur {0} : {1} libres, environ 5 Go nécessaires." -f $lecteur, (Format-XpgTaille $libre)) }
    Complete-XpgEtape -Statut OK -Detail ("{0} ; {1} libres" -f $dossier, (Format-XpgTaille $libre))

    Start-XpgEtape 'L''installateur des Build Tools' | Out-Null
    $bt = Join-Path $dossier 'buildtools\vs_BuildTools.exe'
    Save-XpgFichierWeb -Url $urlBt -Destination $bt
    $prov = Test-XpgProvenance -Chemin $bt -Editeur (Get-XpgConfig -Section 'Outils' -Cle 'buildtools_editeur' -Defaut 'Microsoft Corporation') -Sha256 (Get-XpgConfig -Section 'Outils' -Cle 'buildtools_sha256')
    if (-not $prov.Ok) { Stop-Xpg -Code 10 -Message "Installateur refusé : $($prov.Raison)." }
    Complete-XpgEtape -Statut OK -Detail $prov.Signataire

    Start-XpgEtape 'La disposition hors ligne (--layout ; 20 à 60 min)' | Out-Null
    $a = @('--layout', "`"$(Join-Path $dossier 'buildtools')`"")
    foreach ($c in $composants) { $a += @('--add', $c) }
    $a += @('--lang') + $langues
    $a += @('--passive', '--wait')
    Write-XpgLog -Niveau INFO -Message ("{0} {1}" -f $bt, ($a -join ' '))
    $p = Start-Process -FilePath $bt -ArgumentList $a -Wait -PassThru
    if ($p.ExitCode -ne 0) { Stop-Xpg -Code 10 -Message "La disposition a échoué (code $($p.ExitCode))." -Conseil 'Journaux de Microsoft : %TEMP%\dd_*.log.' }
    Complete-XpgEtape -Statut OK

    Start-XpgEtape 'Inno Setup' | Out-Null
    $inno = Join-Path $dossier ('innosetup\' + [System.IO.Path]::GetFileName(([Uri]$urlInno).AbsolutePath))
    Save-XpgFichierWeb -Url $urlInno -Destination $inno
    $prov = Test-XpgProvenance -Chemin $inno -Editeur (Get-XpgConfig -Section 'Outils' -Cle 'innosetup_editeur') -Sha256 (Get-XpgConfig -Section 'Outils' -Cle 'innosetup_sha256')
    if (-not $prov.Ok) { Stop-Xpg -Code 10 -Message "Installateur d'Inno Setup refusé : $($prov.Raison)." }
    Complete-XpgEtape -Statut OK

    Start-XpgEtape 'Empreintes et mode d''emploi' | Out-Null
    $lignes = @()
    foreach ($f in @($bt, $inno, (Join-Path $dossier 'buildtools\Catalog.json'))) {
        if (Test-Path -LiteralPath $f) { $lignes += ("{0}  {1}" -f (Get-XpgSha256 $f), $f.Substring($dossier.Length + 1)) }
    }
    Write-XpgTexte -Chemin (Join-Path $dossier 'SHA256SUMS.txt') -Texte (($lignes -join "`r`n") + "`r`n") -SansBom
    Write-XpgTexte -Chemin (Join-Path $dossier 'composants.txt') -Texte (($composants -join "`r`n") + "`r`n")
    $taille = (Get-ChildItem -LiteralPath $dossier -Recurse -File | Measure-Object -Property Length -Sum).Sum
    $lisez = @"
Package hors ligne des outils de compilation de XPGAnalyser
Préparé le $((Get-Date).ToString('dd/MM/yyyy HH:mm')) sur $env:COMPUTERNAME ; $(Format-XpgTaille $taille).

Sur le PC sans Internet :
  1. Copie ce dossier (clé USB, partage).
  2. Lance outils\install_build_tools.bat, choix 4 « Utiliser un package hors ligne »,
     et indique ce dossier ; ou, sans question :
       install_build_tools.bat /choix:4 /dossier:"<ce dossier>" /oui
  3. Les fichiers sont revérifiés (SHA256SUMS.txt, signatures) avant d'être lancés.

Composants : voir composants.txt. Les Build Tools demandent les droits
d'administrateur pour s'installer ; Inno Setup s'installe pour toi seul.
Si l'installateur de Microsoft refuse ses paquets hors ligne, installe d'abord
les certificats du dossier buildtools\certificates (voir docs\DEPANNAGE.md).
"@
    Write-XpgTexte -Chemin (Join-Path $dossier 'LISEZ-MOI.txt') -Texte ($lisez -replace "`r?`n", "`r`n")
    Complete-XpgEtape -Statut OK -Detail (Format-XpgTaille $taille)
    if (Confirm-Xpg -Question "Retenir ce dossier dans config.ini ([HorsLigne] dossier) ?" -Defaut $true) {
        Set-XpgIniValeur -Chemin $X.ConfigPath -Section 'HorsLigne' -Cle 'dossier' -Valeur $dossier
        Write-XpgLog -Niveau OK -Message "config.ini : [HorsLigne] dossier = $dossier"
    }
    Exit-Xpg -Code 0
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

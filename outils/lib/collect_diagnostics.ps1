#Requires -Version 5.1
# =============================================================================
#  collect_diagnostics.ps1 - lance par collect_diagnostics.bat : un zip pour le
#  depannage, sur le Bureau. Dedans : les journaux (installation, reparations,
#  compilation), l'etat de l'installation, installation.ini, manifeste.json,
#  XPGAnalyser.ini, la verification du moment, la description du PC ; et, si
#  tu l'acceptes, settings.txt (il contient les chemins de tes projets).
#  JAMAIS tes projets ni ta bibliotheque. Rien n'est envoye nulle part : c'est
#  toi qui transmets le zip.
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgInstallation.psm1') -Force -Global -DisableNameChecking

$usage = @'
collect_diagnostics.bat [/sortie:<dossier>] [/avec-reglages] [/sans-reglages]
'@
$X = Initialize-Xpg -Nom 'collect_diagnostics' -Titre 'XPGAnalyser - collecte des journaux de diagnostic' -Arguments $Arguments -Genre maintenance -Usage $usage
try {
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('xpg-diagnostic-' + (Get-Date).ToString('yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    Start-XpgEtape 'Le PC' | Out-Null
    $os = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
    $info = [ordered]@{ date = (Get-Date).ToString('s'); ordinateur = $env:COMPUTERNAME; windows = $(if ($os) { "$($os.Caption) $($os.Version) ($($os.OSArchitecture))" } else { [Environment]::OSVersion.VersionString })
        powershell = $PSVersionTable.PSVersion.ToString(); administrateur = (Test-XpgAdmin); langue = (Get-Culture).Name; page_de_code = [Console]::OutputEncoding.WebName
        disques = @(Get-PSDrive -PSProvider FileSystem | ForEach-Object { "{0}: {1} libres" -f $_.Name, (Format-XpgTaille $_.Free) }) }
    Write-XpgTexte -Chemin (Join-Path $tmp 'pc.json') -Texte ($info | ConvertTo-Json -Depth 3)
    Complete-XpgEtape -Statut OK -Detail $info.windows

    Start-XpgEtape 'L''installation' | Out-Null
    $i = Get-XpgInstallation
    if ($i) {
        foreach ($f in @('installation.ini', 'manifeste.json')) { $p = Join-Path $i.Dossier $f; if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination $tmp } }
        $e = Get-XpgFichierEtat $i; if (Test-Path -LiteralPath $e) { Copy-Item -LiteralPath $e -Destination $tmp }
        if (Test-Path -LiteralPath $i.IniUtilisateur) { Copy-Item -LiteralPath $i.IniUtilisateur -Destination $tmp }
        $d = Invoke-XpgDiagnostic -Installation $i -Rapide
        Write-XpgTexte -Chemin (Join-Path $tmp 'verification.txt') -Texte (($d.Lignes -join "`r`n") + "`r`n")
        if (Test-Path -LiteralPath $i.Journaux) {
            New-Item -ItemType Directory -Path (Join-Path $tmp 'journaux') -Force | Out-Null
            Get-ChildItem -LiteralPath $i.Journaux -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 60 | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $tmp 'journaux') }
        }
        Complete-XpgEtape -Statut OK -Detail ("{0} {1}" -f $i.Nom, $i.Version)
    } else { Complete-XpgEtape -Statut ATTENTION -Detail 'aucune installation trouvée' }

    Start-XpgEtape 'Les autres journaux' | Out-Null
    $n = 0
    $j = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'XpgAnalyzer\Journaux'
    if ((Test-Path -LiteralPath $j) -and (-not $i -or $j -ne $i.Journaux)) { New-Item -ItemType Directory -Path (Join-Path $tmp 'journaux') -Force | Out-Null; Get-ChildItem -LiteralPath $j -File | Sort-Object LastWriteTime -Descending | Select-Object -First 60 | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $tmp 'journaux') -Force; $n++ } }
    # La compilation (si ce script est lance depuis outils\ du projet).
    $jc = Join-Path $X.Outils 'journaux'
    if (Test-Path -LiteralPath $jc) { New-Item -ItemType Directory -Path (Join-Path $tmp 'compilation') -Force | Out-Null; Get-ChildItem -LiteralPath $jc -File | Sort-Object LastWriteTime -Descending | Select-Object -First 40 | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $tmp 'compilation'); $n++ } }
    # Les journaux d'Inno Setup et de l'installateur de Visual Studio (dans %TEMP%).
    foreach ($motif in @('Setup Log *.txt', 'dd_*.log')) { Get-ChildItem -LiteralPath ([System.IO.Path]::GetTempPath()) -Filter $motif -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 10 | ForEach-Object { New-Item -ItemType Directory -Path (Join-Path $tmp 'temp') -Force | Out-Null; Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $tmp 'temp'); $n++ } }
    Complete-XpgEtape -Statut OK -Detail "$n fichier(s)"

    $avec = Test-XpgOption @('avec-reglages')
    if (-not $avec -and -not (Test-XpgOption @('sans-reglages'))) { $avec = Confirm-Xpg -Question 'Joindre settings.txt ? (il contient les chemins de tes projets récents)' -Defaut $false }
    $st = Join-Path (Get-XpgDossierReglages) 'settings.txt'
    if ($avec -and (Test-Path -LiteralPath $st)) { Copy-Item -LiteralPath $st -Destination $tmp }

    Start-XpgEtape 'Le zip' | Out-Null
    $sortie = Get-XpgOption -Noms @('sortie') -Defaut ([Environment]::GetFolderPath('Desktop'))
    $zip = Join-Path $sortie ("XPGAnalyser-diagnostic-{0}.zip" -f (Get-Date).ToString('yyyy-MM-dd_HH-mm'))
    if ($X.Journal) { Copy-Item -LiteralPath $X.Journal -Destination $tmp -ErrorAction SilentlyContinue }
    Compress-Archive -Path (Join-Path $tmp '*') -DestinationPath $zip -Force
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    Complete-XpgEtape -Statut OK -Detail ("{0} ({1})" -f $zip, (Format-XpgTaille (Get-Item -LiteralPath $zip).Length))
    Write-XpgLog -Niveau INFO -Message 'Rien n''a été envoyé : transmets ce zip toi-même. Il ne contient ni tes projets ni ta bibliothèque.'
    Exit-Xpg -Code 0
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

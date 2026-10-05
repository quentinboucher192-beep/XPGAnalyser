#Requires -Version 5.1
# =============================================================================
#  verify_installation.ps1 - lance par verify_installation.bat : la
#  verification d'integrite, sans rien modifier. Chaque fichier du manifeste
#  (present, taille, SHA-256), les DLL, le runtime, le registre, les
#  raccourcis, les droits, la configuration, les chemins, une installation
#  interrompue. Code : 0 sain ; 1 un probleme ; 20 pas d'installation.
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgInstallation.psm1') -Force -Global -DisableNameChecking

$usage = @'
verify_installation.bat [/rapide] [/dossier:<dossier du programme>] [/json:<fichier>]
  Vérifie l'installation sans rien modifier (repair.bat répare).
'@
$X = Initialize-Xpg -Nom 'verify_installation' -Titre 'XPGAnalyser - vérification de l''installation' -Arguments $Arguments -Genre maintenance -Usage $usage
try {
    Start-XpgEtape 'Installation' | Out-Null
    $i = Get-XpgInstallation -Dossier (Get-XpgOption -Noms @('dossier'))
    if (-not $i) { Stop-Xpg -Code 20 -Message 'Aucune installation de XPGAnalyser trouvée.' }
    Complete-XpgEtape -Statut OK -Detail ("{0} {1} - {2}" -f $i.Nom, $i.Version, $i.Dossier)
    Start-XpgEtape 'Vérification (rien n''est modifié)' | Out-Null
    $d = Invoke-XpgDiagnostic -Installation $i -Rapide:(Test-XpgOption @('rapide'))
    $graves = @($d.Problemes | Where-Object { $_.Gravite -ne 'info' })
    Complete-XpgEtape -Statut $(if ($graves.Count) { 'ECHEC' } else { 'OK' }) -Detail ("{0} problème(s)" -f $graves.Count)
    $json = Get-XpgOption -Noms @('json')
    if ($json) { Write-XpgTexte -Chemin $json -Texte ([pscustomobject]@{ installation = $i.Dossier; version = $i.Version; problemes = $d.Problemes; lignes = $d.Lignes } | ConvertTo-Json -Depth 5) }
    if ($graves.Count) { Write-XpgLog -Niveau INFO -Message 'Pour réparer : repair.bat (menu Démarrer > XPGAnalyser > Maintenance > Réparer).' }
    Exit-Xpg -Code $(if ($graves.Count) { 1 } else { 0 })
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

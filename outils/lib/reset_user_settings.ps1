#Requires -Version 5.1
# =============================================================================
#  reset_user_settings.ps1 - lance par reset_user_settings.bat : remettre les
#  reglages de XPGAnalyser a zero, VOLONTAIREMENT, pour ce compte Windows.
#  Rien n'est supprime : les fichiers sont DEPLACES dans une sauvegarde
#  (restore_backup.bat peut les remettre). Tes projets, ta bibliotheque et tes
#  captures ne sont pas concernes.
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgInstallation.psm1') -Force -Global -DisableNameChecking

$usage = @'
reset_user_settings.bat [/niveau:affichage|dossiers|tout] [/oui]
  affichage : settings.txt (fenêtres, panneaux, thème, projets récents, explorateur)
  dossiers  : + XPGAnalyser.ini (les dossiers reviennent à ceux de l'installation)
  tout      : + thèmes, modèles de vues, reprise après un arrêt
'@
$X = Initialize-Xpg -Nom 'reset_user_settings' -Titre 'XPGAnalyser - réinitialisation des réglages' -Arguments $Arguments -Genre maintenance -Usage $usage
try {
    $i = Get-XpgInstallation
    $reglages = Get-XpgDossierReglages
    Start-XpgEtape 'Tes réglages' | Out-Null
    if (-not (Test-Path -LiteralPath $reglages)) { Complete-XpgEtape -Statut OK -Detail 'aucun (déjà à zéro)'; Exit-Xpg -Code 0 }
    Complete-XpgEtape -Statut OK -Detail $reglages
    $niveaux = [ordered]@{ affichage = @('settings.txt'); dossiers = @('settings.txt', 'XPGAnalyser.ini'); tout = @('settings.txt', 'XPGAnalyser.ini', 'themes', 'modeles-vues', 'reprise') }
    $niveau = (Get-XpgOption -Noms @('niveau')).ToLowerInvariant()
    if (-not $niveau) {
        $n = Read-XpgChoix -Question 'Que remettre à zéro ?' -Options @('L''affichage (settings.txt : fenêtres, panneaux, thème, projets récents)',
            'L''affichage et les dossiers (XPGAnalyser.ini)', 'Tout (aussi les thèmes, les modèles de vues, la reprise après un arrêt)', 'Annuler') -Defaut 1 -OptionLigne '/niveau:affichage|dossiers|tout'
        if ($n -eq 4) { Exit-Xpg -Code 2 }
        $niveau = @($niveaux.Keys)[$n - 1]
    }
    if (-not $niveaux.Contains($niveau)) { Stop-Xpg -Code 2 -Message "Niveau inconnu : $niveau" }
    Write-XpgLog -Niveau ATTENTION -Message ("Seront mis de côté : {0}. Tes projets et ta bibliothèque ne sont pas touchés." -f ($niveaux[$niveau] -join ', '))
    if (-not (Confirm-Xpg -Question 'Réinitialiser ?' -Defaut $false)) { Write-XpgLog -Niveau INFO -Message 'Rien n''a changé.'; Exit-Xpg -Code 2 }
    if ($i) {
        Start-XpgEtape 'Fermer l''application' | Out-Null
        if (-not (Stop-XpgApplication $i)) { Stop-Xpg -Code 6 -Message "$($i.Nom) est ouvert : il réécrirait ses réglages en se fermant. Ferme-le d'abord." }
        Complete-XpgEtape -Statut OK
    }
    Start-XpgEtape 'Mise de côté (sauvegarde)' | Out-Null
    $base = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'XpgAnalyzer\Sauvegardes'
    if ($i) { $base = $i.Sauvegardes }
    $d = Join-Path $base ("{0}_reinitialisation-{1}\reglages" -f (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss'), $niveau)
    New-Item -ItemType Directory -Path $d -Force | Out-Null
    $n = 0
    foreach ($e in $niveaux[$niveau]) {
        $p = Join-Path $reglages $e
        if (Test-Path -LiteralPath $p) { Move-Item -LiteralPath $p -Destination (Join-Path $d $e); $n++ }
    }
    Write-XpgTexte -Chemin (Join-Path (Split-Path -Parent $d) 'sauvegarde.json') -Texte (([pscustomobject]@{ date = (Get-Date).ToString('s'); raison = "reinitialisation-$niveau"; version = $(if ($i) { $i.Version } else { '' }); contenu = @("réglages ($n élément(s))") }) | ConvertTo-Json)
    Complete-XpgEtape -Statut OK -Detail ("{0} élément(s) mis de côté dans {1}" -f $n, $d)
    Write-XpgLog -Niveau INFO -Message 'Au prochain lancement, XPGAnalyser repart des réglages par défaut. Pour annuler : restore_backup.bat, parties « reglages ».'
    Exit-Xpg -Code 0
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

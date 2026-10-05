#Requires -Version 5.1
# =============================================================================
#  restore_backup.ps1 - lance par restore_backup.bat : remettre une sauvegarde
#  (faite avant chaque mise a jour, reparation ou restauration).
#  Une sauvegarde contient le programme, les reglages, les cles de registre ;
#  JAMAIS les projets (ils ne sont touches par aucune operation). Avant de
#  restaurer : l'application fermee, et une sauvegarde de l'etat actuel (on
#  peut donc toujours revenir en arriere).
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgInstallation.psm1') -Force -Global -DisableNameChecking

$usage = @'
restore_backup.bat [options]
  /precedente          la dernière sauvegarde faite avant une mise à jour (« avant-<version> »)
  /sauvegarde:<nom>    une sauvegarde précise (son nom de dossier)
  /parties:programme,reglages,registre   ce qu'il faut remettre (par défaut : tout)
  /liste               montre les sauvegardes, sans rien remettre
'@
$X = Initialize-Xpg -Nom 'restore_backup' -Titre 'XPGAnalyser - restauration d''une sauvegarde' -Arguments $Arguments -Genre maintenance -Usage $usage
try {
    Start-XpgEtape 'Installation et sauvegardes' | Out-Null
    $i = Get-XpgInstallation
    if (-not $i) { Stop-Xpg -Code 20 -Message 'Aucune installation de XPGAnalyser trouvée.' }
    $liste = @(Get-XpgSauvegardes $i)
    if (-not $liste.Count) { Complete-XpgEtape -Statut ATTENTION -Detail "aucune dans $($i.Sauvegardes)"; Exit-Xpg -Code 21 }
    Complete-XpgEtape -Statut OK -Detail ("{0} sauvegarde(s) dans {1}" -f $liste.Count, $i.Sauvegardes)
    foreach ($s in $liste) { Write-XpgLog -Niveau INFO -Message ("  {0}  {1,-22} version {2}  {3}" -f $s.Nom.Substring(0, 19), $s.Raison, $s.Version, (Format-XpgTaille $s.Taille)) }
    if (Test-XpgOption @('liste')) { Exit-Xpg -Code 0 }

    $choisie = $null
    $nom = Get-XpgOption -Noms @('sauvegarde')
    if ($nom) { $choisie = $liste | Where-Object { $_.Nom -eq $nom } | Select-Object -First 1 }
    elseif (Test-XpgOption @('precedente')) { $choisie = $liste | Where-Object { $_.Raison -like 'avant-*' -and $_.Raison -notlike 'avant-reparation*' -and $_.Raison -notlike 'avant-restauration*' } | Select-Object -First 1 }
    else {
        $n = Read-XpgChoix -Question 'Quelle sauvegarde remettre ?' -Options (@($liste | ForEach-Object { "{0} - {1} (version {2})" -f $_.Nom.Substring(0, 19), $_.Raison, $_.Version }) + 'Annuler') -OptionLigne '/sauvegarde:<nom> ou /precedente'
        if ($n -gt $liste.Count) { Exit-Xpg -Code 2 }
        $choisie = $liste[$n - 1]
    }
    if (-not $choisie) { Stop-Xpg -Code 21 -Message 'Sauvegarde introuvable.' }
    $parties = @((Get-XpgOption -Noms @('parties') -Defaut 'programme,reglages,registre') -split ',' | ForEach-Object { $_.Trim().ToLowerInvariant() })
    Write-XpgLog -Niveau INFO -Message ("Remettre {0} ({1}) : {2}. Tes projets ne sont pas concernés." -f $choisie.Nom, $choisie.Raison, ($parties -join ', '))
    if (-not (Confirm-Xpg -Question 'Continuer ?' -Defaut $true)) { Exit-Xpg -Code 2 }
    if ($i.Portee -eq 'tous' -and -not (Test-XpgAdmin) -and ($parties -contains 'programme' -or $parties -contains 'registre')) {
        Stop-Xpg -Code 5 -Message 'Installation pour tous les comptes : il faut les droits d''administrateur.' -Conseil 'Clic droit sur restore_backup.bat > Exécuter en tant qu''administrateur.'
    }

    Start-XpgEtape 'Fermer l''application' | Out-Null
    if (-not (Stop-XpgApplication $i)) { Stop-Xpg -Code 6 -Message "$($i.Nom) est encore ouvert : rien n'a été modifié." }
    Complete-XpgEtape -Statut OK
    Start-XpgEtape 'Sauvegarde de l''état actuel' | Out-Null
    $s = New-XpgSauvegarde -Installation $i -Raison 'avant-restauration'
    Complete-XpgEtape -Statut OK -Detail $s
    Start-XpgEtape "Restauration de $($choisie.Nom)" | Out-Null
    Restore-XpgSauvegarde -Installation $i -Chemin $choisie.Chemin -Programme:($parties -contains 'programme') -Reglages:($parties -contains 'reglages') -Registre:($parties -contains 'registre')
    Complete-XpgEtape -Statut OK
    Start-XpgEtape 'Vérification' | Out-Null
    $i = Read-XpgInstallation -Dossier $i.Dossier
    $d = Invoke-XpgDiagnostic -Installation $i -Rapide
    $graves = @($d.Problemes | Where-Object { $_.Gravite -eq 'erreur' })
    Complete-XpgEtape -Statut $(if ($graves.Count) { 'ATTENTION' } else { 'OK' }) -Detail ("version {0}, {1} problème(s)" -f $i.Version, $graves.Count)
    Exit-Xpg -Code 0
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

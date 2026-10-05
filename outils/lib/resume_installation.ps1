#Requires -Version 5.1
# =============================================================================
#  resume_installation.ps1 - lance par resume_installation.bat : reprendre une
#  installation interrompue (courant coupe, plantage, fenetre fermee).
#  L'installateur note chaque etape dans <Etat>\Etat\installation.json. Ici :
#    - la copie des fichiers n'est pas finie -> relancer l'installateur (celui
#      de la reserve, ou /installateur:<Setup.exe>) ;
#    - les donnees, la reprise, la verification -> refaites (elles sont
#      idempotentes : rien n'est copie deux fois, rien n'est ecrase) ;
#    - ou revenir a la version d'avant (sa sauvegarde).
#  /migrer:<dossier> : reprendre plus tard un ancien dossier oublie.
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgInstallation.psm1') -Force -Global -DisableNameChecking

$usage = @'
resume_installation.bat [options]
  /reprendre            refait les étapes non terminées, sans question
  /precedente           revient à la version d'avant (sa sauvegarde)
  /installateur:<f>     l'installateur à relancer si la copie n'est pas finie
  /migrer:<dossier>     reprend les données d'un ancien dossier (projets, libs, resources, captures)
'@
$X = Initialize-Xpg -Nom 'resume_installation' -Titre 'XPGAnalyser - reprise d''une installation' -Arguments $Arguments -Genre maintenance -Usage $usage

function Invoke-XpgEtapeInstallation {
    param($Installation, [string[]] $Args2)
    $script = Join-Path $PSScriptRoot 'installer_etape.ps1'
    $a = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script) + $Args2 + @("/dossier:$($Installation.Dossier)")
    if (Test-XpgOption @('details')) { $a += '/details' }
    $p = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList ($a | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -NoNewWindow -Wait -PassThru
    return $p.ExitCode
}

try {
    Start-XpgEtape 'Installation et état' | Out-Null
    $i = Get-XpgInstallation
    if (-not $i) { Stop-Xpg -Code 20 -Message 'Aucune installation de XPGAnalyser trouvée.' -Conseil 'Si la toute première installation a été coupée, relance XPGAnalyser-Setup-<version>.exe : il reprend de zéro.' }
    $etat = Read-XpgEtat (Get-XpgFichierEtat $i)
    $migrer = Get-XpgOption -Noms @('migrer')
    if ($etat) { Complete-XpgEtape -Statut OK -Detail ("version {0}, étape « {1} », statut {2} ({3})" -f $etat.version, $etat.etape, $etat.statut, $etat.maj) }
    else { Complete-XpgEtape -Statut OK -Detail 'aucun état enregistré' }

    if ($migrer) {
        Start-XpgEtape "Reprise des données de $migrer" | Out-Null
        if (-not (Stop-XpgApplication $i)) { Stop-Xpg -Code 6 -Message "$($i.Nom) est ouvert : ferme-le d'abord." }
        $c = Invoke-XpgEtapeInstallation -Installation $i -Args2 @('/etape:apres-copie', "/sources:$migrer")
        Complete-XpgEtape -Statut $(if ($c -eq 0) { 'OK' } else { 'ECHEC' }) -Detail "code $c (rapport : dans $($i.Journaux))"
        Exit-Xpg -Code $c
    }
    if (-not $etat -or $etat.statut -eq 'terminee') { Write-XpgLog -Niveau OK -Message 'Aucune installation interrompue : rien à reprendre.'; Exit-Xpg -Code 0 }

    $choix = 1
    if (Test-XpgOption @('precedente')) { $choix = 2 }
    elseif (-not (Test-XpgOption @('reprendre'))) {
        $choix = Read-XpgChoix -Question ("L'installation de la version {0} s'est arrêtée à l'étape « {1} ». Que faire ?" -f $etat.version, $etat.etape) `
                   -Options @('La reprendre à cette étape', 'Revenir à la version d''avant (sa sauvegarde)', 'Annuler') -Defaut 1 -OptionLigne '/reprendre ou /precedente'
    }
    if ($choix -eq 3) { Exit-Xpg -Code 2 }
    if ($choix -eq 2) {
        $p = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$(Join-Path $PSScriptRoot 'restore_backup.ps1')`"", '/precedente') -NoNewWindow -Wait -PassThru
        Exit-Xpg -Code $p.ExitCode
    }
    if (-not (Stop-XpgApplication $i)) { Stop-Xpg -Code 6 -Message "$($i.Nom) est ouvert : ferme-le d'abord." }
    if ($etat.etape -in @('sauvegarde', 'copie')) {
        # La copie des fichiers n'est pas finie : c'est le travail de l'installateur.
        Start-XpgEtape 'Relancer l''installateur (la copie n''était pas finie)' | Out-Null
        $setup = Get-XpgOption -Noms @('installateur')
        if (-not $setup) { $setup = (Get-ChildItem -LiteralPath (Join-Path $i.Reserve $etat.version) -Filter '*-Setup-*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1).FullName }
        if (-not $setup) { $setup = Read-XpgTexte -Question 'Chemin de XPGAnalyser-Setup-<version>.exe' -OptionLigne '/installateur:<fichier>' }
        if (-not $setup -or -not (Test-Path -LiteralPath $setup)) { Stop-Xpg -Code 7 -Message 'L''installateur est introuvable.' -Conseil 'Relance-le toi-même : il reprend l''installation (tes données ne sont pas touchées).' }
        $portee = '/CURRENTUSER'; if ($i.Portee -eq 'tous') { $portee = '/ALLUSERS' }
        $p = Start-Process -FilePath $setup -ArgumentList @('/SILENT', '/SUPPRESSMSGBOXES', '/NORESTART', $portee, "/DIR=`"$($i.Dossier)`"") -Wait -PassThru
        Complete-XpgEtape -Statut $(if ($p.ExitCode -eq 0) { 'OK' } else { 'ECHEC' }) -Detail "code $($p.ExitCode)"
        Exit-Xpg -Code $p.ExitCode
    }
    Start-XpgEtape "Reprise à l'étape « $($etat.etape) »" | Out-Null
    $c = Invoke-XpgEtapeInstallation -Installation $i -Args2 @('/etape:apres-copie')
    Complete-XpgEtape -Statut $(if ($c -eq 0) { 'OK' } else { 'ECHEC' }) -Detail "code $c"
    Exit-Xpg -Code $c
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

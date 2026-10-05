#Requires -Version 5.1
# =============================================================================
#  repair.ps1 - lance par repair.bat (menu Demarrer > XPGAnalyser > Maintenance)
#
#  Pas une reinstallation aveugle : on detecte, on verifie, puis on ne repare
#  que ce qui est casse, apres une sauvegarde. Les 21 etapes du cahier des
#  charges : 1-2 l'installation et sa version ; 3-4 l'application ouverte (la
#  fermer proprement) ; 5-9 le manifeste, les fichiers, tailles et SHA-256,
#  les DLL, le runtime ; 10 le registre ; 11 les raccourcis ; 12 les droits ;
#  13 la configuration ; 14 les chemins ; 15 une installation interrompue ;
#  16 la sauvegarde ; 17 les fichiers manquants ou abimes (depuis la reserve,
#  verifies) ; 18 les raccourcis ; 19 le registre ; 20 tes projets et tes
#  donnees ne sont pas touches ; 21 le rapport.
#
#  Modes : /mode:diagnostic | auto | fichiers | raccourcis | parametres |
#          dependances | reprise | precedente | reinstaller
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgInstallation.psm1') -Force -Global -DisableNameChecking

$usage = @'
repair.bat [/mode:<mode>] [options]
  Modes : diagnostic (rien n'est modifié), auto (réparation automatique), fichiers, raccourcis,
          parametres, dependances, reprise, precedente, reinstaller
  /installateur:<Setup.exe>  pour « reinstaller » si la réserve ne l'a pas
  /rapide                    tailles seulement (pas les SHA-256)
'@
$X = Initialize-Xpg -Nom 'repair' -Titre 'XPGAnalyser - diagnostic et réparation' -Arguments $Arguments -Genre maintenance -Usage $usage

$rapport = New-Object System.Collections.ArrayList
function Add-Rapport([string] $t) { [void]$rapport.Add($t) }

function Save-XpgRapport {
    param($Installation, [string] $Titre)
    $f = Join-Path $Installation.Journaux ("repair_{0}.txt" -f (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss'))
    $entete = @("$Titre - $($Installation.Nom) $($Installation.Version)", "Le $((Get-Date).ToString('dd/MM/yyyy HH:mm')) sur $env:COMPUTERNAME par $env:USERNAME", "Journal : $($X.Journal)", '')
    Write-XpgTexte -Chemin $f -Texte ((($entete + $rapport) -join "`r`n") + "`r`n")
    Write-XpgLog -Niveau INFO -Message "Rapport : $f"
    return $f
}

function Assert-XpgDroits {
    # Une installation pour tous les comptes se repare en administrateur : on se relance
    # eleve, pour ce mode seulement.
    param($Installation, [string] $Mode)
    if ($Installation.Portee -ne 'tous' -or (Test-XpgAdmin)) { return }
    Write-XpgLog -Niveau ATTENTION -Message 'Installation pour tous les comptes : cette réparation demande les droits d''administrateur.'
    if (-not (Confirm-Xpg -Question 'Relancer la réparation en administrateur ?' -Defaut $true)) { Stop-Xpg -Code 5 -Message 'Réparation abandonnée (droits insuffisants).' }
    $a = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath, "/mode:$Mode", '/eleve')
    foreach ($o in @('oui', 'non-interactif', 'details', 'rapide')) { if (Test-XpgOption @($o)) { $a += "/$o" } }
    $p = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList ($a | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -Verb RunAs -Wait -PassThru
    Exit-Xpg -Code $p.ExitCode
}

function Repair-XpgFichiers {
    param($Installation, $Problemes, [switch] $Tous)
    $res = Join-Path $Installation.Reserve $Installation.Version
    $n = 0; $echecs = @()
    $cibles = @($Problemes | Where-Object { $_.Categorie -eq 'fichiers' } | ForEach-Object { $_.Donnee })
    if ($Tous) {
        # Les dependances : SDL3.dll et le runtime, meme s'ils semblent bons.
        $noms = @($Installation.Manifeste.dependances) + @($Installation.Manifeste.runtime_local)
        $cibles = @($Installation.Manifeste.fichiers | Where-Object { $noms -contains $_.chemin } | ForEach-Object { [pscustomobject]@{ Chemin = $_.chemin; Attendu = $_.sha256 } })
    }
    foreach ($e in $cibles) {
        $src = Join-Path $res $e.Chemin
        if (-not (Test-Path -LiteralPath $src) -or (Get-XpgSha256 $src) -ne $e.Attendu) { $echecs += $e.Chemin; Add-Rapport "  $($e.Chemin) : la réserve ne l'a pas (ou abîmé)"; continue }
        $dst = Join-Path $Installation.Dossier $e.Chemin
        $d = Split-Path -Parent $dst
        if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Copy-Item -LiteralPath $src -Destination $dst -Force
        if ((Get-XpgSha256 $dst) -eq $e.Attendu) { $n++; Add-Rapport "  $($e.Chemin) : remis depuis la réserve, SHA-256 vérifié"; Write-XpgLog -Niveau OK -Message "$($e.Chemin) : remis" }
        else { $echecs += $e.Chemin; Add-Rapport "  $($e.Chemin) : ÉCHEC de la copie" }
    }
    return [pscustomobject]@{ Repares = $n; Echecs = $echecs }
}

function Repair-XpgRaccourcis {
    param($Installation, $Problemes)
    $n = 0
    foreach ($p in ($Problemes | Where-Object { $_.Categorie -eq 'raccourcis' })) {
        try { New-XpgRaccourci -Raccourci $p.Donnee -Dossier $Installation.Dossier; $n++; Add-Rapport "  raccourci recréé : $($p.Donnee.Chemin)" }
        catch { Add-Rapport "  raccourci $($p.Donnee.Chemin) : ÉCHEC $($_.Exception.Message)" }
    }
    return $n
}

function Repair-XpgRegistre {
    param($Installation)
    try {
        New-Item -Path $Installation.CleProduit -Force | Out-Null
        Set-ItemProperty -Path $Installation.CleProduit -Name 'InstallDir' -Value $Installation.Dossier
        Set-ItemProperty -Path $Installation.CleProduit -Name 'Version' -Value $Installation.Version
        Set-ItemProperty -Path $Installation.CleProduit -Name 'Portee' -Value $Installation.Portee
        Add-Rapport "  registre : $($Installation.CleProduit) réécrit"
        return $true
    } catch { Add-Rapport "  registre : ÉCHEC $($_.Exception.Message)"; return $false }
}

function Repair-XpgParametres {
    param($Installation, $Problemes)
    $n = 0
    foreach ($p in ($Problemes | Where-Object { $_.Categorie -eq 'parametres' })) {
        if ($p.Texte -like 'XPGAnalyser.ini illisible*') {
            Copy-Item -LiteralPath $Installation.IniUtilisateur -Destination "$($Installation.IniUtilisateur).abime-$((Get-Date).ToString('yyyyMMdd-HHmmss'))" -Force
            Remove-Item -LiteralPath $Installation.IniUtilisateur -Force
            Add-Rapport '  XPGAnalyser.ini illisible : mis de côté (.abime-...) ; les dossiers reviennent à ceux de l''installation'; $n++
        } elseif ($p.Texte -like 'dossier des*absent*' -and $p.Donnee) {
            New-Item -ItemType Directory -Path $p.Donnee -Force | Out-Null; Add-Rapport "  dossier recréé (vide) : $($p.Donnee)"; $n++
        } elseif ($p.Texte -like 'démarrage automatique*' -and $p.Donnee) {
            $exe = Join-Path $Installation.Dossier $Installation.Exe
            $v = [regex]::Replace($p.Donnee.Valeur, '^"[^"]*"', ('"' + $exe.Replace('$', '$$') + '"'))
            Set-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name $p.Donnee.Nom -Value $v
            Add-Rapport "  démarrage du poste d'exploitation : vers $exe (avant : $($p.Donnee.Valeur))"; $n++
        } elseif ($p.Texte -like 'écriture impossible*') {
            Add-Rapport "  $($p.Texte) : à régler à la main (droits du dossier, disque plein, lecteur réseau absent)"
        }
    }
    return $n
}

function Invoke-XpgEnfant {
    param([string] $Script, [string[]] $Args2)
    $a = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot $Script)) + $Args2
    foreach ($o in @('oui', 'non-interactif', 'details')) { if (Test-XpgOption @($o)) { $a += "/$o" } }
    $p = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList ($a | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -NoNewWindow -Wait -PassThru
    return $p.ExitCode
}

try {
    # ---- 1-2 : l'installation ------------------------------------------------------------------
    Start-XpgEtape 'Installation et version' | Out-Null
    $i = Get-XpgInstallation
    if (-not $i) { Stop-Xpg -Code 20 -Message 'Aucune installation de XPGAnalyser trouvée (ni à côté de ce script, ni dans le registre).' -Conseil 'Installe-la avec XPGAnalyser-Setup-<version>.exe.' }
    Complete-XpgEtape -Statut OK -Detail ("{0} {1} - {2}" -f $i.Nom, $i.Version, $i.Dossier)

    $modes = [ordered]@{ diagnostic = 'Diagnostic uniquement (rien n''est modifié)'; auto = 'Réparation automatique'; fichiers = 'Réparation des fichiers du programme'
        raccourcis = 'Réparation des raccourcis'; parametres = 'Réparation des paramètres et chemins'; dependances = 'Réinstallation des dépendances (SDL3, runtime Visual C++)'
        reprise = 'Reprise d''une installation interrompue'; precedente = 'Restauration de l''installation précédente'; reinstaller = 'Réinstallation complète avec conservation des données' }
    $mode = (Get-XpgOption -Noms @('mode')).ToLowerInvariant()
    if (-not $mode) {
        $choix = Read-XpgChoix -Question 'Que faire ?' -Options (@($modes.Values) + 'Quitter') -Defaut 1 -OptionLigne '/mode:diagnostic|auto|...'
        if ($choix -gt $modes.Count) { Exit-Xpg -Code 0 }
        $mode = @($modes.Keys)[$choix - 1]
    }
    if (-not $modes.Contains($mode)) { Stop-Xpg -Code 2 -Message "Mode inconnu : $mode" -Conseil ('Modes : ' + (@($modes.Keys) -join ', ')) }
    Write-XpgLog -Niveau INFO -Message "Mode : $($modes[$mode])"
    Add-Rapport "Mode : $($modes[$mode])"

    if ($mode -eq 'reprise') { Exit-Xpg -Code (Invoke-XpgEnfant -Script 'resume_installation.ps1' -Args2 @()) }
    if ($mode -eq 'precedente') { Assert-XpgDroits $i $mode; Exit-Xpg -Code (Invoke-XpgEnfant -Script 'restore_backup.ps1' -Args2 @('/precedente')) }

    # ---- 3-15 : le diagnostic ----------------------------------------------------------------------
    Start-XpgEtape 'Diagnostic (rien n''est modifié)' | Out-Null
    $diag = Invoke-XpgDiagnostic -Installation $i -Rapide:(Test-XpgOption @('rapide'))
    foreach ($l in $diag.Lignes) { Add-Rapport $l }
    $graves = @($diag.Problemes | Where-Object { $_.Gravite -ne 'info' })
    Complete-XpgEtape -Statut $(if ($graves.Count) { 'ATTENTION' } else { 'OK' }) -Detail ("{0} problème(s)" -f $graves.Count)
    Add-Rapport ''
    Add-Rapport ("Bilan : {0} problème(s)." -f $graves.Count)

    if ($mode -eq 'diagnostic') {
        if ($graves.Count) { Write-XpgLog -Niveau INFO -Message 'Pour réparer : repair.bat, mode « Réparation automatique » (/mode:auto).' }
        Save-XpgRapport $i 'Diagnostic' | Out-Null
        Exit-Xpg -Code $(if ($graves.Count) { 1 } else { 0 })
    }
    # (Dans un switch, $_ est la valeur du switch : le probleme est garde dans $pbm.)
    $aFaire = @($graves | Where-Object {
        $pbm = $_
        switch ($mode) { 'auto' { $true } 'fichiers' { $pbm.Categorie -eq 'fichiers' } 'raccourcis' { $pbm.Categorie -eq 'raccourcis' }
                         'parametres' { $pbm.Categorie -in @('parametres', 'registre') } 'dependances' { $pbm.Categorie -eq 'dependances' } default { $true } } })
    if (-not $aFaire.Count -and $mode -notin @('dependances', 'reinstaller')) {
        Write-XpgLog -Niveau OK -Message 'Rien à réparer pour ce mode.'
        Save-XpgRapport $i 'Réparation' | Out-Null
        Exit-Xpg -Code 0
    }
    Assert-XpgDroits $i $mode

    # ---- 3-4 : l'application fermee ; 16 : la sauvegarde ------------------------------------
    Start-XpgEtape 'Fermer l''application' | Out-Null
    if (-not (Stop-XpgApplication $i)) { Complete-XpgEtape -Statut ECHEC; Stop-Xpg -Code 6 -Message "$($i.Nom) est encore ouvert : rien n'a été modifié." -Conseil 'Ferme-le et relance la réparation.' }
    Complete-XpgEtape -Statut OK
    Start-XpgEtape 'Sauvegarde avant réparation' | Out-Null
    $s = New-XpgSauvegarde -Installation $i -Raison 'avant-reparation'
    Add-Rapport ''; Add-Rapport "Sauvegarde avant réparation : $s"
    Complete-XpgEtape -Statut OK -Detail $s

    Add-Rapport ''; Add-Rapport 'Réparations :'
    if ($mode -eq 'reinstaller' -or ($aFaire | Where-Object { $_.Reparation -eq 'reinstaller' })) {
        Start-XpgEtape 'Réinstallation complète (tes données sont gardées)' | Out-Null
        $setup = Get-XpgOption -Noms @('installateur')
        if (-not $setup) { $setup = (Get-ChildItem -LiteralPath (Join-Path $i.Reserve $i.Version) -Filter '*-Setup-*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1).FullName }
        if (-not $setup) { $setup = Read-XpgTexte -Question 'Chemin de XPGAnalyser-Setup-<version>.exe' -OptionLigne '/installateur:<fichier>' }
        if (-not $setup -or -not (Test-Path -LiteralPath $setup)) { Complete-XpgEtape -Statut ECHEC; Stop-Xpg -Code 7 -Message 'L''installateur est introuvable.' -Conseil 'Relance avec /installateur:<chemin de XPGAnalyser-Setup-*.exe>.' }
        $portee = '/CURRENTUSER'; if ($i.Portee -eq 'tous') { $portee = '/ALLUSERS' }
        $p = Start-Process -FilePath $setup -ArgumentList @('/SILENT', '/SUPPRESSMSGBOXES', '/NORESTART', $portee, "/DIR=`"$($i.Dossier)`"", '/MIGRER=non', "/LOG=`"$(Join-Path $i.Journaux ('reinstallation_' + (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss') + '.log'))`"") -Wait -PassThru
        Add-Rapport "  réinstallation : $setup, code $($p.ExitCode)"
        Complete-XpgEtape -Statut $(if ($p.ExitCode -eq 0) { 'OK' } else { 'ECHEC' }) -Detail "code $($p.ExitCode)"
    } else {
        # ---- 17 : les fichiers ; les dependances ------------------------------------------
        if ($mode -in @('auto', 'fichiers', 'dependances')) {
            Start-XpgEtape 'Fichiers du programme (depuis la réserve, vérifiés)' | Out-Null
            $r = Repair-XpgFichiers -Installation $i -Problemes $aFaire -Tous:($mode -eq 'dependances')
            if ($r.Echecs.Count) { Complete-XpgEtape -Statut ECHEC -Detail ("{0} remis, {1} impossible(s) : {2}" -f $r.Repares, $r.Echecs.Count, ($r.Echecs -join ', ')); Write-XpgLog -Niveau INFO -Message 'Il faut l''installateur : repair.bat /mode:reinstaller /installateur:<Setup.exe>.' }
            else { Complete-XpgEtape -Statut OK -Detail "$($r.Repares) fichier(s) remis" }
        }
        # ---- 18 : les raccourcis --------------------------------------------------------------
        if ($mode -in @('auto', 'raccourcis')) {
            Start-XpgEtape 'Raccourcis' | Out-Null
            $n = Repair-XpgRaccourcis -Installation $i -Problemes $aFaire
            Complete-XpgEtape -Statut OK -Detail "$n recréé(s)"
        }
        # ---- 19 : le registre ; 13-14 : les parametres ------------------------------------------
        if ($mode -in @('auto', 'parametres')) {
            if ($aFaire | Where-Object { $_.Categorie -eq 'registre' -and $_.Reparation -eq 'registre' }) {
                Start-XpgEtape 'Registre' | Out-Null
                $ok = Repair-XpgRegistre -Installation $i
                Complete-XpgEtape -Statut $(if ($ok) { 'OK' } else { 'ECHEC' })
            }
            Start-XpgEtape 'Paramètres et chemins' | Out-Null
            $n = Repair-XpgParametres -Installation $i -Problemes $aFaire
            Complete-XpgEtape -Statut OK -Detail "$n correction(s)"
        }
        if ($mode -eq 'auto' -and ($aFaire | Where-Object { $_.Categorie -eq 'reprise' })) {
            Start-XpgEtape 'Installation interrompue' | Out-Null
            $c = Invoke-XpgEnfant -Script 'resume_installation.ps1' -Args2 @('/reprendre')
            Complete-XpgEtape -Statut $(if ($c -eq 0) { 'OK' } else { 'ECHEC' }) -Detail "code $c"
        }
    }

    # ---- 20 : les donnees ne sont pas touchees ; 21 : le rapport ------------------------------
    Add-Rapport ''; Add-Rapport "Tes projets et tes données ($($i.Donnees)) : non touchés."
    Start-XpgEtape 'Nouvelle vérification' | Out-Null
    $i = Read-XpgInstallation -Dossier $i.Dossier
    $apres = Invoke-XpgDiagnostic -Installation $i -Rapide:(Test-XpgOption @('rapide'))
    $reste = @($apres.Problemes | Where-Object { $_.Gravite -eq 'erreur' })
    Add-Rapport ''; Add-Rapport ("Après réparation : {0} problème(s) grave(s)." -f $reste.Count)
    foreach ($p in $reste) { Add-Rapport "  reste : $($p.Texte)" }
    Complete-XpgEtape -Statut $(if ($reste.Count) { 'ECHEC' } else { 'OK' }) -Detail ("{0} problème(s) grave(s)" -f $reste.Count)
    Save-XpgRapport $i 'Réparation' | Out-Null
    Exit-Xpg -Code $(if ($reste.Count) { 1 } else { 0 })
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

#Requires -Version 5.1
# =============================================================================
#  installer_etape.ps1 - les etapes que l'installateur (Inno Setup) confie a
#  PowerShell, et que resume_installation.bat sait refaire :
#
#    /etape:avant-copie     l'etat "en cours", la sauvegarde de la version installee
#                           (programme, reglages, registre) avant qu'elle soit remplacee ;
#    /etape:apres-copie     les donnees de l'utilisateur (resources\, libs\ livres, sans
#                           rien ecraser), la reprise des anciennes versions (/sources:a|b),
#                           la desinstallation du lot 8 (/desinstaller-lot8), la
#                           verification du manifeste, l'etat "terminee" ;
#    /etape:desinstallation /supprimer-donnees:oui -> la corbeille (jamais sans ta
#                           confirmation dans la fenetre de desinstallation).
#
#  Chaque etape est notee dans <Etat>\Etat\installation.json : une coupure (courant,
#  plantage) se reprend a l'etape interrompue.
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgInstallation.psm1') -Force -Global -DisableNameChecking

$X = Initialize-Xpg -Nom 'installation' -Titre 'XPGAnalyser - installation (étapes PowerShell)' -Arguments ($Arguments + @('/non-interactif')) -Genre maintenance
$etape = Get-XpgOption -Noms @('etape')
$dossier = Get-XpgOption -Noms @('dossier')

function Set-XpgEtape {
    param($Installation, [string] $Etape, [string] $Statut = 'en_cours', [string] $Detail = '')
    $f = Get-XpgFichierEtat $Installation
    $e = Read-XpgEtat $f
    if (-not $e) { $e = [pscustomobject]@{ version = $Installation.Version; dossier = $Installation.Dossier; debut = (Get-Date).ToString('s'); etapes = @() } }
    $e | Add-Member -NotePropertyName 'statut' -NotePropertyValue $Statut -Force
    $e | Add-Member -NotePropertyName 'etape' -NotePropertyValue $Etape -Force
    $e | Add-Member -NotePropertyName 'detail' -NotePropertyValue $Detail -Force
    $e | Add-Member -NotePropertyName 'version' -NotePropertyValue $Installation.Version -Force
    $e | Add-Member -NotePropertyName 'journal' -NotePropertyValue $X.Journal -Force
    $e.etapes = @($e.etapes) + @([pscustomobject]@{ etape = $Etape; statut = $Statut; date = (Get-Date).ToString('s'); detail = $Detail })
    Save-XpgEtat -Chemin $f -Etat $e
    # Le statut du moment, seul sur une ligne : l'installateur (Inno Setup) le lit pour savoir
    # si l'installation precedente a ete interrompue (installation.json garde tout l'historique).
    Write-XpgTexte -Chemin (Join-Path (Split-Path -Parent $f) 'statut.txt') -Texte ("{0} {1}`r`n" -f $Statut, $Etape) -SansBom
}

try {
    if (-not $dossier) { Stop-Xpg -Code 2 -Message 'Il manque /dossier:<dossier du programme>.' }
    switch ($etape) {
        'avant-copie' {
            # Le programme n'est pas encore copie : on lit l'ancienne installation (s'il y en a une).
            $version = Get-XpgOption -Noms @('version')
            $ancienne = $null
            if (Test-Path -LiteralPath (Join-Path $dossier 'installation.ini')) { $ancienne = Read-XpgInstallation -Dossier $dossier }
            $cible = [pscustomobject]@{ Version = $version; Dossier = $dossier; Etat = (Get-XpgOption -Noms @('etat')) }
            if (-not $cible.Etat) { $cible.Etat = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'XpgAnalyzer' }
            Set-XpgEtape -Installation $cible -Etape 'sauvegarde'
            Start-XpgEtape 'Sauvegarde de la version installée' | Out-Null
            if ($ancienne) {
                $s = New-XpgSauvegarde -Installation $ancienne -Raison "avant-$version"
                Complete-XpgEtape -Statut OK -Detail $s
            } else {
                # Une premiere installation : les reglages seulement, s'il y en a.
                $tmp = [pscustomobject]@{ Dossier = ''; Version = ''; Reglages = (Get-XpgDossierReglages); Sauvegardes = (Join-Path $cible.Etat 'Sauvegardes'); CleProduit = ''; CleDesinstallation = '' }
                if (Test-Path -LiteralPath $tmp.Reglages) { $s = New-XpgSauvegarde -Installation $tmp -Raison "avant-$version" -SansProgramme; Complete-XpgEtape -Statut OK -Detail "réglages : $s" }
                else { Complete-XpgEtape -Statut IGNORE -Detail 'première installation, rien à sauvegarder' }
            }
            Set-XpgEtape -Installation $cible -Etape 'copie'
            Exit-Xpg -Code 0
        }
        'apres-copie' {
            $i = Read-XpgInstallation -Dossier $dossier
            Set-XpgEtape -Installation $i -Etape 'donnees'
            $rapport = New-Object System.Collections.ArrayList
            [void]$rapport.Add("XPGAnalyser $($i.Version) - installation du $((Get-Date).ToString('dd/MM/yyyy HH:mm')) ($($i.Portee))")
            [void]$rapport.Add("Programme : $($i.Dossier)")
            [void]$rapport.Add("Tes données : $($i.Donnees)")
            [void]$rapport.Add('')

            Start-XpgEtape 'Tes données : resources\ et libs\ livrés' | Out-Null
            foreach ($d in @($i.Donnees, $i.Projets, $i.Captures)) { if (-not (Test-XpgEcriture $d)) { Complete-XpgEtape -Statut ECHEC -Detail "écriture impossible : $d"; Set-XpgEtape -Installation $i -Etape 'donnees' -Statut 'echec' -Detail "écriture impossible : $d"; Stop-Xpg -Code 1 -Message "Écriture impossible dans $d." } }
            $livrees = Get-XpgEmpreintesLivrees
            [void]$rapport.Add('Fichiers livrés (resources, bibliothèque) :')
            $b1 = Update-XpgDonneesLivrees -De (Join-Path $i.Dossier 'resources') -Vers (Join-Path $i.Donnees 'resources') -Livrees $livrees -Version $i.Version -Rapport $rapport
            $b2 = Update-XpgDonneesLivrees -De (Join-Path $i.Dossier 'libs') -Vers $i.Bibliotheque -Livrees $livrees -Version $i.Version -Rapport $rapport
            [void]$rapport.Add(("  {0} copié(s), {1} identique(s), {2} mis à jour, {3} à toi gardé(s)" -f ($b1.Copies + $b2.Copies), ($b1.Identiques + $b2.Identiques), ($b1.MisAJour + $b2.MisAJour), ($b1.Tiens + $b2.Tiens)))
            Complete-XpgEtape -Statut $(if ($b1.Erreurs + $b2.Erreurs) { 'ATTENTION' } else { 'OK' }) -Detail ("{0} copié(s), {1} gardé(s) à toi" -f ($b1.Copies + $b2.Copies), ($b1.Tiens + $b2.Tiens))

            $sources = @((Get-XpgOption -Noms @('sources')) -split '\|' | Where-Object { $_ })
            if ($sources.Count) {
                Set-XpgEtape -Installation $i -Etape 'migration'
                Start-XpgEtape 'Reprise des anciennes versions' | Out-Null
                $trouvees = @(Get-XpgAnciennesDonnees -Dossiers $sources -Exclure $i.Dossier | Where-Object { $c = $_.Chemin; $sources | Where-Object { $_.TrimEnd('\') -ieq $c } })
                [void]$rapport.Add('')
                [void]$rapport.Add('Reprise des anciennes versions (copiées ; les originaux ne sont pas modifiés) :')
                $bm = Invoke-XpgMigration -Installation $i -Sources $trouvees -Rapport $rapport
                Complete-XpgEtape -Statut $(if ($bm.Erreurs) { 'ATTENTION' } else { 'OK' }) -Detail ("{0} projet(s), {1} fichier(s), {2} fichier(s) à toi gardés" -f $bm.Projets, $bm.Fichiers, $bm.Tiens)
                if ((Get-XpgOption -Noms @('desinstaller-lot8')) -eq 'oui') {
                    foreach ($s in ($trouvees | Where-Object { $_.Lot8 })) {
                        Start-XpgEtape 'Désinstallation de la version du lot 8 (ses fichiers de programme)' | Out-Null
                        $u = Join-Path $s.Chemin 'Desinstaller.exe'
                        if ($bm.Erreurs) { Complete-XpgEtape -Statut IGNORE -Detail 'reprise incomplète : l''ancienne version reste' ; continue }
                        if (Test-Path -LiteralPath $u) {
                            $p = Start-Process -FilePath $u -ArgumentList @('/S', "_?=$($s.Chemin)") -Wait -PassThru
                            Remove-Item -LiteralPath $u -Force -ErrorAction SilentlyContinue
                            [void]$rapport.Add("Version du lot 8 désinstallée (code $($p.ExitCode)) ; ses données restent dans $($s.Chemin).")
                            Complete-XpgEtape -Statut $(if ($p.ExitCode -eq 0) { 'OK' } else { 'ATTENTION' }) -Detail "code $($p.ExitCode)"
                        } else { Complete-XpgEtape -Statut IGNORE -Detail 'désinstallateur absent' }
                    }
                }
            }

            # 1.8.0 : les blocs choisis a la page « Ta bibliotheque » de l'assistant.
            $ajoutes = Get-XpgOption -Noms @('libs-ajoutees')
            if ($ajoutes -and (Test-Path -LiteralPath $ajoutes)) {
                Start-XpgEtape 'Ta bibliothèque : tes blocs ajoutés' | Out-Null
                [void]$rapport.Add('')
                [void]$rapport.Add('Tes blocs, ajoutés à l''installation :')
                $bb = Add-XpgBlocsAjoutes -Installation $i -Liste $ajoutes -Version $i.Version -Rapport $rapport
                Complete-XpgEtape -Statut $(if ($bb.Erreurs) { 'ATTENTION' } else { 'OK' }) -Detail ("{0} ajouté(s), {1} à la place d'un bloc livré (rangé à côté)" -f $bb.Ajoutes, $bb.Remplaces)
            }

            Set-XpgEtape -Installation $i -Etape 'verification'
            Start-XpgEtape 'Vérification du manifeste (tailles et SHA-256)' | Out-Null
            $ecarts = Test-XpgFichiers -Installation $i
            $total = @($i.Manifeste.fichiers).Count
            if ($null -eq $ecarts) { Complete-XpgEtape -Statut ECHEC -Detail 'manifeste.json illisible'; Set-XpgEtape -Installation $i -Etape 'verification' -Statut 'echec'; Stop-Xpg -Code 1 -Message 'manifeste.json illisible.' }
            if ($ecarts.Count) {
                foreach ($e in $ecarts) { Write-XpgLog -Niveau ERREUR -Message "$($e.Chemin) : $($e.Etat)" }
                Complete-XpgEtape -Statut ECHEC -Detail ("{0} fichier(s) sur {1} non conformes" -f $ecarts.Count, $total)
                Set-XpgEtape -Installation $i -Etape 'verification' -Statut 'echec' -Detail "$($ecarts.Count) fichier(s) non conformes"
                Stop-Xpg -Code 1 -Message 'Des fichiers installés ne correspondent pas au manifeste.' -Conseil 'Lance repair.bat (menu Démarrer > XPGAnalyser > Maintenance).'
            }
            Complete-XpgEtape -Statut OK -Detail ("{0}/{0} conformes" -f $total)
            [void]$rapport.Add('')
            [void]$rapport.Add("Vérification : $total/$total fichiers conformes au manifeste.")
            $f = Join-Path $i.Journaux ("migration_{0}.txt" -f (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss'))
            Write-XpgTexte -Chemin $f -Texte (($rapport -join "`r`n") + "`r`n")
            Write-XpgLog -Niveau INFO -Message "Rapport : $f"
            Set-XpgEtape -Installation $i -Etape 'terminee' -Statut 'terminee' -Detail $f
            Exit-Xpg -Code 0
        }
        'desinstallation' {
            $i = Read-XpgInstallation -Dossier $dossier
            Start-XpgEtape 'Nettoyage de la maintenance (réserve, état)' | Out-Null
            foreach ($d in @((Join-Path $i.Reserve $i.Version), (Join-Path $i.Etat 'Etat'))) {
                if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force -ErrorAction SilentlyContinue }
            }
            if ((Test-Path -LiteralPath $i.Reserve) -and -not (Get-ChildItem -LiteralPath $i.Reserve -Force -ErrorAction SilentlyContinue)) { Remove-Item -LiteralPath $i.Reserve -Force -ErrorAction SilentlyContinue }
            Complete-XpgEtape -Statut OK
            if ((Get-XpgOption -Noms @('supprimer-donnees')) -eq 'oui') {
                Start-XpgEtape 'Tes données et tes réglages vers la Corbeille (demandé)' | Out-Null
                Add-Type -AssemblyName Microsoft.VisualBasic
                $partis = @()
                foreach ($d in @($i.Donnees, $i.Reglages, $i.Sauvegardes)) {
                    if ($d -and (Test-Path -LiteralPath $d)) {
                        try { [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($d, 'OnlyErrorDialogs', 'SendToRecycleBin'); $partis += $d } catch { Write-XpgLog -Niveau ATTENTION -Message "$d : $($_.Exception.Message)" }
                    }
                }
                foreach ($d in @($i.Projets, $i.Bibliotheque, $i.Captures)) {
                    if ($d -and (Test-Path -LiteralPath $d) -and -not $d.StartsWith($i.Donnees, [StringComparison]::OrdinalIgnoreCase)) { Write-XpgLog -Niveau INFO -Message "Gardé (hors du dossier des données, peut-être partagé) : $d" }
                }
                Complete-XpgEtape -Statut OK -Detail ("dans la Corbeille : " + ($partis -join ' ; '))
            }
            Exit-Xpg -Code 0
        }
        default { Stop-Xpg -Code 2 -Message "Étape inconnue : « $etape » (avant-copie, apres-copie, desinstallation)." }
    }
} catch {
    try { if ($i) { Set-XpgEtape -Installation $i -Etape $etape -Statut 'echec' -Detail $_.Exception.Message } } catch { }
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

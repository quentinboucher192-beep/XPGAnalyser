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
#
#  1.12.29 : /progres:<fichier> (l'assistant le donne) - une ligne courte par action
#  (Write-XpgProgres), que l'assistant montre dans ses « Détails » pendant que l'etape
#  tourne : la sauvegarde, tes dossiers, chaque ancienne version reprise et ce qui en est
#  pris, la desinstallation du lot 8, la verification. Le journal ne change pas.
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
            Write-XpgProgres -Etat info -Texte 'Chaque étape est notée : une coupure (courant, plantage) se reprend là où elle s''est arrêtée'
            Start-XpgEtape 'Sauvegarde de la version installée' | Out-Null
            if ($ancienne) {
                Write-XpgProgres -Etat encours -Cle 'sauvegarde' -Texte ("Sauvegarde de la version {0} avant de la remplacer…" -f $ancienne.Version)
                $s = New-XpgSauvegarde -Installation $ancienne -Raison "avant-$version" -CleProgres 'sauvegarde'
                Complete-XpgEtape -Statut OK -Detail $s
            } else {
                # Une premiere installation : les reglages seulement, s'il y en a.
                $tmp = [pscustomobject]@{ Dossier = ''; Version = ''; Reglages = (Get-XpgDossierReglages); Sauvegardes = (Join-Path $cible.Etat 'Sauvegardes'); CleProduit = ''; CleDesinstallation = '' }
                if (Test-Path -LiteralPath $tmp.Reglages) {
                    Write-XpgProgres -Etat encours -Cle 'sauvegarde' -Texte 'Sauvegarde de tes réglages (pas de version installée)…'
                    $s = New-XpgSauvegarde -Installation $tmp -Raison "avant-$version" -SansProgramme -CleProgres 'sauvegarde'; Complete-XpgEtape -Statut OK -Detail "réglages : $s"
                }
                else {
                    Write-XpgProgres -Etat info -Texte 'Rien à sauvegarder : pas de version installée, pas de réglages'
                    Complete-XpgEtape -Statut IGNORE -Detail 'première installation, rien à sauvegarder'
                }
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
            Write-XpgProgres -Etat encours -Cle 'dossiers' -Texte ("Tes dossiers de données : {0}…" -f (Format-XpgCheminCourt $i.Donnees))
            foreach ($d in @($i.Donnees, $i.Projets, $i.Captures)) { if (-not (Test-XpgEcriture $d)) { Write-XpgProgres -Etat erreur -Cle 'dossiers' -Texte ("Écriture impossible dans {0}" -f $d); Complete-XpgEtape -Statut ECHEC -Detail "écriture impossible : $d"; Set-XpgEtape -Installation $i -Etape 'donnees' -Statut 'echec' -Detail "écriture impossible : $d"; Stop-Xpg -Code 1 -Message "Écriture impossible dans $d." } }
            Write-XpgProgres -Etat fait -Cle 'dossiers' -Texte ("Tes dossiers sont prêts : {0}" -f (Format-XpgCheminCourt $i.Donnees))
            # ceux que tu as mis ailleurs (accueil > Dossiers), un par ligne ; sinon, une ligne
            $ailleurs = @(@(@('projets', $i.Projets), @('bibliothèque', $i.Bibliotheque), @('captures', $i.Captures)) | Where-Object { -not $_[1].StartsWith($i.Donnees + '\', [StringComparison]::OrdinalIgnoreCase) })
            if ($ailleurs.Count) { foreach ($a in $ailleurs) { Write-XpgProgres -Etat info -Niveau 1 -Texte ("{0} : {1}" -f $a[0], (Format-XpgCheminCourt $a[1])) } }
            else { Write-XpgProgres -Etat info -Niveau 1 -Texte 'projets\, libs\, resources\ et captures\ sont dedans' }
            $livrees = Get-XpgEmpreintesLivrees
            [void]$rapport.Add('Fichiers livrés (resources, bibliothèque) :')
            Write-XpgProgres -Etat encours -Cle 'livres' -Texte 'Le catalogue et la bibliothèque livrés vont dans tes données…'
            $b1 = Update-XpgDonneesLivrees -De (Join-Path $i.Dossier 'resources') -Vers (Join-Path $i.Donnees 'resources') -Livrees $livrees -Version $i.Version -Rapport $rapport -CleProgres 'livres' -Libelle 'resources'
            $b2 = Update-XpgDonneesLivrees -De (Join-Path $i.Dossier 'libs') -Vers $i.Bibliotheque -Livrees $livrees -Version $i.Version -Rapport $rapport -CleProgres 'livres' -Libelle 'libs'
            [void]$rapport.Add(("  {0} copié(s), {1} identique(s), {2} mis à jour, {3} à toi gardé(s)" -f ($b1.Copies + $b2.Copies), ($b1.Identiques + $b2.Identiques), ($b1.MisAJour + $b2.MisAJour), ($b1.Tiens + $b2.Tiens)))
            Complete-XpgEtape -Statut $(if ($b1.Erreurs + $b2.Erreurs) { 'ATTENTION' } else { 'OK' }) -Detail ("{0} copié(s), {1} gardé(s) à toi" -f ($b1.Copies + $b2.Copies), ($b1.Tiens + $b2.Tiens))
            # (les comptes à zéro ne sont pas dits)
            $m = @()
            if ($b1.Copies + $b2.Copies) { $m += ("{0} copié(s)" -f ($b1.Copies + $b2.Copies)) }
            if ($b1.MisAJour + $b2.MisAJour) { $m += ("{0} mis à jour" -f ($b1.MisAJour + $b2.MisAJour)) }
            if ($b1.Identiques + $b2.Identiques) { $m += ("{0} déjà là" -f ($b1.Identiques + $b2.Identiques)) }
            if ($b1.Tiens + $b2.Tiens) { $m += ("{0} à toi gardé(s)" -f ($b1.Tiens + $b2.Tiens)) }
            if (-not $m.Count) { $m = @('rien à copier') }
            Write-XpgProgres -Etat $(if ($b1.Erreurs + $b2.Erreurs) { 'attention' } else { 'fait' }) -Cle 'livres' -Texte ("Catalogue et bibliothèque livrés : {0}" -f ($m -join ', '))

            $sources = @((Get-XpgOption -Noms @('sources')) -split '\|' | Where-Object { $_ })
            if ($sources.Count) {
                Set-XpgEtape -Installation $i -Etape 'migration'
                Start-XpgEtape 'Reprise des anciennes versions' | Out-Null
                Write-XpgProgres -Etat encours -Cle 'reprise' -Texte ("Reprise de tes anciennes versions : {0} dossier(s) à regarder…" -f $sources.Count)
                $trouvees = @(Get-XpgAnciennesDonnees -Dossiers $sources -Exclure $i.Dossier | Where-Object { $c = $_.Chemin; $sources | Where-Object { $_.TrimEnd('\') -ieq $c } })
                foreach ($t in $trouvees) {
                    # ce qu'il y a (projets, libs, resources, captures) et combien, en une ligne
                    $quoi = @($t.Parties | ForEach-Object { if ($_.Sous -like '*projets') { 'projets' } else { $_.Sous } } | Select-Object -Unique)
                    $nf = [int](($t.Parties | Measure-Object -Property Fichiers -Sum).Sum)
                    $no = [double](($t.Parties | Measure-Object -Property Taille -Sum).Sum)
                    Write-XpgProgres -Etat info -Niveau 1 -Texte ("{0} : {1} ({2} fichier(s), {3})" -f $t.Chemin, ($quoi -join ', '), $nf, (Format-XpgTaille $no))
                }
                [void]$rapport.Add('')
                [void]$rapport.Add('Reprise des anciennes versions (copiées ; les originaux ne sont pas modifiés) :')
                $bm = Invoke-XpgMigration -Installation $i -Sources $trouvees -Rapport $rapport
                Complete-XpgEtape -Statut $(if ($bm.Erreurs) { 'ATTENTION' } else { 'OK' }) -Detail ("{0} projet(s), {1} fichier(s), {2} fichier(s) à toi gardés" -f $bm.Projets, $bm.Fichiers, $bm.Tiens)
                if ($trouvees.Count) {
                    Write-XpgProgres -Etat $(if ($bm.Erreurs) { 'attention' } else { 'fait' }) -Cle 'reprise' -Texte ("Reprise terminée : {0} projet(s), {1} fichier(s), copiés (les originaux ne sont pas modifiés)" -f $bm.Projets, $bm.Fichiers)
                } else {
                    Write-XpgProgres -Etat info -Cle 'reprise' -Texte 'Rien à reprendre dans ces dossiers (ni projets, ni bibliothèque, ni captures)'
                }
                if ((Get-XpgOption -Noms @('desinstaller-lot8')) -eq 'oui') {
                    foreach ($s in ($trouvees | Where-Object { $_.Lot8 })) {
                        Start-XpgEtape 'Désinstallation de la version du lot 8 (ses fichiers de programme)' | Out-Null
                        $u = Join-Path $s.Chemin 'Desinstaller.exe'
                        if ($bm.Erreurs) {
                            Write-XpgProgres -Etat attention -Cle 'lot8' -Texte 'La reprise n''est pas complète : l''ancienne version (lot 8) reste installée'
                            Complete-XpgEtape -Statut IGNORE -Detail 'reprise incomplète : l''ancienne version reste' ; continue
                        }
                        if (Test-Path -LiteralPath $u) {
                            Write-XpgProgres -Etat encours -Cle 'lot8' -Texte 'Désinstallation de l''ancienne version (lot 8 : son programme seulement)…'
                            $p = Start-Process -FilePath $u -ArgumentList @('/S', "_?=$($s.Chemin)") -Wait -PassThru
                            Remove-Item -LiteralPath $u -Force -ErrorAction SilentlyContinue
                            [void]$rapport.Add("Version du lot 8 désinstallée (code $($p.ExitCode)) ; ses données restent dans $($s.Chemin).")
                            Complete-XpgEtape -Statut $(if ($p.ExitCode -eq 0) { 'OK' } else { 'ATTENTION' }) -Detail "code $($p.ExitCode)"
                            $t = "Ancienne version (lot 8) désinstallée (code {0}) ; ses données restent dans {1}" -f $p.ExitCode, $s.Chemin
                            Write-XpgProgres -Etat $(if ($p.ExitCode -eq 0) { 'fait' } else { 'attention' }) -Cle 'lot8' -Texte $t
                            Write-XpgProgres -Etat recap -Cle 'reprise' -Texte $t
                        } else {
                            Write-XpgProgres -Etat info -Cle 'lot8' -Texte 'Ancienne version (lot 8) : son désinstallateur n''est plus là, rien à désinstaller'
                            Complete-XpgEtape -Statut IGNORE -Detail 'désinstallateur absent'
                        }
                    }
                }
            }

            # 1.8.0 : les blocs choisis a la page « Ta bibliotheque » de l'assistant.
            $ajoutes = Get-XpgOption -Noms @('libs-ajoutees')
            if ($ajoutes -and (Test-Path -LiteralPath $ajoutes)) {
                Start-XpgEtape 'Ta bibliothèque : tes blocs ajoutés' | Out-Null
                [void]$rapport.Add('')
                [void]$rapport.Add('Tes blocs, ajoutés à l''installation :')
                Write-XpgProgres -Etat encours -Cle 'blocs' -Texte 'Tes blocs vont dans ta bibliothèque…'
                $bb = Add-XpgBlocsAjoutes -Installation $i -Liste $ajoutes -Version $i.Version -Rapport $rapport -CleProgres 'blocs'
                Complete-XpgEtape -Statut $(if ($bb.Erreurs) { 'ATTENTION' } else { 'OK' }) -Detail ("{0} ajouté(s), {1} à la place d'un bloc livré (rangé à côté)" -f $bb.Ajoutes, $bb.Remplaces)
                $t = "Tes blocs : {0} ajouté(s) à ta bibliothèque, {1} à la place d'un bloc livré (rangé à côté)" -f $bb.Ajoutes, $bb.Remplaces
                Write-XpgProgres -Etat $(if ($bb.Erreurs) { 'attention' } else { 'fait' }) -Cle 'blocs' -Texte $t
                Write-XpgProgres -Etat recap -Cle 'blocs' -Texte $t
            }

            Set-XpgEtape -Installation $i -Etape 'verification'
            Start-XpgEtape 'Vérification du manifeste (tailles et SHA-256)' | Out-Null
            # (1.14.0 : sans l'exe que le profil n'a pas installe)
            $total = @($i.Manifeste.fichiers | Where-Object { @($i.Absents) -notcontains $_.chemin }).Count
            Write-XpgProgres -Etat encours -Cle 'verif' -Texte ("Vérification des fichiers : 0 / {0} (taille et SHA-256)" -f $total)
            $ecarts = Test-XpgFichiers -Installation $i -CleProgres 'verif'
            if ($null -eq $ecarts) { Write-XpgProgres -Etat erreur -Cle 'verif' -Texte 'Vérification impossible : manifeste.json est illisible'; Complete-XpgEtape -Statut ECHEC -Detail 'manifeste.json illisible'; Set-XpgEtape -Installation $i -Etape 'verification' -Statut 'echec'; Stop-Xpg -Code 1 -Message 'manifeste.json illisible.' }
            if ($ecarts.Count) {
                foreach ($e in $ecarts) { Write-XpgLog -Niveau ERREUR -Message "$($e.Chemin) : $($e.Etat)" }
                Write-XpgProgres -Etat erreur -Cle 'verif' -Texte ("{0} fichier(s) sur {1} ne correspondent pas au manifeste" -f $ecarts.Count, $total)
                foreach ($e in @($ecarts | Select-Object -First 6)) { Write-XpgProgres -Etat erreur -Niveau 1 -Texte ("{0} : {1}" -f $e.Chemin, $e.Etat) }
                Complete-XpgEtape -Statut ECHEC -Detail ("{0} fichier(s) sur {1} non conformes" -f $ecarts.Count, $total)
                Set-XpgEtape -Installation $i -Etape 'verification' -Statut 'echec' -Detail "$($ecarts.Count) fichier(s) non conformes"
                Stop-Xpg -Code 1 -Message 'Des fichiers installés ne correspondent pas au manifeste.' -Conseil 'Lance repair.bat (menu Démarrer > XPGAnalyser > Maintenance).'
            }
            Complete-XpgEtape -Statut OK -Detail ("{0}/{0} conformes" -f $total)
            Write-XpgProgres -Etat fait -Cle 'verif' -Texte ("{0} / {0} fichiers vérifiés : taille et SHA-256 conformes au manifeste" -f $total)
            [void]$rapport.Add('')
            [void]$rapport.Add("Vérification : $total/$total fichiers conformes au manifeste.")
            $f = Join-Path $i.Journaux ("migration_{0}.txt" -f (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss'))
            Write-XpgTexte -Chemin $f -Texte (($rapport -join "`r`n") + "`r`n")
            Write-XpgLog -Niveau INFO -Message "Rapport : $f"
            Write-XpgProgres -Etat info -Texte ("Le rapport : {0}" -f (Format-XpgCheminCourt $f))
            Write-XpgProgres -Etat recap -Cle 'rapport' -Texte $f
            Set-XpgEtape -Installation $i -Etape 'terminee' -Statut 'terminee' -Detail $f
            Exit-Xpg -Code 0
        }
        'desinstallation' {
            $i = Read-XpgInstallation -Dossier $dossier
            Start-XpgEtape 'Nettoyage de la maintenance (copies de secours, état)' | Out-Null
            # 1.14.0 : toutes les copies de secours (avant : celle de la version installee seulement - celles des
            # versions d'avant restaient)
            if ($i.Reserve -and ((Split-Path -Leaf $i.Reserve) -ieq 'Reserve') -and (Test-Path -LiteralPath $i.Reserve)) { Remove-Item -LiteralPath $i.Reserve -Recurse -Force -ErrorAction SilentlyContinue }
            $d = Join-Path $i.Etat 'Etat'
            if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force -ErrorAction SilentlyContinue }
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
    Write-XpgProgres -Etat erreur -Texte ("Erreur inattendue : {0}" -f $_.Exception.Message)
    try { if ($i) { Set-XpgEtape -Installation $i -Etape $etape -Statut 'echec' -Detail $_.Exception.Message } } catch { }
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

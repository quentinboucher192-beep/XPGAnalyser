#Requires -Version 5.1
# =============================================================================
#  XpgInstallation.psm1 - l'installation de XPGAnalyser, vue par la maintenance
# -----------------------------------------------------------------------------
#  Trouver l'installation (le dossier du script, le registre, la cle de
#  desinstallation), la lire (installation.ini, manifeste.json), la verifier
#  (fichiers, tailles, SHA-256, DLL, runtime, registre, raccourcis, droits,
#  configuration, chemins), la sauvegarder et la restaurer, reprendre les
#  anciennes donnees (migration) sans jamais rien ecraser ni supprimer.
#  Utilise par repair, verify_installation, restore_backup,
#  resume_installation, reset_user_settings, collect_diagnostics, et par
#  l'installateur lui-meme (installer_etape.ps1).
# =============================================================================

Set-StrictMode -Off
$ErrorActionPreference = 'Stop'

# ------------------------------------------------------------------ trouver ----
function Get-XpgDossierReglages { return (Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'XpgAnalyzer') }

function Expand-XpgJetonsDonnees {
    # Les jetons de l'application (app/Dossiers.hpp) : {Documents}, {AppData}, {LocalAppData},
    # {ProgramData}, {Programme}, {Donnees} ; %VARIABLES% ; un relatif part de -Base.
    param([string] $Valeur, [string] $Programme, [string] $Donnees = '', [string] $Base = '')
    if (-not $Valeur) { return '' }
    $v = $Valeur.Trim().Trim('"')
    $table = @{ documents = [Environment]::GetFolderPath('MyDocuments'); appdata = [Environment]::GetFolderPath('ApplicationData')
                localappdata = [Environment]::GetFolderPath('LocalApplicationData'); programdata = [Environment]::GetFolderPath('CommonApplicationData')
                programme = $Programme; donnees = $Donnees }
    $v = [regex]::Replace($v, '\{([^}]+)\}', { param($m) $k = $m.Groups[1].Value.ToLowerInvariant(); if ($table.ContainsKey($k) -and $table[$k]) { return $table[$k] } else { return $m.Value } })
    $v = [Environment]::ExpandEnvironmentVariables($v)
    if (-not [System.IO.Path]::IsPathRooted($v) -and $Base) { $v = Join-Path $Base $v }
    try { $v = [System.IO.Path]::GetFullPath($v) } catch { }
    return $v.TrimEnd('\')
}

function Read-XpgIniSimple {
    param([string] $Chemin)
    if (-not $Chemin -or -not (Test-Path -LiteralPath $Chemin)) { return $null }
    try { return (Read-XpgIni $Chemin) } catch { return $null }
}

function Get-XpgValeurIni { param($Ini, [string] $Section, [string] $Cle) if ($null -eq $Ini) { return '' }; $s = $Section.ToLowerInvariant(); if ($Ini.ContainsKey($s) -and $Ini[$s].ContainsKey($Cle.ToLowerInvariant())) { return [string]$Ini[$s][$Cle.ToLowerInvariant()] }; return '' }

function Get-XpgInstallation {
    # L'installation : celle du dossier de ce script s'il en est une, sinon celle du registre
    # (HKCU puis HKLM : Software\<nom>, puis la cle de desinstallation d'Inno Setup).
    param([string] $Dossier = '')
    $ctx = Get-XpgContexte
    $nom = Get-XpgConfig -Section 'Produit' -Cle 'nom' -Defaut 'XPGAnalyser'
    $guid = (Get-XpgConfig -Section 'Produit' -Cle 'app_id' -Defaut '').Trim('{', '}')
    $candidats = @()
    if ($Dossier) { $candidats += $Dossier }
    if ($ctx -and (Test-Path -LiteralPath (Join-Path $ctx.Racine 'installation.ini'))) { $candidats += $ctx.Racine }
    foreach ($racineReg in @('HKCU:', 'HKLM:')) {
        try { $k = Get-ItemProperty -Path "$racineReg\Software\$nom" -ErrorAction Stop; if ($k.InstallDir) { $candidats += $k.InstallDir } } catch { }
        if ($guid) {
            foreach ($vue in @("$racineReg\Software\Microsoft\Windows\CurrentVersion\Uninstall\{$guid}_is1", "$racineReg\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{$guid}_is1")) {
                try { $k = Get-ItemProperty -Path $vue -ErrorAction Stop; if ($k.InstallLocation) { $candidats += $k.InstallLocation.TrimEnd('\') } } catch { }
            }
        }
    }
    foreach ($c in $candidats) {
        if (-not $c) { continue }
        $c = $c.TrimEnd('\')
        $ini = Join-Path $c 'installation.ini'
        if (-not (Test-Path -LiteralPath $ini)) { continue }
        return (Read-XpgInstallation -Dossier $c)
    }
    return $null
}

function Read-XpgInstallation {
    param([string] $Dossier)
    $ini = Read-XpgIniSimple (Join-Path $Dossier 'installation.ini')
    $manif = $null
    $fm = Join-Path $Dossier 'manifeste.json'
    if (Test-Path -LiteralPath $fm) { try { $manif = [System.IO.File]::ReadAllText($fm) | ConvertFrom-Json } catch { } }
    $portee = Get-XpgValeurIni $ini 'Installation' 'portee'
    if (-not $portee) { $portee = 'utilisateur' }
    $etatBrut = Get-XpgValeurIni $ini 'Maintenance' 'etat'
    if (-not $etatBrut) { if ($portee -eq 'tous') { $etatBrut = '{ProgramData}\XPGAnalyser' } else { $etatBrut = '{LocalAppData}\XpgAnalyzer' } }
    $etat = Expand-XpgJetonsDonnees -Valeur $etatBrut -Programme $Dossier -Base $Dossier
    $i = [pscustomobject]@{
        Dossier = $Dossier; Ini = $ini; Manifeste = $manif
        Nom = (Get-XpgValeurIni $ini 'Installation' 'nom'); Version = (Get-XpgValeurIni $ini 'Installation' 'version'); Portee = $portee
        Exe = (Get-XpgValeurIni $ini 'Installation' 'exe'); Date = (Get-XpgValeurIni $ini 'Installation' 'date')
        Etat = $etat
        Journaux = (Expand-XpgJetonsDonnees -Valeur (Get-XpgValeurIni $ini 'Maintenance' 'journaux') -Programme $Dossier -Base $Dossier)
        Sauvegardes = (Expand-XpgJetonsDonnees -Valeur (Get-XpgValeurIni $ini 'Maintenance' 'sauvegardes') -Programme $Dossier -Base $Dossier)
        Reserve = (Expand-XpgJetonsDonnees -Valeur (Get-XpgValeurIni $ini 'Maintenance' 'reserve') -Programme $Dossier -Base $Dossier)
        Reglages = (Get-XpgDossierReglages)
        IniUtilisateur = (Join-Path (Get-XpgDossierReglages) 'XPGAnalyser.ini')
        Donnees = ''; Projets = ''; Bibliotheque = ''; Captures = ''
        CleProduit = ''; CleDesinstallation = ''
        RaccourciBureau = ((Get-XpgValeurIni $ini 'Installation' 'raccourci_bureau') -eq 'oui')
    }
    if (-not $i.Nom) { $i.Nom = Get-XpgConfig -Section 'Produit' -Cle 'nom' -Defaut 'XPGAnalyser' }
    if (-not $i.Exe) { $i.Exe = Get-XpgConfig -Section 'Produit' -Cle 'exe' -Defaut 'XpgAnalyzer.exe' }
    if (-not $i.Version -and $manif) { $i.Version = $manif.version }
    if (-not $i.Journaux) { $i.Journaux = Join-Path $etat 'Journaux' }
    if (-not $i.Sauvegardes) { $i.Sauvegardes = Join-Path $etat 'Sauvegardes' }
    if (-not $i.Reserve) { $i.Reserve = Join-Path $etat 'Reserve' }
    $hive = 'HKCU:'; if ($portee -eq 'tous') { $hive = 'HKLM:' }
    $i.CleProduit = "$hive\Software\$($i.Nom)"
    $guid = (Get-XpgConfig -Section 'Produit' -Cle 'app_id' -Defaut '').Trim('{', '}')
    if ($guid) { $i.CleDesinstallation = "$hive\Software\Microsoft\Windows\CurrentVersion\Uninstall\{$guid}_is1" }
    Update-XpgDossiersDonnees $i
    return $i
}

function Update-XpgDossiersDonnees {
    # Les dossiers de l'utilisateur courant, comme l'application les calcule : XPGAnalyser.ini,
    # puis installation.ini, puis {Documents}\XPGAnalyser.
    param($Installation)
    $user = Read-XpgIniSimple $Installation.IniUtilisateur
    $brut = Get-XpgValeurIni $user 'Dossiers' 'donnees'
    if (-not $brut) { $brut = Get-XpgValeurIni $Installation.Ini 'Dossiers' 'donnees' }
    if (-not $brut) { $brut = '{Documents}\XPGAnalyser' }
    $d = Expand-XpgJetonsDonnees -Valeur $brut -Programme $Installation.Dossier -Base $Installation.Dossier
    $Installation.Donnees = $d
    foreach ($p in @(@('projets', 'Projets', 'projets'), @('bibliotheque', 'Bibliotheque', 'libs'), @('captures', 'Captures', 'captures'))) {
        $b = Get-XpgValeurIni $user 'Dossiers' $p[0]
        if (-not $b) { $b = Get-XpgValeurIni $Installation.Ini 'Dossiers' $p[0] }
        if ($b) { $v = Expand-XpgJetonsDonnees -Valeur $b -Programme $Installation.Dossier -Donnees $d -Base $d } else { $v = Join-Path $d $p[2] }
        $Installation.($p[1]) = $v
    }
}

# ------------------------------------------------------------ l'application ----
function Get-XpgProcessus {
    param($Installation)
    $exe = Join-Path $Installation.Dossier $Installation.Exe
    return @(Get-Process -ErrorAction SilentlyContinue | Where-Object { try { $_.Path -and ($_.Path -ieq $exe) } catch { $false } })
}

function Stop-XpgApplication {
    # Fermer proprement (comme un clic sur la croix : l'application propose d'enregistrer),
    # attendre ; forcer seulement avec ton accord.
    param($Installation, [int] $Secondes = 30)
    $p = Get-XpgProcessus $Installation
    if (-not $p.Count) { return $true }
    Write-XpgLog -Niveau ATTENTION -Message ("{0} est ouvert ({1} fenêtre(s))." -f $Installation.Nom, $p.Count)
    if (-not (Confirm-Xpg -Question "Le fermer proprement maintenant ? (il proposera d'enregistrer ton travail)" -Defaut $true)) { return $false }
    foreach ($x in $p) { try { [void]$x.CloseMainWindow() } catch { } }
    $fin = (Get-Date).AddSeconds($Secondes)
    while ((Get-Date) -lt $fin) { Start-Sleep -Milliseconds 500; if (-not (Get-XpgProcessus $Installation).Count) { Write-XpgLog -Niveau OK -Message 'Fermé.'; return $true } }
    Write-XpgLog -Niveau ATTENTION -Message "Toujours ouvert après $Secondes s (une question attend peut-être une réponse)."
    if (Confirm-Xpg -Question 'Le forcer à s''arrêter ? (ce qui n''est pas enregistré sera perdu)' -Defaut $false) {
        foreach ($x in (Get-XpgProcessus $Installation)) { try { Stop-Process -Id $x.Id -Force } catch { } }
        Start-Sleep -Seconds 1
        return (-not (Get-XpgProcessus $Installation).Count)
    }
    return $false
}

# ----------------------------------------------------------- la verification ----
function Test-XpgFichiers {
    # Chaque fichier du manifeste : present, sa taille, son SHA-256. Rend la liste des ecarts.
    param($Installation, [string] $Dossier = '', [switch] $SansEmpreinte)
    if (-not $Dossier) { $Dossier = $Installation.Dossier }
    $ecarts = New-Object System.Collections.ArrayList
    if (-not $Installation.Manifeste) { return $null }
    foreach ($f in $Installation.Manifeste.fichiers) {
        $p = Join-Path $Dossier $f.chemin
        if (-not (Test-Path -LiteralPath $p)) { [void]$ecarts.Add([pscustomobject]@{ Chemin = $f.chemin; Etat = 'absent'; Role = $f.role; Attendu = $f.sha256 }); continue }
        $taille = (Get-Item -LiteralPath $p).Length
        if ($taille -ne [int64]$f.taille) { [void]$ecarts.Add([pscustomobject]@{ Chemin = $f.chemin; Etat = 'taille différente'; Role = $f.role; Attendu = $f.sha256 }); continue }
        if (-not $SansEmpreinte -and (Get-XpgSha256 $p) -ne $f.sha256) { [void]$ecarts.Add([pscustomobject]@{ Chemin = $f.chemin; Etat = 'contenu différent'; Role = $f.role; Attendu = $f.sha256 }) }
    }
    return , @($ecarts)
}

function Get-XpgRaccourcis {
    param($Installation)
    $menu = [Environment]::GetFolderPath('Programs'); $bureau = [Environment]::GetFolderPath('DesktopDirectory')
    if ($Installation.Portee -eq 'tous') { $menu = [Environment]::GetFolderPath('CommonPrograms'); $bureau = [Environment]::GetFolderPath('CommonDesktopDirectory') }
    # Un dossier connu introuvable (profil itinerant abime...) : son chemin habituel.
    if (-not $menu) { $menu = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Microsoft\Windows\Start Menu\Programs' }
    if (-not $bureau) { $bureau = Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Desktop' }
    $r = @([pscustomobject]@{ Nom = 'menu Démarrer'; Chemin = (Join-Path $menu "$($Installation.Nom)\$($Installation.Nom).lnk"); Cible = (Join-Path $Installation.Dossier $Installation.Exe); Obligatoire = $true })
    if ($Installation.RaccourciBureau) { $r += [pscustomobject]@{ Nom = 'Bureau'; Chemin = (Join-Path $bureau "$($Installation.Nom).lnk"); Cible = (Join-Path $Installation.Dossier $Installation.Exe); Obligatoire = $true } }
    foreach ($m in @(@('Réparer', 'repair.bat'), @('Vérifier l''installation', 'verify_installation.bat'), @('Reprendre une installation interrompue', 'resume_installation.bat'),
                     @('Restaurer une sauvegarde', 'restore_backup.bat'), @('Collecter les journaux', 'collect_diagnostics.bat'), @('Réinitialiser mes réglages', 'reset_user_settings.bat'))) {
        $r += [pscustomobject]@{ Nom = "Maintenance : $($m[0])"; Chemin = (Join-Path $menu ("{0}\Maintenance\{1}.lnk" -f $Installation.Nom, $m[0])); Cible = (Join-Path $Installation.Dossier ("maintenance\{0}" -f $m[1])); Obligatoire = $false }
    }
    # 1.8.0 : « Desinstaller XPGAnalyser » (la recherche Windows le trouve) : le desinstallateur d'Inno Setup.
    $u = @(Get-ChildItem -LiteralPath $Installation.Dossier -Filter 'unins*.exe' -File -ErrorAction SilentlyContinue | Sort-Object Name) | Select-Object -Last 1
    if ($u) { $r += [pscustomobject]@{ Nom = 'menu Démarrer : Désinstaller'; Chemin = (Join-Path $menu ("{0}\Désinstaller {0}.lnk" -f $Installation.Nom)); Cible = $u.FullName; Obligatoire = $false } }
    return $r
}

function Test-XpgRaccourci {
    param($Raccourci)
    if (-not (Test-Path -LiteralPath $Raccourci.Chemin)) { return 'absent' }
    try {
        $sh = New-Object -ComObject WScript.Shell
        $l = $sh.CreateShortcut($Raccourci.Chemin)
        if ($l.TargetPath -ine $Raccourci.Cible) { return "vise $($l.TargetPath)" }
    } catch { return 'illisible' }
    return 'ok'
}

function New-XpgRaccourci {
    param($Raccourci, [string] $Dossier)
    $d = Split-Path -Parent $Raccourci.Chemin
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
    $sh = New-Object -ComObject WScript.Shell
    $l = $sh.CreateShortcut($Raccourci.Chemin)
    $l.TargetPath = $Raccourci.Cible
    $l.WorkingDirectory = $Dossier
    if ($Raccourci.Cible -like '*.exe') { $l.IconLocation = "$($Raccourci.Cible),0" }
    $l.Save()
}

function Test-XpgEcriture {
    param([string] $Dossier)
    try {
        if (-not (Test-Path -LiteralPath $Dossier)) { New-Item -ItemType Directory -Path $Dossier -Force | Out-Null }
        $f = Join-Path $Dossier ('.xpg-essai-' + [Guid]::NewGuid().ToString('N') + '.tmp')
        [System.IO.File]::WriteAllText($f, 'essai'); Remove-Item -LiteralPath $f -Force
        return $true
    } catch { return $false }
}

function Get-XpgProjetsRecents {
    # settings.txt : "recent.projects = a|b|c" (le separateur est |).
    $f = Join-Path (Get-XpgDossierReglages) 'settings.txt'
    if (-not (Test-Path -LiteralPath $f)) { return @() }
    foreach ($l in [System.IO.File]::ReadAllLines($f, [System.Text.Encoding]::UTF8)) {
        if ($l -match '^\s*recent\.projects\s*=\s*(.*)$') { return @($Matches[1].Split('|') | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
    }
    return @()
}

function Get-XpgDemarrageAuto {
    # La valeur Run que l'application ecrit pour le poste d'exploitation.
    try {
        $k = Get-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -ErrorAction Stop
        foreach ($p in $k.PSObject.Properties) { if ($p.Name -like 'XPG Poste*') { return [pscustomobject]@{ Nom = $p.Name; Valeur = [string]$p.Value } } }
    } catch { }
    return $null
}

# ---------------------------------------------------- sauvegarder, restaurer ----
function New-XpgSauvegarde {
    # Le programme (les fichiers du manifeste), les reglages (%APPDATA%\XpgAnalyzer), les cles
    # de registre ; jamais les projets (ils ne sont pas touches). Garde les N dernieres.
    param($Installation, [string] $Raison, [switch] $SansProgramme)
    $nom = "{0}_{1}" -f (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss'), ($Raison -replace '[^\w\.-]', '-')
    $d = Join-Path $Installation.Sauvegardes $nom
    New-Item -ItemType Directory -Path $d -Force | Out-Null
    $contenu = @()
    if (-not $SansProgramme -and (Test-Path -LiteralPath $Installation.Dossier)) {
        $n = 0
        foreach ($f in Get-ChildItem -LiteralPath $Installation.Dossier -Recurse -File -Force -ErrorAction SilentlyContinue) {
            $rel = $f.FullName.Substring($Installation.Dossier.Length + 1)
            if ($rel -like 'unins*') { continue }
            $c = Join-Path $d "programme\$rel"
            $cd = Split-Path -Parent $c
            if (-not (Test-Path -LiteralPath $cd)) { New-Item -ItemType Directory -Path $cd -Force | Out-Null }
            Copy-Item -LiteralPath $f.FullName -Destination $c -Force
            $n++
        }
        $contenu += "programme ($n fichiers)"
    }
    if (Test-Path -LiteralPath $Installation.Reglages) {
        $n = 0
        foreach ($f in Get-ChildItem -LiteralPath $Installation.Reglages -Recurse -File -Force -ErrorAction SilentlyContinue) {
            $rel = $f.FullName.Substring($Installation.Reglages.Length + 1)
            $c = Join-Path $d "reglages\$rel"
            $cd = Split-Path -Parent $c
            if (-not (Test-Path -LiteralPath $cd)) { New-Item -ItemType Directory -Path $cd -Force | Out-Null }
            Copy-Item -LiteralPath $f.FullName -Destination $c -Force
            $n++
        }
        $contenu += "réglages ($n fichiers)"
    }
    $reg = @()
    foreach ($k in @($Installation.CleProduit, $Installation.CleDesinstallation)) {
        if (-not $k) { continue }
        $natif = $k.Replace('HKCU:', 'HKCU').Replace('HKLM:', 'HKLM')
        $f = Join-Path $d ("registre-{0}.reg" -f $reg.Count)
        try {
            & reg.exe export $natif $f /y 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) { $reg += $f }
        } catch { Write-XpgDetail "reg.exe export $natif : $($_.Exception.Message)" }
    }
    if ($reg.Count) { $contenu += "registre ($($reg.Count) clé(s))" }
    $info = [pscustomobject]@{ date = (Get-Date).ToString('s'); raison = $Raison; version = $Installation.Version; programme = $Installation.Dossier; contenu = $contenu; projets = 'non (jamais touchés)' }
    Write-XpgTexte -Chemin (Join-Path $d 'sauvegarde.json') -Texte ($info | ConvertTo-Json -Depth 4)
    Write-XpgLog -Niveau OK -Message ("Sauvegarde : {0} ({1})" -f $d, ($contenu -join ', '))
    # Les plus anciennes au-dela de N (les sauvegardes « avant-restauration » comptent aussi).
    $garder = [int](Get-XpgConfig -Section 'Maintenance' -Cle 'sauvegardes_a_garder' -Defaut '3')
    $toutes = @(Get-ChildItem -LiteralPath $Installation.Sauvegardes -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending)
    if ($toutes.Count -gt $garder) {
        foreach ($v in $toutes[$garder..($toutes.Count - 1)]) { Remove-Item -LiteralPath $v.FullName -Recurse -Force -ErrorAction SilentlyContinue; Write-XpgDetail "Ancienne sauvegarde retirée : $($v.Name)" }
    }
    return $d
}

function Get-XpgSauvegardes {
    param($Installation)
    $r = @()
    foreach ($d in @(Get-ChildItem -LiteralPath $Installation.Sauvegardes -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending)) {
        $info = Read-XpgEtat (Join-Path $d.FullName 'sauvegarde.json')
        $taille = (Get-ChildItem -LiteralPath $d.FullName -Recurse -File -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum
        $r += [pscustomobject]@{ Nom = $d.Name; Chemin = $d.FullName; Raison = $(if ($info) { $info.raison } else { '?' }); Version = $(if ($info) { $info.version } else { '?' }); Taille = $taille }
    }
    return $r
}

function Restore-XpgSauvegarde {
    param($Installation, [string] $Chemin, [switch] $Programme, [switch] $Reglages, [switch] $Registre)
    $n = 0
    if ($Programme -and (Test-Path -LiteralPath (Join-Path $Chemin 'programme'))) {
        $src = Join-Path $Chemin 'programme'
        foreach ($f in Get-ChildItem -LiteralPath $src -Recurse -File -Force) {
            $c = Join-Path $Installation.Dossier $f.FullName.Substring($src.Length + 1)
            $cd = Split-Path -Parent $c
            if (-not (Test-Path -LiteralPath $cd)) { New-Item -ItemType Directory -Path $cd -Force | Out-Null }
            Copy-Item -LiteralPath $f.FullName -Destination $c -Force; $n++
        }
        Write-XpgLog -Niveau OK -Message "Programme remis : $n fichier(s)."
    }
    if ($Reglages -and (Test-Path -LiteralPath (Join-Path $Chemin 'reglages'))) {
        $src = Join-Path $Chemin 'reglages'; $m = 0
        foreach ($f in Get-ChildItem -LiteralPath $src -Recurse -File -Force) {
            $c = Join-Path $Installation.Reglages $f.FullName.Substring($src.Length + 1)
            $cd = Split-Path -Parent $c
            if (-not (Test-Path -LiteralPath $cd)) { New-Item -ItemType Directory -Path $cd -Force | Out-Null }
            Copy-Item -LiteralPath $f.FullName -Destination $c -Force; $m++
        }
        Write-XpgLog -Niveau OK -Message "Réglages remis : $m fichier(s)."
    }
    if ($Registre) {
        foreach ($f in Get-ChildItem -LiteralPath $Chemin -Filter 'registre-*.reg' -File -ErrorAction SilentlyContinue) {
            try { & reg.exe import $f.FullName 2>$null | Out-Null } catch { }
            if ($LASTEXITCODE -eq 0) { Write-XpgLog -Niveau OK -Message "Registre remis : $($f.Name)" } else { Write-XpgLog -Niveau ATTENTION -Message "Registre non remis ($($f.Name)) : droits d'administrateur ?" }
        }
    }
}

# --------------------------------------------------------------- l'etat ----
function Get-XpgFichierEtat { param($Installation) return (Join-Path $Installation.Etat 'Etat\installation.json') }

# ---------------------------------------------------------- la migration ----
function Get-XpgEmpreintesLivrees {
    # Les SHA-256 des fichiers de libs\ et resources\ livres par les lots precedents : un
    # ancien fichier qui en a une n'a pas ete modifie par l'utilisateur.
    $f = Join-Path $PSScriptRoot 'empreintes-livrees.txt'
    $h = @{}
    if (Test-Path -LiteralPath $f) { foreach ($l in [System.IO.File]::ReadAllLines($f)) { if ($l -match '^([0-9a-f]{64})\b') { $h[$Matches[1]] = $true } } }
    return $h
}

function Copy-XpgFusion {
    # Fusionne l'arbre $De dans $Vers SANS RIEN ECRASER NI SUPPRIMER :
    #  - absent de $Vers : copie ;
    #  - identique : rien ;
    #  - different, et l'ancien est un fichier livre non modifie (empreinte connue) : on garde
    #    le nouveau (celui de $Vers) ;
    #  - different, et l'ancien est A TOI : ta version prend la place, la version livree
    #    est rangee a cote (<nom>.livre-<version>) ; rien n'est perdu.
    param([string] $De, [string] $Vers, [hashtable] $Livrees, [string] $Version, [System.Collections.ArrayList] $Rapport)
    $b = [pscustomobject]@{ Copies = 0; Identiques = 0; Anciens = 0; Tiens = 0; Erreurs = 0 }
    if (-not (Test-Path -LiteralPath $De)) { return $b }
    $base = (Get-Item -LiteralPath $De).FullName.TrimEnd('\')
    foreach ($f in Get-ChildItem -LiteralPath $De -Recurse -File -Force -ErrorAction SilentlyContinue) {
        $rel = $f.FullName.Substring($base.Length + 1)
        $c = Join-Path $Vers $rel
        try {
            if (-not (Test-Path -LiteralPath $c)) {
                $cd = Split-Path -Parent $c
                if (-not (Test-Path -LiteralPath $cd)) { New-Item -ItemType Directory -Path $cd -Force | Out-Null }
                Copy-Item -LiteralPath $f.FullName -Destination $c; $b.Copies++
                continue
            }
            $ha = Get-XpgSha256 $f.FullName
            $hn = Get-XpgSha256 $c
            if ($ha -eq $hn) { $b.Identiques++; continue }
            if ($Livrees.ContainsKey($ha)) { $b.Anciens++; if ($Rapport) { [void]$Rapport.Add("  $rel : ancienne version livrée, remplacée par celle de $Version") }; continue }
            $livre = "$c.livre-$Version"
            if (-not (Test-Path -LiteralPath $livre)) { Move-Item -LiteralPath $c -Destination $livre }
            Copy-Item -LiteralPath $f.FullName -Destination $c -Force
            $b.Tiens++
            if ($Rapport) { [void]$Rapport.Add("  $rel : TA version gardée ; celle de $Version rangée à côté ($([System.IO.Path]::GetFileName($livre)))") }
        } catch {
            $b.Erreurs++
            if ($Rapport) { [void]$Rapport.Add("  $rel : ERREUR $($_.Exception.Message)") }
        }
    }
    return $b
}

function Merge-XpgIndexBibliotheque {
    # libs\index.txt : "kind ; category ; name ; version ; author ; comment". Les lignes livrees
    # d'abord (l'en-tete aussi) ; un bloc livre que TU as remplace (sa ligne n'est pas signee
    # XpgAnalyzer : publie depuis l'application, ajoute a l'installation) garde TA ligne ; puis
    # tes blocs a toi.
    param([string] $Livre, [string] $Tien)
    $utf8 = New-Object System.Text.UTF8Encoding $false
    $out = New-Object System.Collections.ArrayList
    $tiens = [ordered]@{}
    if (Test-Path -LiteralPath $Tien) {
        foreach ($l in [System.IO.File]::ReadAllLines($Tien, $utf8)) {
            if (-not $l.Trim() -or $l.TrimStart().StartsWith('#')) { continue }
            $f = $l -split ';'
            if ($f.Count -lt 4) { continue }
            $tiens[$f[2].Trim().ToLowerInvariant()] = $l
        }
    }
    $livres = @{}
    $nl = 0; $nt = 0
    foreach ($l in [System.IO.File]::ReadAllLines($Livre, $utf8)) {
        if ($l.TrimStart().StartsWith('#') -or -not $l.Trim()) { [void]$out.Add($l); continue }
        $f = $l -split ';'
        if ($f.Count -lt 4) { [void]$out.Add($l); continue }
        $n = $f[2].Trim().ToLowerInvariant()
        $livres[$n] = $true
        $t = $tiens[$n]
        $auteur = ''
        if ($t) { $ft = $t -split ';'; if ($ft.Count -ge 5) { $auteur = $ft[4].Trim() } }
        if ($t -and $auteur -and $auteur -ine 'XpgAnalyzer') { [void]$out.Add($t); $nt++ } else { [void]$out.Add($l); $nl++ }
    }
    foreach ($n in $tiens.Keys) {
        if ($livres.ContainsKey($n)) { continue }
        [void]$out.Add($tiens[$n]); $nt++
    }
    return [pscustomobject]@{ Lignes = $out; Livres = $nl; Tiens = $nt }
}

function Update-XpgDonneesLivrees {
    # Les fichiers LIVRES (resources\, libs\ du dossier du programme) vers les donnees de
    # l'utilisateur : absent -> copie ; identique -> rien ; l'ancien est une version livree
    # non modifiee (empreinte connue) -> remplace (garde en .avant-<version>) ; l'ancien est
    # A TOI -> garde, la nouvelle version rangee a cote (<nom>.livre-<version>).
    param([string] $De, [string] $Vers, [hashtable] $Livrees, [string] $Version, [System.Collections.ArrayList] $Rapport)
    $b = [pscustomobject]@{ Copies = 0; Identiques = 0; MisAJour = 0; Tiens = 0; Erreurs = 0 }
    if (-not (Test-Path -LiteralPath $De)) { return $b }
    $base = (Get-Item -LiteralPath $De).FullName.TrimEnd('\')
    foreach ($f in Get-ChildItem -LiteralPath $De -Recurse -File -Force -ErrorAction SilentlyContinue) {
        $rel = $f.FullName.Substring($base.Length + 1)
        $c = Join-Path $Vers $rel
        try {
            if (-not (Test-Path -LiteralPath $c)) {
                $cd = Split-Path -Parent $c
                if (-not (Test-Path -LiteralPath $cd)) { New-Item -ItemType Directory -Path $cd -Force | Out-Null }
                Copy-Item -LiteralPath $f.FullName -Destination $c; $b.Copies++; continue
            }
            $hn = Get-XpgSha256 $f.FullName
            $hu = Get-XpgSha256 $c
            if ($hn -eq $hu) { $b.Identiques++; continue }
            # 1.8.0 : l'index de la bibliotheque se FUSIONNE - les blocs livres (nouveaux, mis a
            # jour) y entrent, les tiens (publies depuis l'application, ajoutes a l'installation)
            # restent. Sans cela, un index modifie gardait « ta version » et cachait les blocs
            # nouveaux de la version installee.
            if ($rel -ieq 'index.txt' -and (Split-Path -Leaf $Vers) -ine 'resources') {
                $m = Merge-XpgIndexBibliotheque -Livre $f.FullName -Tien $c
                Copy-Item -LiteralPath $c -Destination "$c.avant-$Version" -Force
                [System.IO.File]::WriteAllLines($c, [string[]]$m.Lignes, (New-Object System.Text.UTF8Encoding $false))
                $b.MisAJour++
                if ($Rapport) { [void]$Rapport.Add("  $rel : fusionné ($($m.Livres) blocs livrés, $($m.Tiens) à toi gardés ; l'ancien en .avant-$Version)") }
                continue
            }
            if ($Livrees.ContainsKey($hu)) {
                Copy-Item -LiteralPath $c -Destination "$c.avant-$Version" -Force
                Copy-Item -LiteralPath $f.FullName -Destination $c -Force
                $b.MisAJour++
                if ($Rapport) { [void]$Rapport.Add("  $rel : mis à jour (l'ancienne version livrée gardée en .avant-$Version)") }
            } else {
                Copy-Item -LiteralPath $f.FullName -Destination "$c.livre-$Version" -Force
                $b.Tiens++
                if ($Rapport) { [void]$Rapport.Add("  $rel : TA version gardée ; celle de $Version rangée à côté (.livre-$Version)") }
            }
        } catch {
            $b.Erreurs++
            if ($Rapport) { [void]$Rapport.Add("  $rel : ERREUR $($_.Exception.Message)") }
        }
    }
    return $b
}

function Add-XpgBlocsAjoutes {
    # 1.8.0 : les blocs choisis dans l'assistant (page « Ta bibliotheque ») vont dans la
    # bibliotheque de l'utilisateur, et index.txt les connait. $Liste : un fichier UTF-8,
    # une ligne "chemin<TAB>categorie" par bloc. Le nom et la version sont lus dans le fichier
    # (name = ..., version = ..., #! summary = ...). Meme nom qu'un bloc deja la : le tien le
    # remplace, dans SA categorie ; l'autre est range a cote (.livre-<version>).
    param($Installation, [string] $Liste, [string] $Version, [System.Collections.ArrayList] $Rapport)
    $b = [pscustomobject]@{ Ajoutes = 0; Remplaces = 0; Erreurs = 0 }
    $lib = $Installation.Bibliotheque
    if (-not $lib) { return $b }
    New-Item -ItemType Directory -Path $lib -Force | Out-Null
    $index = Join-Path $lib 'index.txt'
    $utf8 = New-Object System.Text.UTF8Encoding $false
    $lignes = New-Object System.Collections.ArrayList
    if (Test-Path -LiteralPath $index) { foreach ($l in [System.IO.File]::ReadAllLines($index, $utf8)) { [void]$lignes.Add($l) } }
    foreach ($entree in [System.IO.File]::ReadAllLines($Liste, $utf8)) {
        if (-not $entree.Trim()) { continue }
        $morceaux = $entree -split "`t"
        $src = $morceaux[0].Trim()
        $cat = if ($morceaux.Count -gt 1 -and $morceaux[1].Trim()) { $morceaux[1].Trim() } else { 'Perso' }
        try {
            if (-not (Test-Path -LiteralPath $src)) { throw "introuvable" }
            $ext = [System.IO.Path]::GetExtension($src).TrimStart('.').ToLowerInvariant()
            if ($ext -notin @('dfb', 'ddt', 'mac')) { throw "pas un bloc (.dfb, .ddt, .mac)" }
            $tete = @(Get-Content -LiteralPath $src -TotalCount 60 -Encoding UTF8 -ErrorAction SilentlyContinue)
            $lire = { param($cle) $m = $tete | Where-Object { $_ -match "^\s*$cle\s*=\s*(.+?)\s*$" } | Select-Object -First 1; if ($m -and $m -match "^\s*$cle\s*=\s*(.+?)\s*$") { $Matches[1] } else { '' } }
            $nom = & $lire 'name'
            if (-not $nom) { $nom = [System.IO.Path]::GetFileNameWithoutExtension($src) }
            $ver = & $lire 'version'
            if (-not $ver) { $ver = '1.00' }
            $resume = & $lire '#!\s*summary'
            $resume = $resume -replace ';', ','
            # Un bloc de ce nom deja connu : le tien va a sa place (sa categorie).
            $deja = $null
            foreach ($l in $lignes) {
                if ($l.TrimStart().StartsWith('#')) { continue }
                $f = $l -split ';'
                if ($f.Count -ge 4 -and $f[2].Trim() -ieq $nom) { $deja = $l; $cat = $f[1].Trim(); break }
            }
            $dossier = Join-Path $lib $cat
            New-Item -ItemType Directory -Path $dossier -Force | Out-Null
            $cible = Join-Path $dossier ("{0}.{1}" -f $nom, $ext)
            if ((Test-Path -LiteralPath $cible) -and ((Get-XpgSha256 $cible) -ne (Get-XpgSha256 $src))) {
                Copy-Item -LiteralPath $cible -Destination "$cible.livre-$Version" -Force
                $b.Remplaces++
                if ($Rapport) { [void]$Rapport.Add("  $cat\$nom.$ext : le tien remplace celui d'avant, rangé à côté (.livre-$Version)") }
            } elseif ($Rapport) { [void]$Rapport.Add("  $cat\$nom.$ext : ajouté (depuis $src)") }
            Copy-Item -LiteralPath $src -Destination $cible -Force
            if ($deja) { [void]$lignes.Remove($deja) }
            [void]$lignes.Add(("{0} ; {1} ; {2} ; {3} ; {4} ; {5}" -f $ext, $cat, $nom, $ver, $env:USERNAME, $resume))
            $b.Ajoutes++
        } catch {
            $b.Erreurs++
            if ($Rapport) { [void]$Rapport.Add("  $src : ERREUR $($_.Exception.Message)") }
        }
    }
    if ($b.Ajoutes) { [System.IO.File]::WriteAllLines($index, [string[]]$lignes, $utf8) }
    return $b
}

function Copy-XpgProjets {
    # Chaque projet (un dossier) de $De va dans $Vers ; un projet du meme nom deja la et
    # different : copie sous "<nom> (repris de <origine>)". Rien n'est ecrase.
    param([string] $De, [string] $Vers, [string] $Origine, [System.Collections.ArrayList] $Rapport, [hashtable] $Correspondances)
    $b = [pscustomobject]@{ Projets = 0; Deja = 0; Renommes = 0; Fichiers = 0 }
    if (-not (Test-Path -LiteralPath $De)) { return $b }
    New-Item -ItemType Directory -Path $Vers -Force | Out-Null
    foreach ($p in Get-ChildItem -LiteralPath $De -Directory -Force -ErrorAction SilentlyContinue) {
        $cible = Join-Path $Vers $p.Name
        if (Test-Path -LiteralPath $cible) {
            $a = @(Get-ChildItem -LiteralPath $p.FullName -Recurse -File -Force | ForEach-Object { "$($_.FullName.Substring($p.FullName.Length)):$($_.Length)" }) -join '|'
            $n = @(Get-ChildItem -LiteralPath $cible -Recurse -File -Force | ForEach-Object { "$($_.FullName.Substring($cible.Length)):$($_.Length)" }) -join '|'
            if ($a -eq $n) { $b.Deja++; if ($Correspondances) { $Correspondances[$p.FullName] = $cible }; continue }
            $cible = Join-Path $Vers ("{0} (repris de {1})" -f $p.Name, $Origine)
            $k = 2
            while (Test-Path -LiteralPath $cible) { $cible = Join-Path $Vers ("{0} (repris de {1} {2})" -f $p.Name, $Origine, $k); $k++ }
            $b.Renommes++
            if ($Rapport) { [void]$Rapport.Add("  projet $($p.Name) : un autre du même nom existe déjà ; repris sous « $([System.IO.Path]::GetFileName($cible)) »") }
        }
        Copy-Item -LiteralPath $p.FullName -Destination $cible -Recurse
        $b.Projets++
        $b.Fichiers += @(Get-ChildItem -LiteralPath $cible -Recurse -File -Force).Count
        if ($Correspondances) { $Correspondances[$p.FullName] = $cible }
    }
    # Les fichiers poses a la racine de projets\ (des exports) : copies sans ecraser.
    foreach ($f in Get-ChildItem -LiteralPath $De -File -Force -ErrorAction SilentlyContinue) {
        $c = Join-Path $Vers $f.Name
        if (-not (Test-Path -LiteralPath $c)) { Copy-Item -LiteralPath $f.FullName -Destination $c; $b.Fichiers++ }
    }
    return $b
}

function Get-XpgAnciennesDonnees {
    # Les anciennes versions a reprendre : l'installation du lot 8 (NSIS), les dossiers de
    # config.ini ([Maintenance] anciens_dossiers) et ceux donnes. Chacune : ses projets, sa
    # bibliotheque, ses ressources, ses captures, leur taille.
    param([string[]] $Dossiers = @(), [string] $Exclure = '')
    $liste = @()
    try { $k = Get-ItemProperty -Path 'HKCU:\Software\XpgAnalyzer' -ErrorAction Stop; if ($k.InstallDir) { $liste += [pscustomobject]@{ Chemin = $k.InstallDir; Origine = 'lot 8'; Lot8 = $true } } } catch { }
    # maintenance\config.ini (ecrit par package.bat) la range sous [Maintenance] ; outils\config.ini,
    # sous [Installateur].
    $cfg = Get-XpgConfig -Section 'Maintenance' -Cle 'anciens_dossiers' -Defaut ''
    if (-not $cfg) { $cfg = Get-XpgConfig -Section 'Installateur' -Cle 'anciens_dossiers' -Defaut '' }
    foreach ($d in (($cfg -split ';') + $Dossiers)) {
        if (-not $d -or -not $d.Trim()) { continue }
        # Les jetons de config.ini ({pf32}, {LocalAppData}, {Documents}..., sans tenir compte des
        # majuscules) et les %VARIABLES%.
        $e = (Expand-XpgChemin -Valeur $d.Trim()).TrimEnd('\')
        if ($e -match '\{') { continue }
        if (-not ($liste | Where-Object { $_.Chemin -ieq $e })) { $liste += [pscustomobject]@{ Chemin = $e; Origine = (Split-Path -Leaf $e); Lot8 = $false } }
    }
    $r = @()
    foreach ($s in $liste) {
        if (-not (Test-Path -LiteralPath $s.Chemin)) { continue }
        if ($Exclure -and ($s.Chemin -ieq $Exclure)) { continue }
        $parties = @()
        foreach ($sous in @('projets', 'build\Debug\projets', 'build\Release\projets', 'libs', 'resources', 'captures')) {
            $p = Join-Path $s.Chemin $sous
            if (Test-Path -LiteralPath $p) {
                $fs = @(Get-ChildItem -LiteralPath $p -Recurse -File -Force -ErrorAction SilentlyContinue)
                if ($fs.Count) { $parties += [pscustomobject]@{ Sous = $sous; Fichiers = $fs.Count; Taille = ($fs | Measure-Object -Property Length -Sum).Sum } }
            }
        }
        if ($parties.Count) { $r += [pscustomobject]@{ Chemin = $s.Chemin; Origine = $s.Origine; Lot8 = $s.Lot8; Parties = $parties } }
    }
    return $r
}

function Invoke-XpgMigration {
    # Reprend les anciennes donnees dans le dossier des donnees (voir Copy-XpgFusion et
    # Copy-XpgProjets) ; reecrit les projets recents de settings.txt (sauvegarde avant) et
    # le demarrage du poste d'exploitation s'il visait un ancien exe. Les anciens dossiers
    # ne sont jamais modifies.
    param($Installation, [object[]] $Sources, [System.Collections.ArrayList] $Rapport)
    $livrees = Get-XpgEmpreintesLivrees
    $corr = @{}
    $bilan = [pscustomobject]@{ Projets = 0; Fichiers = 0; Tiens = 0; Erreurs = 0 }
    foreach ($s in $Sources) {
        [void]$Rapport.Add("Depuis $($s.Chemin) ($($s.Origine)) :")
        foreach ($sous in @('projets', 'build\Debug\projets', 'build\Release\projets')) {
            $b = Copy-XpgProjets -De (Join-Path $s.Chemin $sous) -Vers $Installation.Projets -Origine $s.Origine -Rapport $Rapport -Correspondances $corr
            if ($b.Projets -or $b.Deja) { [void]$Rapport.Add(("  {0} : {1} projet(s) repris ({2} fichiers), {3} déjà là" -f $sous, $b.Projets, $b.Fichiers, $b.Deja)) }
            $bilan.Projets += $b.Projets; $bilan.Fichiers += $b.Fichiers
        }
        foreach ($p in @(@('libs', $Installation.Bibliotheque), @('resources', (Join-Path $Installation.Donnees 'resources')), @('captures', $Installation.Captures))) {
            $b = Copy-XpgFusion -De (Join-Path $s.Chemin $p[0]) -Vers $p[1] -Livrees $livrees -Version $Installation.Version -Rapport $Rapport
            if ($b.Copies + $b.Identiques + $b.Anciens + $b.Tiens) {
                [void]$Rapport.Add(("  {0} : {1} copié(s), {2} identique(s), {3} ancienne(s) version(s) livrée(s), {4} à toi gardé(s)" -f $p[0], $b.Copies, $b.Identiques, $b.Anciens, $b.Tiens))
            }
            $bilan.Fichiers += $b.Copies; $bilan.Tiens += $b.Tiens; $bilan.Erreurs += $b.Erreurs
        }
    }
    # Les projets recents : l'ancien chemin -> le nouveau (settings.txt sauvegarde avant).
    $reglages = Join-Path $Installation.Reglages 'settings.txt'
    if ($corr.Count -and (Test-Path -LiteralPath $reglages)) {
        # Seulement si settings.txt est de l'UTF-8 valide : une version sans le manifeste UTF-8
        # a pu ecrire des chemins accentues en ANSI ; les relire en UTF-8 les abimerait.
        $texte = $null
        try { $texte = (New-Object System.Text.UTF8Encoding($false, $true)).GetString([System.IO.File]::ReadAllBytes($reglages)) } catch { }
        if ($null -eq $texte) { [void]$Rapport.Add("Réglages : settings.txt n'est pas en UTF-8 : les projets récents ne sont pas réécrits (ils visent les anciens dossiers, toujours là)."); $corr = @{} ; $texte = '' }
        # (Pas StartsWith : en comparaison culturelle, le BOM est ignorable et tout texte « commence » par lui.)
        if ($texte.Length -gt 0 -and $texte[0] -eq [char]0xFEFF) { $texte = $texte.Substring(1) }
        $nouveau = $texte; $n = 0
        foreach ($k in $corr.Keys) { if ($nouveau.IndexOf($k, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $nouveau = [regex]::Replace($nouveau, [regex]::Escape($k), ($corr[$k] -replace '\$', '$$$$'), 'IgnoreCase'); $n++ } }
        if ($n) {
            Copy-Item -LiteralPath $reglages -Destination "$reglages.avant-$($Installation.Version)" -Force
            [System.IO.File]::WriteAllText($reglages, $nouveau, (New-Object System.Text.UTF8Encoding $false))
            [void]$Rapport.Add("Réglages : $n chemin(s) de projet réécrit(s) vers le nouveau dossier (l'ancien settings.txt gardé en settings.txt.avant-$($Installation.Version)).")
        }
    }
    # Le poste d'exploitation lance au demarrage : vers le nouvel exe.
    $run = Get-XpgDemarrageAuto
    if ($run) {
        foreach ($s in $Sources) {
            if ($run.Valeur.IndexOf($s.Chemin, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $exe = Join-Path $Installation.Dossier $Installation.Exe
                $v = [regex]::Replace($run.Valeur, '^"[^"]*"', ('"' + $exe.Replace('$', '$$') + '"'))
                foreach ($k in $corr.Keys) { $v = [regex]::Replace($v, [regex]::Escape($k), ($corr[$k] -replace '\$', '$$$$'), 'IgnoreCase') }
                Set-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name $run.Nom -Value $v
                [void]$Rapport.Add("Démarrage du poste d'exploitation : réécrit vers $exe (avant : $($run.Valeur)).")
                break
            }
        }
    }
    return $bilan
}

# ------------------------------------------------------------ le diagnostic ----
function New-XpgProbleme {
    param([string] $Categorie, [string] $Texte, [string] $Gravite = 'erreur', [string] $Reparation = '', $Donnee = $null)
    return [pscustomobject]@{ Categorie = $Categorie; Texte = $Texte; Gravite = $Gravite; Reparation = $Reparation; Donnee = $Donnee }
}

function Invoke-XpgDiagnostic {
    # Les verifications de repair.bat (etapes 1 a 15) et de verify_installation.bat. Rien
    # n'est modifie. Rend : Lignes (ce qui a ete vu) et Problemes (Categorie, Texte, Gravite,
    # Reparation : fichiers | dependances | raccourcis | registre | parametres | reprise | reinstaller).
    param($Installation, [switch] $Rapide)
    $i = $Installation
    $lignes = New-Object System.Collections.ArrayList
    $pb = New-Object System.Collections.ArrayList
    function Add-XpgLigne([string] $t, [string] $n = 'INFO') { [void]$lignes.Add($t); Write-XpgLog -Niveau $n -Message $t }

    # 1-2. l'installation, sa version
    Add-XpgLigne ("Installation : {0} {1} ({2}) dans {3}" -f $i.Nom, $i.Version, $(if ($i.Portee -eq 'tous') { 'tous les comptes' } else { 'pour ce compte' }), $i.Dossier)
    if ($i.Date) { Add-XpgLigne ("Installée le : {0}" -f $i.Date) }
    # 3. en cours d'execution ?
    $p = Get-XpgProcessus $i
    if ($p.Count) { Add-XpgLigne ("Application en cours d'exécution : oui ({0})" -f $p.Count) 'ATTENTION'; [void]$pb.Add((New-XpgProbleme 'application' "$($i.Nom) est ouvert" 'info')) } else { Add-XpgLigne 'Application en cours d''exécution : non' 'OK' }
    # 5-7. le manifeste, les fichiers, leurs tailles et empreintes
    if (-not $i.Manifeste) { Add-XpgLigne 'Manifeste : absent ou illisible' 'ERREUR'; [void]$pb.Add((New-XpgProbleme 'manifeste' 'manifeste.json absent ou illisible' 'erreur' 'reinstaller')) }
    else {
        Add-XpgLigne ("Manifeste : {0} {1}, {2} fichiers (chaîne {3})" -f $i.Manifeste.produit, $i.Manifeste.version, @($i.Manifeste.fichiers).Count, $i.Manifeste.chaine)
        $ecarts = Test-XpgFichiers -Installation $i -SansEmpreinte:$Rapide
        if ($ecarts.Count) {
            foreach ($e in $ecarts) { Add-XpgLigne ("  {0} : {1}" -f $e.Chemin, $e.Etat) 'ERREUR'; [void]$pb.Add((New-XpgProbleme 'fichiers' "$($e.Chemin) : $($e.Etat)" 'erreur' 'fichiers' $e)) }
        } else { Add-XpgLigne ("Fichiers obligatoires : {0} présents, tailles{1} conformes" -f @($i.Manifeste.fichiers).Count, $(if ($Rapide) { '' } else { ' et SHA-256' })) 'OK' }
        # 8. les DLL necessaires
        foreach ($d in @($i.Manifeste.dependances)) {
            if (-not $d) { continue }
            $ici = Test-Path -LiteralPath (Join-Path $i.Dossier $d)
            $sys = $false
            if ([Environment]::SystemDirectory) { $sys = Test-Path -LiteralPath (Join-Path ([Environment]::SystemDirectory) $d) }
            if (-not $ici -and -not $sys) { Add-XpgLigne "DLL nécessaire absente : $d (l'application ne démarrera pas)" 'ERREUR'; [void]$pb.Add((New-XpgProbleme 'dependances' "$d absente" 'erreur' 'dependances' $d)) }
        }
        if (-not ($pb | Where-Object { $_.Categorie -eq 'dependances' })) { Add-XpgLigne ("DLL nécessaires : {0}" -f (@($i.Manifeste.dependances) -join ', ')) 'OK' }
        # 9. les runtimes
        if ($i.Manifeste.chaine -eq 'msvc') {
            $loc = @($i.Manifeste.runtime_local | Where-Object { $_ -and -not (Test-Path -LiteralPath (Join-Path $i.Dossier $_)) })
            if ($loc.Count) { Add-XpgLigne ("Runtime Visual C++ incomplet : {0}" -f ($loc -join ', ')) 'ERREUR'; [void]$pb.Add((New-XpgProbleme 'dependances' 'runtime Visual C++ incomplet' 'erreur' 'dependances')) }
            else { Add-XpgLigne ("Runtime Visual C++ : local, {0} DLL" -f @($i.Manifeste.runtime_local).Count) 'OK' }
        } else { Add-XpgLigne 'Runtime C++ : lié dans l''exe (MinGW), rien à vérifier' 'OK' }
    }
    # 10. le registre
    try {
        $k = Get-ItemProperty -Path $i.CleProduit -ErrorAction Stop
        if ($k.InstallDir -and ($k.InstallDir.TrimEnd('\') -ieq $i.Dossier)) { Add-XpgLigne "Registre ($($i.CleProduit)) : OK" 'OK' }
        else { Add-XpgLigne "Registre : InstallDir vaut « $($k.InstallDir) »" 'ATTENTION'; [void]$pb.Add((New-XpgProbleme 'registre' 'InstallDir incorrect' 'attention' 'registre')) }
    } catch { Add-XpgLigne "Registre : clé $($i.CleProduit) absente" 'ATTENTION'; [void]$pb.Add((New-XpgProbleme 'registre' "clé $($i.CleProduit) absente" 'attention' 'registre')) }
    if ($i.CleDesinstallation) {
        try {
            $u = Get-ItemProperty -Path $i.CleDesinstallation -ErrorAction Stop
            $unins = ''
            if ($u.UninstallString) { $unins = ($u.UninstallString -replace '^"([^"]+)".*$', '$1') }
            if ($unins -and (Test-Path -LiteralPath $unins)) { Add-XpgLigne ("« Applications installées » : {0} {1}" -f $u.DisplayName, $u.DisplayVersion) 'OK' }
            else { Add-XpgLigne '« Applications installées » : le désinstallateur est introuvable' 'ERREUR'; [void]$pb.Add((New-XpgProbleme 'registre' 'désinstallateur introuvable' 'erreur' 'reinstaller')) }
        } catch { Add-XpgLigne '« Applications installées » : entrée absente' 'ERREUR'; [void]$pb.Add((New-XpgProbleme 'registre' 'entrée « Applications installées » absente' 'erreur' 'reinstaller')) }
    }
    # 11. les raccourcis
    $manquants = 0
    foreach ($r in (Get-XpgRaccourcis $i)) {
        $e = Test-XpgRaccourci $r
        if ($e -ne 'ok') {
            $manquants++
            $g = 'attention'; if ($r.Obligatoire) { $g = 'erreur' }
            [void]$pb.Add((New-XpgProbleme 'raccourcis' "$($r.Nom) : $e" $g 'raccourcis' $r))
        }
    }
    if ($manquants) { Add-XpgLigne "Raccourcis : $manquants absent(s) ou faux" 'ATTENTION' } else { Add-XpgLigne 'Raccourcis : OK' 'OK' }
    # 12. les droits des dossiers
    $lecture = Test-Path -LiteralPath (Join-Path $i.Dossier $i.Exe)
    $ecriture = Test-XpgEcriture $i.Donnees
    Add-XpgLigne ("Droits : programme {0} ; données {1}" -f $(if ($lecture) { 'lecture OK' } else { 'ILLISIBLE' }), $(if ($ecriture) { 'écriture OK' } else { 'ÉCRITURE REFUSÉE' })) $(if ($lecture -and $ecriture) { 'OK' } else { 'ERREUR' })
    if (-not $ecriture) { [void]$pb.Add((New-XpgProbleme 'parametres' "écriture impossible dans $($i.Donnees)" 'erreur' 'parametres')) }
    # 13. les fichiers de configuration
    $st = Join-Path $i.Reglages 'settings.txt'
    if (Test-Path -LiteralPath $st) {
        try { $n = @([System.IO.File]::ReadAllLines($st) | Where-Object { $_ -match '=' }).Count; Add-XpgLigne "settings.txt : lisible, $n clés" 'OK' } catch { Add-XpgLigne 'settings.txt : illisible' 'ERREUR'; [void]$pb.Add((New-XpgProbleme 'parametres' 'settings.txt illisible' 'erreur' 'parametres')) }
    } else { Add-XpgLigne 'settings.txt : absent (normal avant le premier lancement)' }
    if (Test-Path -LiteralPath $i.IniUtilisateur) {
        try { $null = Read-XpgIni $i.IniUtilisateur; Add-XpgLigne 'XPGAnalyser.ini : lisible' 'OK' } catch { Add-XpgLigne 'XPGAnalyser.ini : illisible' 'ERREUR'; [void]$pb.Add((New-XpgProbleme 'parametres' 'XPGAnalyser.ini illisible' 'erreur' 'parametres')) }
    }
    if (-not $i.Ini) { Add-XpgLigne 'installation.ini : illisible' 'ERREUR'; [void]$pb.Add((New-XpgProbleme 'parametres' 'installation.ini illisible' 'erreur' 'reinstaller')) }
    # 14. la coherence des chemins enregistres
    foreach ($d in @(@('données', $i.Donnees), @('projets', $i.Projets), @('bibliothèque', $i.Bibliotheque), @('captures', $i.Captures))) {
        if (-not (Test-Path -LiteralPath $d[1])) {
            $g = 'attention'; if ($d[0] -eq 'données') { $g = 'erreur' }
            if ($d[0] -ne 'captures') { Add-XpgLigne "Dossier des $($d[0]) absent : $($d[1])" 'ATTENTION'; [void]$pb.Add((New-XpgProbleme 'parametres' "dossier des $($d[0]) absent : $($d[1])" $g 'parametres' $d[1])) }
        }
    }
    $absents = @(Get-XpgProjetsRecents | Where-Object { -not (Test-Path -LiteralPath $_) })
    if ($absents.Count) { Add-XpgLigne ("Projets récents introuvables : {0} ({1})" -f $absents.Count, ($absents -join ' ; ')) 'ATTENTION'; [void]$pb.Add((New-XpgProbleme 'parametres' "$($absents.Count) projet(s) récent(s) introuvable(s) (une clé USB absente ?)" 'info')) }
    else { Add-XpgLigne 'Chemins enregistrés : cohérents' 'OK' }
    $run = Get-XpgDemarrageAuto
    if ($run) {
        $exeRun = ($run.Valeur -replace '^"([^"]+)".*$', '$1')
        if (-not (Test-Path -LiteralPath $exeRun)) { Add-XpgLigne "Démarrage du poste d'exploitation : vise un exe absent ($exeRun)" 'ATTENTION'; [void]$pb.Add((New-XpgProbleme 'parametres' 'démarrage automatique vers un exe absent' 'attention' 'parametres' $run)) }
    }
    # 15. une installation interrompue ?
    $etat = Read-XpgEtat (Get-XpgFichierEtat $i)
    if ($etat -and $etat.statut -ne 'terminee') { Add-XpgLigne ("Installation interrompue : oui (étape « {0} », {1})" -f $etat.etape, $etat.statut) 'ATTENTION'; [void]$pb.Add((New-XpgProbleme 'reprise' "installation interrompue à l'étape $($etat.etape)" 'erreur' 'reprise' $etat)) }
    else { Add-XpgLigne 'Installation interrompue : non' 'OK' }
    # La reserve de reparation
    $res = Join-Path $i.Reserve $i.Version
    if (Test-Path -LiteralPath (Join-Path $res 'manifeste.json')) { Add-XpgLigne "Réserve de réparation : $res" 'OK' } else { Add-XpgLigne "Réserve de réparation : absente ($res)" 'ATTENTION' }
    return [pscustomobject]@{ Lignes = $lignes; Problemes = $pb }
}

Export-ModuleMember -Function *

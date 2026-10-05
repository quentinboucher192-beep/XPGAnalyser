#Requires -Version 5.1
# =============================================================================
#  XpgCommun.psm1 - ce que tous les scripts de XPGAnalyser partagent
# -----------------------------------------------------------------------------
#  Chaque .bat lance un .ps1 de ce dossier (lib\). Le .ps1 commence par
#  Initialize-Xpg : il trouve son propre emplacement, lit config.ini, ouvre
#  son journal, lit les options (/oui, /non-interactif...). Ensuite :
#
#    Start-XpgEtape "Compilation" ... Complete-XpgEtape OK|ECHEC|IGNORE
#    Write-XpgLog  (console en couleur + journal UTF-8 horodaté)
#    Invoke-XpgProcessus (un programme : sa sortie dans le journal, son code vérifié)
#    Read-XpgChoix / Confirm-Xpg (interactif ; en non-interactif : la valeur
#        par défaut, ou un arrêt propre qui dit quelle option manque)
#    Exit-Xpg / Stop-Xpg : le résumé final, la pause si le script a été lancé
#        par un double-clic, le code de retour.
#
#  PowerShell 5.1 (Windows 10 et 11) : pas de ?? ni de ?. ni de ternaire ici.
#  Ce fichier est en UTF-8 AVEC BOM : sans BOM, PowerShell 5.1 le lirait en ANSI.
# =============================================================================

Set-StrictMode -Off
$ErrorActionPreference = 'Stop'

$script:X = $null

# ------------------------------------------------------------------ options ----
function ConvertFrom-XpgArguments {
    # /oui /non-interactif /journal:C:\x.log /mode=diagnostic -oui --aide : un tableau
    # nom -> valeur ("" pour un interrupteur) ; le reste dans .Libres.
    param([string[]] $Arguments)
    $h = @{}
    $libres = New-Object System.Collections.ArrayList
    foreach ($a in $Arguments) {
        if ($null -eq $a -or $a -eq '') { continue }
        if ($a -match '^(--|-|/)([A-Za-z0-9_\-\?]+)([:=](.*))?$') {
            $nom = $Matches[2].ToLowerInvariant()
            $valeur = ''
            if ($Matches[3]) { $valeur = $Matches[4] }
            if ($valeur.Length -ge 2 -and $valeur.StartsWith('"') -and $valeur.EndsWith('"')) { $valeur = $valeur.Substring(1, $valeur.Length - 2) }
            $h[$nom] = $valeur
        } else {
            [void]$libres.Add($a)
        }
    }
    $h['__libres'] = @($libres)
    return $h
}

function Test-XpgOption {
    param([string[]] $Noms)
    foreach ($n in $Noms) { if ($script:X.Options.ContainsKey($n)) { return $true } }
    return $false
}

function Get-XpgOption {
    param([string[]] $Noms, [string] $Defaut = '')
    foreach ($n in $Noms) { if ($script:X.Options.ContainsKey($n)) { return [string]$script:X.Options[$n] } }
    return $Defaut
}

# -------------------------------------------------------------------- .ini ----
function Read-XpgIni {
    # [Section] ; cle = valeur ; les lignes ; et # sont des commentaires. Rend un
    # tableau section -> (cle -> valeur), noms en minuscules. UTF-8 (avec ou sans BOM).
    param([Parameter(Mandatory = $true)] [string] $Chemin)
    $ini = @{}
    $section = ''
    $ini[$section] = @{}
    $lignes = [System.IO.File]::ReadAllLines($Chemin, [System.Text.Encoding]::UTF8)
    foreach ($brute in $lignes) {
        $l = $brute.Trim()
        if ($l -eq '' -or $l.StartsWith(';') -or $l.StartsWith('#')) { continue }
        if ($l.StartsWith('[') -and $l.EndsWith(']')) {
            $section = $l.Substring(1, $l.Length - 2).Trim().ToLowerInvariant()
            if (-not $ini.ContainsKey($section)) { $ini[$section] = @{} }
            continue
        }
        $i = $l.IndexOf('=')
        if ($i -lt 1) { continue }
        $cle = $l.Substring(0, $i).Trim().ToLowerInvariant()
        $val = $l.Substring($i + 1).Trim()
        # Un commentaire en fin de ligne : " ; ..." (un espace avant le point-virgule).
        $c = $val.IndexOf(' ;')
        if ($c -ge 0) { $val = $val.Substring(0, $c).TrimEnd() }
        if ($val.Length -ge 2 -and $val.StartsWith('"') -and $val.EndsWith('"')) { $val = $val.Substring(1, $val.Length - 2) }
        $ini[$section][$cle] = $val
    }
    return $ini
}

function Set-XpgIniValeur {
    # Remplace (ou ajoute) une cle dans un .ini en gardant tout le reste (commentaires
    # compris). Ecrit en UTF-8 avec BOM.
    param([string] $Chemin, [string] $Section, [string] $Cle, [string] $Valeur)
    $lignes = New-Object System.Collections.ArrayList
    if (Test-Path -LiteralPath $Chemin) {
        foreach ($l in [System.IO.File]::ReadAllLines($Chemin, [System.Text.Encoding]::UTF8)) { [void]$lignes.Add($l) }
    }
    $dedans = $false; $fin = -1; $fait = $false
    for ($i = 0; $i -lt $lignes.Count; $i++) {
        $t = ([string]$lignes[$i]).Trim()
        if ($t.StartsWith('[') -and $t.EndsWith(']')) {
            if ($dedans) { break }
            $dedans = ($t.Substring(1, $t.Length - 2).Trim() -ieq $Section)
            if ($dedans) { $fin = $i + 1 }
            continue
        }
        if (-not $dedans) { continue }
        if ($t -ne '') { $fin = $i + 1 }
        if ($t.StartsWith(';') -or $t.StartsWith('#')) { continue }
        $e = $t.IndexOf('=')
        if ($e -gt 0 -and $t.Substring(0, $e).Trim() -ieq $Cle) { $lignes[$i] = "$Cle = $Valeur"; $fait = $true; break }
    }
    if (-not $fait) {
        if ($fin -ge 0) { $lignes.Insert($fin, "$Cle = $Valeur") }
        else {
            if ($lignes.Count -gt 0 -and ([string]$lignes[$lignes.Count - 1]).Trim() -ne '') { [void]$lignes.Add('') }
            [void]$lignes.Add("[$Section]"); [void]$lignes.Add("$Cle = $Valeur")
        }
    }
    Write-XpgTexte -Chemin $Chemin -Texte (($lignes -join "`r`n") + "`r`n")
}

function Write-XpgTexte {
    # Un fichier texte en UTF-8 avec BOM (le Bloc-notes et PowerShell 5.1 le lisent bien).
    param([string] $Chemin, [string] $Texte, [switch] $SansBom)
    $dossier = Split-Path -Parent $Chemin
    if ($dossier -and -not (Test-Path -LiteralPath $dossier)) { New-Item -ItemType Directory -Path $dossier -Force | Out-Null }
    $enc = New-Object System.Text.UTF8Encoding (-not $SansBom)
    [System.IO.File]::WriteAllText($Chemin, $Texte, $enc)
}

# ------------------------------------------------------------------ chemins ----
function Get-XpgDossierConnu {
    param([string] $Nom)
    switch ($Nom.ToLowerInvariant()) {
        'documents'          { return [Environment]::GetFolderPath('MyDocuments') }
        'appdata'            { return [Environment]::GetFolderPath('ApplicationData') }
        'localappdata'       { return [Environment]::GetFolderPath('LocalApplicationData') }
        'programdata'        { return [Environment]::GetFolderPath('CommonApplicationData') }
        'programfiles'       { return [Environment]::GetFolderPath('ProgramFiles') }
        'pf'                 { return [Environment]::GetFolderPath('ProgramFiles') }
        'programfiles(x86)'  { return [Environment]::GetFolderPath('ProgramFilesX86') }
        'pf32'               { return [Environment]::GetFolderPath('ProgramFilesX86') }
        'temp'               { return [System.IO.Path]::GetTempPath().TrimEnd('\') }
        'userprofile'        { return [Environment]::GetFolderPath('UserProfile') }
        'racine'             { if ($script:X) { return $script:X.Racine } }
        'outils'             { if ($script:X) { return $script:X.Outils } }
    }
    return $null
}

function Expand-XpgChemin {
    # {Documents}, {LocalAppData}, {ProgramFiles(x86)}, {pf32}, {Racine}... et %VARIABLES%.
    # Un chemin relatif part de -Base (par defaut : la racine du projet).
    param([string] $Valeur, [string] $Base)
    if ($null -eq $Valeur) { return '' }
    $v = [regex]::Replace($Valeur, '\{([^}]+)\}', {
        param($m)
        $r = Get-XpgDossierConnu $m.Groups[1].Value
        if ($r) { return $r } else { return $m.Value }
    })
    $v = [Environment]::ExpandEnvironmentVariables($v)
    if ($v -eq '') { return '' }
    if (-not [System.IO.Path]::IsPathRooted($v)) {
        if (-not $Base -and $script:X) { $Base = $script:X.Racine }
        if ($Base) { $v = Join-Path $Base $v }
    }
    try { $v = [System.IO.Path]::GetFullPath($v) } catch { }
    return $v.TrimEnd('\')
}

function Get-XpgConfig {
    # Une valeur de config.ini. -Obligatoire : absente, vide ou A_RENSEIGNER -> arret propre
    # qui dit quoi renseigner et ou. -Chemin : developpee (jetons, variables, relatif).
    param([string] $Section, [string] $Cle, [string] $Defaut = '', [switch] $Obligatoire, [switch] $Chemin)
    $v = $Defaut
    $s = $Section.ToLowerInvariant(); $k = $Cle.ToLowerInvariant()
    if ($script:X -and $script:X.Config.ContainsKey($s) -and $script:X.Config[$s].ContainsKey($k)) { $v = [string]$script:X.Config[$s][$k] }
    $manque = ($v -eq '') -or ($v -match '_RENSEIGNER')
    if ($manque -and $Obligatoire) {
        Stop-Xpg -Code 3 -Message "Paramètre manquant : [$Section] $Cle dans $($script:X.ConfigPath)" `
                 -Conseil "Ouvre ce fichier dans le Bloc-notes, renseigne la valeur marquée À_RENSEIGNER, puis relance. Le détail de chaque paramètre est dans docs\CONFIG-INI.md."
    }
    if ($manque) { return '' }
    if ($Chemin) { return (Expand-XpgChemin $v) }
    return $v
}

function Get-XpgRacine {
    # La racine du projet (ou du dossier du programme, pour la maintenance) du script en cours.
    if ($script:X) { return $script:X.Racine }
    return (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}

function Get-XpgContexte { return $script:X }

# ----------------------------------------------------------------- journal ----
function Write-XpgLog {
    param(
        [ValidateSet('INFO', 'OK', 'ATTENTION', 'ERREUR', 'ETAPE', 'DETAIL', 'TITRE', 'QUESTION')] [string] $Niveau = 'INFO',
        [string] $Message = ''
    )
    $heure = (Get-Date).ToString('HH:mm:ss')
    if ($script:X -and $script:X.Journal) {
        try {
            [System.IO.File]::AppendAllText($script:X.Journal, "$heure [$Niveau] $Message`r`n", (New-Object System.Text.UTF8Encoding $true))
        } catch { }
    }
    if ($Niveau -eq 'DETAIL' -and -not ($script:X -and $script:X.Details)) { return }
    $couleur = 'Gray'; $prefixe = '   '
    switch ($Niveau) {
        'OK'        { $couleur = 'Green';  $prefixe = ' OK ' }
        'ATTENTION' { $couleur = 'Yellow'; $prefixe = ' !! ' }
        'ERREUR'    { $couleur = 'Red';    $prefixe = ' XX ' }
        'ETAPE'     { $couleur = 'Cyan';   $prefixe = '' }
        'TITRE'     { $couleur = 'White';  $prefixe = '' }
        'QUESTION'  { $couleur = 'White';  $prefixe = '' }
        'DETAIL'    { $couleur = 'DarkGray' }
    }
    Write-Host ($prefixe + $Message) -ForegroundColor $couleur
}

function Write-XpgDetail { param([string] $Message) Write-XpgLog -Niveau DETAIL -Message $Message }

# ------------------------------------------------------------------ etapes ----
function Start-XpgEtape {
    param([string] $Nom)
    $e = [pscustomobject]@{ Nom = $Nom; Debut = Get-Date; Fin = $null; Statut = 'EN COURS'; Detail = '' }
    [void]$script:X.Etapes.Add($e)
    $n = $script:X.Etapes.Count
    Write-Host ''
    Write-XpgLog -Niveau ETAPE -Message ("[{0}] {1}" -f $n, $Nom)
    return $e
}

function Complete-XpgEtape {
    param([ValidateSet('OK', 'ECHEC', 'IGNORE', 'ATTENTION')] [string] $Statut = 'OK', [string] $Detail = '')
    if ($script:X.Etapes.Count -eq 0) { return }
    $e = $script:X.Etapes[$script:X.Etapes.Count - 1]
    $e.Fin = Get-Date; $e.Statut = $Statut; $e.Detail = $Detail
    $d = Format-XpgDuree ($e.Fin - $e.Debut)
    $texte = "$($e.Nom) : $Statut ($d)"
    if ($Detail) { $texte += " - $Detail" }
    switch ($Statut) {
        'OK'        { Write-XpgLog -Niveau OK -Message $texte }
        'ECHEC'     { Write-XpgLog -Niveau ERREUR -Message $texte }
        'ATTENTION' { Write-XpgLog -Niveau ATTENTION -Message $texte }
        default     { Write-XpgLog -Niveau INFO -Message $texte }
    }
}

function Format-XpgDuree {
    param([TimeSpan] $T)
    if ($T.TotalSeconds -lt 60) { return ('{0:0.0} s' -f $T.TotalSeconds).Replace('.', ',') }
    if ($T.TotalMinutes -lt 60) { return ('{0} min {1:00} s' -f [int][math]::Floor($T.TotalMinutes), $T.Seconds) }
    return ('{0} h {1:00} min' -f [int][math]::Floor($T.TotalHours), $T.Minutes)
}

function Format-XpgTaille {
    param([double] $Octets)
    $u = @('o', 'Ko', 'Mo', 'Go', 'To'); $i = 0
    while ($Octets -ge 1024 -and $i -lt 4) { $Octets = $Octets / 1024; $i++ }
    if ($i -eq 0) { return "$([int]$Octets) o" }
    return (('{0:0.0} {1}' -f $Octets, $u[$i]).Replace('.', ','))
}

# ------------------------------------------------------------- initialiser ----
function Initialize-Xpg {
    param(
        [Parameter(Mandatory = $true)] [string] $Nom,
        [string] $Titre = '',
        [string[]] $Arguments = @(),
        [ValidateSet('developpement', 'maintenance')] [string] $Genre = 'developpement',
        [string] $Usage = ''
    )
    # Le .bat qui nous lance attend cette marque : sans elle, PowerShell n'a pas pu lancer le
    # script (une strategie de groupe, le plus souvent) et le .bat le dit.
    if ($env:XPG_DEMARRE) { try { [System.IO.File]::WriteAllText($env:XPG_DEMARRE, $Nom) } catch { } }
    $lib = $PSScriptRoot
    $outils = Split-Path -Parent $lib
    $racine = Split-Path -Parent $outils
    $opts = ConvertFrom-XpgArguments $Arguments
    # /marque:<fichier> : la meme marque, pour l'installateur (Inno Setup ne sait pas passer de
    # variable d'environnement) : sans elle, il sait que Windows a refuse le script.
    if ($opts.ContainsKey('marque') -and $opts['marque']) { try { [System.IO.File]::WriteAllText($opts['marque'], $Nom) } catch { } }

    $configPath = Join-Path $outils 'config.ini'
    if ($opts.ContainsKey('config') -and $opts['config']) { $configPath = [System.IO.Path]::GetFullPath($opts['config']) }
    $config = @{ '' = @{} }
    $erreurConfig = ''
    if (Test-Path -LiteralPath $configPath) {
        try { $config = Read-XpgIni $configPath } catch { $erreurConfig = $_.Exception.Message }
    } elseif ($Genre -eq 'developpement') {
        $erreurConfig = "config.ini introuvable : $configPath"
    }

    $nonInteractif = $opts.ContainsKey('non-interactif') -or $opts.ContainsKey('noninteractif') -or $opts.ContainsKey('batch') -or
                     ($env:XPG_NONINTERACTIF -eq '1')
    try { if ([Console]::IsInputRedirected) { $nonInteractif = $true } } catch { }

    $script:X = [pscustomobject]@{
        Nom = $Nom; Titre = $Titre; Genre = $Genre
        Lib = $lib; Outils = $outils; Racine = $racine
        Config = $config; ConfigPath = $configPath
        Options = $opts; NonInteractif = [bool]$nonInteractif
        Oui = ($opts.ContainsKey('oui') -or $opts.ContainsKey('yes'))
        Details = ($opts.ContainsKey('details') -or $opts.ContainsKey('verbose'))
        Journal = ''; DossierJournaux = ''
        Etapes = New-Object System.Collections.ArrayList
        Debut = Get-Date; DoubleClic = $false
    }
    $script:X.DoubleClic = Test-XpgDoubleClic

    # Le journal : outils\journaux\ pour la compilation ; pour la maintenance, les Journaux de
    # l'installation (installation.ini, [Maintenance] journaux : %LOCALAPPDATA%\XpgAnalyzer ou
    # %ProgramData%\XPGAnalyser), sinon %LOCALAPPDATA%\XpgAnalyzer\Journaux. /journal:<fichier> l'impose.
    $horodate = "{0}_{1}.log" -f $Nom, (Get-Date).ToString('yyyy-MM-dd_HH-mm-ss')
    $secours = Join-Path (Get-XpgDossierConnu 'localappdata') 'XpgAnalyzer\Journaux'
    if ($opts.ContainsKey('journal') -and $opts['journal']) {
        $journal = [System.IO.Path]::GetFullPath($opts['journal'])
        $dossierJ = Split-Path -Parent $journal
    } else {
        if ($Genre -eq 'developpement') { $dossierJ = Join-Path $outils 'journaux' }
        else {
            $dossierJ = $secours
            $iniInst = Join-Path $racine 'installation.ini'
            if (Test-Path -LiteralPath $iniInst) {
                try {
                    $ii = Read-XpgIni $iniInst
                    if ($ii.ContainsKey('maintenance') -and $ii['maintenance'].ContainsKey('journaux')) {
                        $jj = [Environment]::ExpandEnvironmentVariables([string]$ii['maintenance']['journaux'])
                        if ($jj -and [System.IO.Path]::IsPathRooted($jj)) { $dossierJ = $jj }
                    }
                } catch { }
            }
        }
        $journal = Join-Path $dossierJ $horodate
    }
    # Le dossier voulu, puis (maintenance) celui de secours : un compte sans droits peut ne pas
    # pouvoir ecrire dans les Journaux d'une installation pour tous les comptes.
    $essais = New-Object System.Collections.ArrayList
    [void]$essais.Add(@($dossierJ, $journal))
    if ($Genre -eq 'maintenance' -and $dossierJ -ne $secours -and -not $opts.ContainsKey('journal')) { [void]$essais.Add(@($secours, (Join-Path $secours $horodate))) }
    foreach ($e in $essais) {
        try {
            if (-not (Test-Path -LiteralPath $e[0])) { New-Item -ItemType Directory -Path $e[0] -Force | Out-Null }
            # Un journal impose (/journal:) qui existe deja est continue, pas vide : les etapes de
            # l'installateur (avant, puis apres la copie) ecrivent dans le meme.
            if ($opts.ContainsKey('journal') -and (Test-Path -LiteralPath $e[1])) {
                [System.IO.File]::AppendAllText($e[1], "`r`n", (New-Object System.Text.UTF8Encoding $true))
            } else {
                [System.IO.File]::WriteAllText($e[1], '', (New-Object System.Text.UTF8Encoding $true))
            }
            $script:X.Journal = $e[1]; $script:X.DossierJournaux = $e[0]
            break
        } catch {
            Write-Host " !! Journal impossible dans $($e[0]) : $($_.Exception.Message)" -ForegroundColor Yellow
        }
    }

    if ($Titre) {
        Write-Host ''
        Write-XpgLog -Niveau TITRE -Message $Titre
        Write-XpgLog -Niveau TITRE -Message ('=' * [Math]::Min(78, $Titre.Length))
    }
    Write-XpgDetail "Script : $Nom ; dossier : $outils ; projet : $racine"
    Write-XpgDetail ("PowerShell {0} ; Windows {1} ; utilisateur {2} ; administrateur : {3}" -f $PSVersionTable.PSVersion, [Environment]::OSVersion.Version, [Environment]::UserName, (Test-XpgAdmin))
    Write-XpgDetail ("Options : " + (($opts.Keys | Where-Object { $_ -ne '__libres' } | ForEach-Object { "/$_" }) -join ' '))
    if ($script:X.Journal) { Write-XpgLog -Niveau INFO -Message "Journal : $($script:X.Journal)" }

    if ($opts.ContainsKey('aide') -or $opts.ContainsKey('?') -or $opts.ContainsKey('help') -or $opts.ContainsKey('h')) {
        Write-Host ''
        if ($Usage) { Write-Host $Usage } else { Write-Host "Pas d'aide pour ce script : voir docs\GUIDE-COMPILATION.md." }
        Write-Host ''
        Write-Host 'Options communes : /non-interactif (aucune question : valeurs par défaut), /oui (accepte les confirmations),'
        Write-Host '                   /journal:<fichier>, /config:<config.ini>, /details (tout le détail à l''écran), /aide'
        exit 0
    }
    if ($erreurConfig) {
        Stop-Xpg -Code 3 -Message $erreurConfig -Conseil 'config.ini doit être dans le dossier outils\ (à côté de ce script) : voir docs\CONFIG-INI.md.'
    }
    return $script:X
}

function Test-XpgDoubleClic {
    # Lance par un double-clic : powershell <- cmd <- explorer. On garde alors la fenetre
    # ouverte a la fin, pour lire le resume.
    try {
        $moi = Get-CimInstance Win32_Process -Filter "ProcessId=$PID" -ErrorAction Stop
        $cmd = Get-CimInstance Win32_Process -Filter "ProcessId=$($moi.ParentProcessId)" -ErrorAction Stop
        if ($cmd.Name -notmatch '^cmd\.exe$') { return $false }
        $parent = Get-CimInstance Win32_Process -Filter "ProcessId=$($cmd.ParentProcessId)" -ErrorAction Stop
        return ($parent.Name -match '^explorer\.exe$')
    } catch { return $false }
}

function Test-XpgAdmin {
    try {
        $id = [Security.Principal.WindowsIdentity]::GetCurrent()
        return (New-Object Security.Principal.WindowsPrincipal $id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    } catch { return $false }
}

# ----------------------------------------------------------- les questions ----
function Read-XpgChoix {
    # Un menu : les options numerotees, un choix. En non-interactif : -Defaut, ou un arret
    # propre qui dit quelle option de la ligne de commande donne la reponse.
    param(
        [string] $Question,
        [string[]] $Options,
        [int] $Defaut = 0,             # 1..n ; 0 : pas de defaut
        [string] $OptionLigne = ''     # ex. "/choix:1" : pour le message du non-interactif
    )
    if ($script:X.NonInteractif) {
        if ($Defaut -gt 0) {
            Write-XpgLog -Niveau INFO -Message ("{0} -> {1} (non interactif)" -f $Question, $Options[$Defaut - 1])
            return $Defaut
        }
        Stop-Xpg -Code 4 -Message "Une réponse est nécessaire : $Question" -Conseil "En mode non interactif, donne-la sur la ligne de commande ($OptionLigne)."
    }
    Write-Host ''
    Write-XpgLog -Niveau QUESTION -Message $Question
    for ($i = 0; $i -lt $Options.Count; $i++) { Write-Host ("  [{0}] {1}" -f ($i + 1), $Options[$i]) }
    while ($true) {
        $invite = '  Ton choix'
        if ($Defaut -gt 0) { $invite += " (Entrée = $Defaut)" }
        $r = Read-Host $invite
        if ($r -eq '' -and $Defaut -gt 0) { $r = [string]$Defaut }
        $n = 0
        if ([int]::TryParse($r, [ref]$n) -and $n -ge 1 -and $n -le $Options.Count) {
            Write-XpgDetail "Réponse : $n ($($Options[$n - 1]))"
            return $n
        }
        Write-Host "  Tape un nombre entre 1 et $($Options.Count)." -ForegroundColor Yellow
    }
}

function Confirm-Xpg {
    # O/N. /oui accepte d'office ; en non-interactif sans /oui : -Defaut.
    param([string] $Question, [bool] $Defaut = $false)
    if ($script:X.Oui) { Write-XpgLog -Niveau INFO -Message "$Question -> oui (/oui)"; return $true }
    if ($script:X.NonInteractif) {
        $t = 'non'; if ($Defaut) { $t = 'oui' }
        Write-XpgLog -Niveau INFO -Message "$Question -> $t (non interactif, valeur par défaut)"
        return $Defaut
    }
    $suffixe = ' (o/N)'; if ($Defaut) { $suffixe = ' (O/n)' }
    while ($true) {
        $r = (Read-Host ("  " + $Question + $suffixe)).Trim().ToLowerInvariant()
        if ($r -eq '') { Write-XpgDetail "Réponse : défaut"; return $Defaut }
        if ($r -in @('o', 'oui', 'y', 'yes')) { Write-XpgDetail "Réponse : oui"; return $true }
        if ($r -in @('n', 'non', 'no')) { Write-XpgDetail "Réponse : non"; return $false }
        Write-Host '  Réponds o (oui) ou n (non).' -ForegroundColor Yellow
    }
}

function Read-XpgTexte {
    param([string] $Question, [string] $Defaut = '', [string] $OptionLigne = '')
    if ($script:X.NonInteractif) {
        if ($Defaut) { return $Defaut }
        Stop-Xpg -Code 4 -Message "Une réponse est nécessaire : $Question" -Conseil "En mode non interactif, donne-la sur la ligne de commande ($OptionLigne)."
    }
    $invite = "  $Question"
    if ($Defaut) { $invite += " (Entrée = $Defaut)" }
    $r = Read-Host $invite
    if ($r -eq '') { return $Defaut }
    return $r.Trim().Trim('"')
}

# ---------------------------------------------------------- les programmes ----
function Invoke-XpgProcessus {
    # Lance un programme, sa sortie (et ses erreurs) ligne par ligne dans le journal, a
    # l'ecran si -Afficher. Rend le code de retour ; -CodesOk : ceux qui ne sont pas des
    # echecs (-Critique : sinon arret propre).
    param(
        [Parameter(Mandatory = $true)] [string] $Fichier,
        [string[]] $Arguments = @(),
        [string] $Dossier = '',
        [switch] $Afficher,
        [int[]] $CodesOk = @(0),
        [switch] $Critique,
        [string] $Libelle = ''
    )
    if (-not $Libelle) { $Libelle = [System.IO.Path]::GetFileName($Fichier) }
    Write-XpgDetail ("Commande : `"{0}`" {1}" -f $Fichier, ($Arguments -join ' '))
    $ancien = $ErrorActionPreference
    $avant = Get-Location
    $ErrorActionPreference = 'Continue'
    $code = -1
    try {
        if ($Dossier) { Set-Location -LiteralPath $Dossier }
        & $Fichier @Arguments 2>&1 | ForEach-Object {
            $ligne = [string]$_
            if ($script:X.Journal) {
                try { [System.IO.File]::AppendAllText($script:X.Journal, "         | $ligne`r`n", (New-Object System.Text.UTF8Encoding $true)) } catch { }
            }
            if ($Afficher) { Write-Host "   $ligne" -ForegroundColor DarkGray }
        }
        $code = $LASTEXITCODE
        if ($null -eq $code) { $code = 0 }
    } catch {
        Write-XpgLog -Niveau ERREUR -Message "$Libelle n'a pas pu être lancé : $($_.Exception.Message)"
        $code = 9009
    } finally {
        Set-Location -LiteralPath $avant
        $ErrorActionPreference = $ancien
    }
    Write-XpgDetail "$Libelle : code de retour $code"
    if ($Critique -and ($CodesOk -notcontains $code)) {
        Stop-Xpg -Code 1 -Message "$Libelle a échoué (code $code)." -Conseil "Le détail est dans le journal : $($script:X.Journal)"
    }
    return $code
}

function Start-XpgScriptEnfant {
    # Un autre script de lib\, dans cette meme fenetre, attendu ; rend son code. Les reponses
    # suivent (/non-interactif, /oui, /details, /choix:) : une question pour toi reste une
    # question (jamais d'arret force parce qu'on est un script lance par un autre). L'enfant ne
    # se met pas en pause a la fin : son parent est PowerShell, pas un double-clic.
    param([Parameter(Mandatory = $true)] [string] $Script, [string[]] $Arguments = @(), [string] $Origine = '')
    $a = New-Object System.Collections.ArrayList
    foreach ($x in @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $script:X.Lib $Script))) { [void]$a.Add($x) }
    foreach ($x in $Arguments) { [void]$a.Add($x) }
    if ($script:X.NonInteractif) { [void]$a.Add('/non-interactif') }
    if ($script:X.Oui) { [void]$a.Add('/oui') }
    if ($script:X.Details) { [void]$a.Add('/details') }
    # Les reponses au menu des outils suivent aussi (build.bat /choix:3 /dossier:<VS>) ; un chemin
    # perd sa barre finale (C:\VS\" serait lu comme un guillemet echappe).
    foreach ($nomOpt in @('config', 'choix', 'dossier', 'installateur')) {
        $v = Get-XpgOption -Noms @($nomOpt)
        if ($v) { [void]$a.Add(("/{0}:{1}" -f $nomOpt, $v.TrimEnd('\'))) }
    }
    if ($Origine) { [void]$a.Add("/origine:$Origine") }
    $ps = Join-Path $PSHOME 'powershell.exe'
    if (-not (Test-Path -LiteralPath $ps)) { $ps = (Get-Process -Id $PID).Path }
    $texte = @($a | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } })
    Write-XpgDetail ("Lancement : {0} {1}" -f $Script, (($texte | Select-Object -Skip 6) -join ' '))
    $p = Start-Process -FilePath $ps -ArgumentList $texte -NoNewWindow -Wait -PassThru
    return [int]$p.ExitCode
}

function Update-XpgConfig {
    # Relit config.ini : un script enfant a pu l'ecrire (install_build_tools, choix 3 :
    # [Compilateur] compilateur_manuel), et la detection du compilateur le lit.
    try { $script:X.Config = Read-XpgIni $script:X.ConfigPath } catch { Write-XpgDetail "config.ini : relecture impossible ($($_.Exception.Message))" }
}

function Save-XpgOrigine {
    # Ce que tu as lance, le script et ses options : si l'installation des outils demande un
    # redemarrage de Windows, la reprise relance exactement cela (install_build_tools.ps1).
    # Rien si un autre script nous a lance (/origine:) : c'est lui, l'origine.
    param([string] $Script, [string[]] $Arguments = @())
    if (Test-XpgOption @('origine')) { return }
    $garder = @($Arguments | Where-Object { $_ -and ([string]$_) -notmatch '^[/-](choix|origine|depuis|reprendre|marque)([:=]|$)' })
    try { Save-XpgEtat -Chemin (Join-Path $script:X.Outils 'etat\origine.json') -Etat ([pscustomobject]@{ script = $Script; arguments = $garder }) } catch { }
}

function Get-XpgSortie {
    # La sortie d'un programme, pour la lire (une version, un chemin) ; "" s'il echoue.
    param([string] $Fichier, [string[]] $Arguments = @())
    $ancien = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $s = & $Fichier @Arguments 2>&1 | ForEach-Object { [string]$_ }
        return (($s | Out-String).Trim())
    } catch { return '' } finally { $ErrorActionPreference = $ancien }
}

# ------------------------------------------------------------ fins propres ----
function Write-XpgResume {
    param([int] $Code)
    $duree = Format-XpgDuree ((Get-Date) - $script:X.Debut)
    Write-Host ''
    Write-XpgLog -Niveau TITRE -Message ('-' * 78)
    Write-XpgLog -Niveau TITRE -Message ("Résumé de {0} ({1})" -f $script:X.Nom, $duree)
    foreach ($e in $script:X.Etapes) {
        $d = ''
        if ($e.Fin) { $d = Format-XpgDuree ($e.Fin - $e.Debut) }
        $ligne = "  {0,-10} {1}" -f $e.Statut, $e.Nom
        if ($d) { $ligne += " ($d)" }
        if ($e.Detail) { $ligne += " - $($e.Detail)" }
        $niveau = 'INFO'
        if ($e.Statut -eq 'OK') { $niveau = 'OK' } elseif ($e.Statut -eq 'ECHEC' -or $e.Statut -eq 'EN COURS') { $niveau = 'ERREUR' } elseif ($e.Statut -eq 'ATTENTION') { $niveau = 'ATTENTION' }
        Write-XpgLog -Niveau $niveau -Message $ligne
    }
    if ($Code -eq 0) { Write-XpgLog -Niveau OK -Message "Terminé sans erreur." }
    else { Write-XpgLog -Niveau ERREUR -Message "Terminé avec le code $Code." }
    if ($script:X.Journal) { Write-XpgLog -Niveau INFO -Message "Journal complet : $($script:X.Journal)" }
    Write-XpgLog -Niveau TITRE -Message ('-' * 78)
}

function Wait-XpgFin {
    if ($script:X -and $script:X.DoubleClic -and -not $script:X.NonInteractif) {
        Write-Host ''
        [void](Read-Host 'Appuie sur Entrée pour fermer cette fenêtre')
    }
}

function Exit-Xpg {
    param([int] $Code = 0)
    foreach ($e in $script:X.Etapes) { if ($e.Statut -eq 'EN COURS') { $e.Statut = 'ECHEC'; $e.Fin = Get-Date; $e.Detail = 'interrompue' } }
    Write-XpgResume -Code $Code
    Wait-XpgFin
    exit $Code
}

function Stop-Xpg {
    # L'arret propre : ce qui s'est passe, que faire, le resume, le code.
    param([int] $Code = 1, [string] $Message = '', [string] $Conseil = '', $Exception = $null)
    if ($null -eq $script:X) { Write-Host "ERREUR : $Message" -ForegroundColor Red; exit $Code }
    if ($script:X.Etapes.Count -gt 0) {
        $e = $script:X.Etapes[$script:X.Etapes.Count - 1]
        if ($e.Statut -eq 'EN COURS') { $e.Statut = 'ECHEC'; $e.Fin = Get-Date; $e.Detail = $Message }
    }
    Write-Host ''
    if ($Message) { Write-XpgLog -Niveau ERREUR -Message $Message }
    if ($Conseil) { Write-XpgLog -Niveau INFO -Message "Que faire : $Conseil" }
    if ($Exception) {
        Write-XpgDetail ("Exception : " + ($Exception | Out-String))
        try { Write-XpgDetail ("Pile : " + $Exception.ScriptStackTrace) } catch { }
    }
    Write-XpgResume -Code $Code
    Wait-XpgFin
    exit $Code
}

# ----------------------------------------------------------- la securite ----
function Get-XpgSha256 {
    param([string] $Chemin)
    return (Get-FileHash -LiteralPath $Chemin -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-XpgProvenance {
    # Refuse un fichier dont la provenance n'est pas validee : signature Authenticode valide
    # de l'editeur attendu, et SHA-256 egal a celui de config.ini quand il y en a un.
    param([string] $Chemin, [string] $Editeur = '', [string] $Sha256 = '')
    $r = [pscustomobject]@{ Ok = $false; Signature = ''; Signataire = ''; Sha256 = ''; Raison = '' }
    $r.Sha256 = Get-XpgSha256 $Chemin
    Write-XpgLog -Niveau INFO -Message ("SHA-256 de {0} : {1}" -f [System.IO.Path]::GetFileName($Chemin), $r.Sha256)
    if ($Sha256) {
        if ($r.Sha256 -ne $Sha256.ToLowerInvariant()) {
            $r.Raison = "SHA-256 différent de celui de config.ini ($Sha256)"
            return $r
        }
        Write-XpgLog -Niveau OK -Message 'SHA-256 conforme à config.ini'
    }
    try {
        $sig = Get-AuthenticodeSignature -LiteralPath $Chemin
        $r.Signature = [string]$sig.Status
        if ($sig.SignerCertificate) { $r.Signataire = $sig.SignerCertificate.Subject }
    } catch {
        $r.Raison = "signature illisible : $($_.Exception.Message)"
        return $r
    }
    Write-XpgLog -Niveau INFO -Message ("Signature : {0} ; signataire : {1}" -f $r.Signature, $r.Signataire)
    if ($r.Signature -ne 'Valid') {
        $r.Raison = "signature Authenticode non valide ($($r.Signature))"
        return $r
    }
    if ($Editeur -and ($r.Signataire -notmatch ('CN="?' + [regex]::Escape($Editeur)))) {
        $r.Raison = "signé par « $($r.Signataire) », pas par « $Editeur »"
        return $r
    }
    $r.Ok = $true
    return $r
}

function Test-XpgInternet {
    # Une requete HEAD courte vers l'adresse donnee (5 s) : pas de blocage indefini.
    param([string] $Url, [int] $Secondes = 8)
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        $req = [System.Net.WebRequest]::Create($Url)
        $req.Method = 'HEAD'; $req.Timeout = $Secondes * 1000
        $rep = $req.GetResponse(); $rep.Close()
        return $true
    } catch [System.Net.WebException] {
        # Une reponse HTTP (meme 403/405) prouve que le reseau passe.
        if ($_.Exception.Response) { return $true }
        return $false
    } catch { return $false }
}

function Save-XpgFichierWeb {
    # Telecharge en HTTPS seulement, avec une progression, dans -Destination.
    param([string] $Url, [string] $Destination)
    if (-not $Url.StartsWith('https://', [StringComparison]::OrdinalIgnoreCase)) {
        Stop-Xpg -Code 5 -Message "Adresse refusée (HTTPS obligatoire) : $Url" -Conseil 'Corrige l''adresse dans config.ini.'
    }
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $dossier = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $dossier)) { New-Item -ItemType Directory -Path $dossier -Force | Out-Null }
    Write-XpgLog -Niveau INFO -Message "Téléchargement : $Url"
    $ancien = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'   # la barre de 5.1 ralentit enormement Invoke-WebRequest
    try {
        Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing -TimeoutSec 600
    } finally { $ProgressPreference = $ancien }
    $taille = (Get-Item -LiteralPath $Destination).Length
    Write-XpgLog -Niveau OK -Message ("Téléchargé : {0} ({1})" -f $Destination, (Format-XpgTaille $taille))
}

# ------------------------------------------------------------ l'etat (JSON) ----
function Save-XpgEtat {
    param([string] $Chemin, $Etat)
    $Etat | Add-Member -NotePropertyName 'maj' -NotePropertyValue ((Get-Date).ToString('s')) -Force
    Write-XpgTexte -Chemin $Chemin -Texte ($Etat | ConvertTo-Json -Depth 8)
}

function Read-XpgEtat {
    param([string] $Chemin)
    if (-not (Test-Path -LiteralPath $Chemin)) { return $null }
    try { return ([System.IO.File]::ReadAllText($Chemin, [System.Text.Encoding]::UTF8) | ConvertFrom-Json) } catch { return $null }
}

Export-ModuleMember -Function *-Xpg*, Initialize-Xpg, Exit-Xpg, Stop-Xpg, Confirm-Xpg, Start-XpgScriptEnfant

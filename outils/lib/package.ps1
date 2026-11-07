#Requires -Version 5.1
# =============================================================================
#  package.ps1 - lance par package.bat : TOUT, DANS L'ORDRE
#
#   1. les versions concordent (config.ini, CMakeLists.txt, Version.hpp, .rc) ;
#   2. la compilation Release (build.ps1) ;
#   3. les tests (test_build.ps1) ;
#   4. le dossier a livrer, dist\staging : l'exe, SDL3.dll, pdfium.dll (1.12.30), le runtime Visual C++
#      (deploiement local : un PC vierge n'a rien a installer), resources\,
#      libs\, maintenance\ (repair, verify...), licences\, LISEZ-MOI.txt ;
#   5. manifeste.json : chaque fichier, sa taille, son SHA-256, son role, et
#      les DLL dont l'exe a besoin (repair.bat et verify s'en servent) ;
#   6. l'installateur (build_installer.ps1) : XPGAnalyser-Setup-<version>.exe a la racine
#      du dossier, et son empreinte dans dist\SHA256SUMS.txt.
#  1.30.0 : licences\registre.xpgreg (le registre des licences signe par l'editeur) s'il est dans
#  ressources-licences\ ; puis LE CONTROLE DU PAQUET (Test-XpgLivraison), le meme que
#  outils/mingw/package_mingw.py : rien de l'outil Admin des licences, aucune cle privee, aucun
#  jeton, pas la cle d'essai, un registre signe par une cle de PublisherKey.hpp - un seul ecart
#  arrete la construction.
#  1.32.0 : de meme licences\revocations.xpgrev (la liste de revocation livree, depuis
#  ressources-licences\) ; licences\ n'admet que le registre, la liste et les textes de licence des
#  bibliotheques ; [Licences] obligatoires = oui (config.ini) refuse un paquet sans vraie cle d'editeur.
#
#  /exe:<fichier> /sdl:<SDL3.dll> /pdfium:<pdfium.dll> /chaine:mingw : empaqueter un exe deja
#  compile ailleurs (c'est ainsi qu'a ete fait l'installateur livre avec la
#  chaine, sous Linux, avec MinGW : pas de runtime Visual C++ a y mettre).
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgOutils.psm1') -Force -Global -DisableNameChecking

$usage = @'
package.bat [options]
  /sans-tests          ne lance pas test_build (plus rapide ; déconseillé)
  /sans-installateur   s'arrête au dossier dist\staging (sans Inno Setup)
  /exe:<fichier>       empaquette cet exe au lieu de compiler (avec /sdl:<SDL3.dll>)
  /pdfium:<fichier>    le pdfium.dll à livrer (par défaut third_party\pdfium\win-x64\pdfium.dll)
  /chaine:msvc|mingw   la chaîne qui a compilé l'exe (msvc par défaut : runtime VC++ livré)
'@
$X = Initialize-Xpg -Nom 'package' -Titre 'XPGAnalyser - compilation, tests et installateur' -Arguments $Arguments -Usage $usage
Save-XpgOrigine -Script 'package.ps1' -Arguments @($Arguments)

function Invoke-XpgEnfant {
    # Un script de lib\ (build, test_build, build_installer, install_build_tools) : ses questions
    # restent des questions (sauf si package.bat a lui-meme /non-interactif) ; /origine:package
    # lui dit qui reprendre apres un redemarrage de Windows.
    param([string] $Script, [string[]] $Args2, [string] $Libelle)
    $code = Start-XpgScriptEnfant -Script $Script -Arguments $Args2 -Origine 'package'
    if ($code -eq 3010) {
        Stop-Xpg -Code 3010 -Message 'Windows doit redémarrer pour finir l''installation des outils.' `
                 -Conseil 'Après le redémarrage, à l''ouverture de session, la procédure reprend toute seule et relance package.bat (ou relance-le toi-même).'
    }
    if ($code -ne 0) { Stop-Xpg -Code $code -Message "$Libelle a échoué (code $code)." -Conseil 'Son journal est dans outils\journaux\.' }
}

function Copy-XpgArbre {
    param([string] $De, [string] $Vers, [string[]] $Exclure = @())
    if (-not (Test-Path -LiteralPath $De)) { return 0 }
    New-Item -ItemType Directory -Path $Vers -Force | Out-Null
    $n = 0
    $base = (Get-Item -LiteralPath $De).FullName.TrimEnd('\', '/')
    foreach ($f in Get-ChildItem -LiteralPath $De -Recurse -File -Force) {
        $rel = $f.FullName.Substring($base.Length + 1)
        $sauter = $false
        foreach ($e in $Exclure) { if ($rel -like $e) { $sauter = $true } }
        if ($sauter) { continue }
        $cible = Join-Path $Vers $rel
        $d = Split-Path -Parent $cible
        if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Copy-Item -LiteralPath $f.FullName -Destination $cible -Force
        $n++
    }
    return $n
}

# ---- 1.30.0 : LE CONTROLE DU PAQUET -----------------------------------------------------------------
#  Le MEME que outils/mingw/package_mingw.py (controle_livraison) : memes chaines (en ASCII et en UTF-16 :
#  les chaines larges des exe), memes motifs, memes noms, meme controle du registre livre. Changer l'un,
#  c'est changer l'autre (docs\LICENCES.md). Les octets sont lus en Latin-1 (un caractere par octet) : les
#  chaines se cherchent en ordinal, les motifs avec Regex (sensibles a la casse, comme en Python).
#  1.32.0 : la liste de revocation livree, ce que licences\ admet, ni licence, ni demande, ni github*.json,
#  ni dossier archive, ni etat ou historique d'un PC ; [Licences] obligatoires.
$script:XpgAdminChaines = @(
    @('XPG-License-Admin', "le nom de l'outil Admin des licences"),
    @('XPG License Manager Admin', "le nom de l'outil Admin des licences"),
    @('XPG-License-Manager-Admin', "le nom de l'application Admin des licences (1.33.0)"),
    @('XPG-LICENSE-ADMIN-NE-JAMAIS-LIVRER', "la marque de l'outil Admin des licences"),
    @('XPG-CLE-ESSAI-LICENCES', "la cle d'ESSAI de l'editeur (cree la vraie cle et regenere src/licensing/PublisherKey.hpp)"),
    @('XPG-SAUVEGARDE-CLE', "une sauvegarde de la cle de l'editeur (1.32.0)"),
    @('XPG-CHIFFRE 1 xpgs-', "un fichier chiffre de la synchro des licences (1.31.0)"),
    @('XPG-INDEX 1 xpgs-', "l'index de la synchro des licences (1.31.0)"),
    @('XPG-LICENSING 1 licence ', "une licence signee (le contenu d'un .xpglic : elle va a un client, jamais dans le paquet) (1.32.0)"),
    @('XPG-LICENSING 1 demande ', "une demande d'activation (le contenu d'un .xpgreq) (1.32.0)")
)
$script:XpgAdminMotifs = @(
    @('-----BEGIN [A-Z ]*PRIVATE KEY-----\r?\n[A-Za-z0-9+/=\r\n]{40,}', 'une cle privee (bloc PEM)'),
    @('(?<![A-Za-z0-9_])gh[pousr]_[A-Za-z0-9]{36}(?![A-Za-z0-9_])|(?<![A-Za-z0-9_])github_pat_[A-Za-z0-9_]{50,}', 'un jeton GitHub')
)
$script:XpgAdminNoms = @('*.cle', 'cle_editeur.bin', 'cle-editeur-licences*', '*license*admin*', '*licence*admin*', '*license_admin*',
                        '*.xpgcle', '*.xpgsync', 'activations.jsonl',
                        '*.xpglic', '*.xpgreq', 'github*.json', 'etat.bin', 'historique.jsonl')   # 1.32.0
$script:XpgAdminDossiers = @('archive')   # 1.32.0 : l'archive de l'outil Admin (signer-*, archiver), a aucun niveau
# 1.32.0 : ce que licences\ admet (chemins relatifs a licences\, sans casse) - le reste est un ecart.
$script:XpgLicencesPermis = @('registre.xpgreg', 'revocations.xpgrev', '*-LICENSE*', '*.LICENSE',
                             'pdfium\*.txt', 'pdfium\*.md', 'pdfium\*.ijg')

function Get-XpgClesCompilees {
    # Les identifiants des cles d'un fichier ecrit par exporter-cle (src\licensing\<Nom>).
    param([string] $Racine, [string] $Nom)
    $chemin = Join-Path $Racine "src\licensing\$Nom"
    if (-not (Test-Path -LiteralPath $chemin -PathType Leaf)) { return @() }
    $t = [System.IO.File]::ReadAllText($chemin)
    return @([regex]::Matches($t, '\{"(xpg-[0-9a-f]{16})",') | ForEach-Object { $_.Groups[1].Value })
}

function Test-XpgDocumentSigne {
    # Un document livre (le registre, la liste de revocation) : signe par une cle des programmes (PublisherKey.hpp),
    # jamais par la cle d'essai. Rend l'ecart, ou $null (controle_signe de package_mingw.py).
    param([string] $Base, [string] $Racine, [string] $Rel, [string] $Genre, [string] $Sujet, [string] $PasSigne, [string] $Signe)
    $chemin = Join-Path $Base $Rel
    if (-not (Test-Path -LiteralPath $chemin -PathType Leaf)) { return $null }
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    $t = $latin1.GetString([System.IO.File]::ReadAllBytes($chemin))
    $cles = @([regex]::Matches($t, '\nSIGNATURE ed25519 (xpg-[0-9a-f]{16}) [0-9a-f]{128}\n') | ForEach-Object { $_.Groups[1].Value })
    if (-not [regex]::IsMatch($t, ('\AXPG-LICENSING 1 ' + $Genre + ' ([0-9]+)\n')) -or $cles.Count -ne 1) {
        return "$Rel : $Sujet n'est pas $PasSigne (un seul bloc XPG-LICENSING $Genre)"
    }
    if (@(Get-XpgClesCompilees -Racine $Racine -Nom 'TestPublisherKey.hpp') -contains $cles[0]) {
        return "$Rel : $Sujet est $Signe par la cle d'ESSAI ($($cles[0])) : signe-le avec la vraie cle"
    }
    if (-not (@(Get-XpgClesCompilees -Racine $Racine -Nom 'PublisherKey.hpp') -contains $cles[0])) {
        return "$Rel : $Sujet est $Signe par la cle $($cles[0]), que src/licensing/PublisherKey.hpp ne contient pas : les programmes ne pourraient pas le verifier"
    }
    return $null
}

function Test-XpgLivraison {
    # Les ecarts trouves dans le dossier a livrer : des chaines « chemin relatif : ce qui est trouve » ; vide : rien.
    # 1.32.0 : -Obligatoires oui|non remplace [Licences] obligatoires de config.ini.
    param([string] $Dossier, [string] $Racine, [string] $Obligatoires = '')
    $ecarts = New-Object System.Collections.ArrayList
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    $base = (Get-Item -LiteralPath $Dossier).FullName.TrimEnd('\', '/')
    # 1.32.0 : [Licences] obligatoires = oui - PublisherKey.hpp doit contenir une vraie cle de l'editeur.
    if ($Obligatoires -eq '') { $Obligatoires = Get-XpgConfig -Section 'Licences' -Cle 'obligatoires' -Defaut 'non' }
    if ($Obligatoires.Trim().ToLowerInvariant() -eq 'oui' -and @(Get-XpgClesCompilees -Racine $Racine -Nom 'PublisherKey.hpp').Count -eq 0) {
        [void]$ecarts.Add("src\licensing\PublisherKey.hpp : aucune cle de l'editeur alors que [Licences] obligatoires = oui (outils/config.ini) : les programmes livres auraient les licences non activees (tout ouvert) ; exporter-cle la vraie cle, ou obligatoires = non")
    }
    # Le registre et (1.32.0) la liste de revocation livres : signes par une cle des programmes, jamais par la cle d'essai.
    $e = Test-XpgDocumentSigne -Base $base -Racine $Racine -Rel 'licences\registre.xpgreg' -Genre 'registre' `
                               -Sujet 'le registre livre' -PasSigne 'un registre signe' -Signe 'signe'
    if ($e) { [void]$ecarts.Add($e) }
    $e = Test-XpgDocumentSigne -Base $base -Racine $Racine -Rel 'licences\revocations.xpgrev' -Genre 'revocations' `
                               -Sujet 'la liste de revocation livree' -PasSigne 'une liste de revocation signee' -Signe 'signee'
    if ($e) { [void]$ecarts.Add($e) }
    # 1.32.0 : licences\ n'admet que le registre, la liste de revocation et les textes de licence des bibliotheques.
    $licDir = Join-Path $base 'licences'
    if (Test-Path -LiteralPath $licDir -PathType Container) {
        $licBase = (Get-Item -LiteralPath $licDir).FullName.TrimEnd('\', '/')
        foreach ($f in Get-ChildItem -LiteralPath $licDir -Recurse -File -Force | Sort-Object FullName) {
            $r = $f.FullName.Substring($licBase.Length + 1)
            $permis = $false
            foreach ($p in $script:XpgLicencesPermis) { if ($r -like $p) { $permis = $true; break } }
            if (-not $permis) { [void]$ecarts.Add("licences\$r : licences\ n'admet que registre.xpgreg, revocations.xpgrev et les textes de licence des bibliotheques") }
        }
    }
    # 1.32.0 : un dossier de l'outil Admin (son archive), a aucun niveau.
    foreach ($d in Get-ChildItem -LiteralPath $base -Recurse -Directory -Force | Sort-Object FullName) {
        if ($script:XpgAdminDossiers -contains $d.Name.ToLowerInvariant()) {
            [void]$ecarts.Add("$($d.FullName.Substring($base.Length + 1)) : un dossier de l'outil Admin (l'archive des licences signees)")
        }
    }
    # Les chaines : en ASCII, et en UTF-16 (petit-boutiste, comme Python « utf-16-le »).
    $chaines = New-Object System.Collections.ArrayList
    foreach ($c in $script:XpgAdminChaines) { [void]$chaines.Add(@($c[0], $c[1])) }
    foreach ($c in $script:XpgAdminChaines) { [void]$chaines.Add(@($latin1.GetString([System.Text.Encoding]::Unicode.GetBytes($c[0])), $c[1])) }
    # (une boucle, pas un pipeline : ForEach-Object deroulerait chaque paire)
    $motifs = New-Object System.Collections.ArrayList
    foreach ($m in $script:XpgAdminMotifs) {
        [void]$motifs.Add(@([System.Text.RegularExpressions.Regex]::new($m[0], [System.Text.RegularExpressions.RegexOptions]::CultureInvariant), $m[1]))
    }
    foreach ($f in Get-ChildItem -LiteralPath $base -Recurse -File -Force | Sort-Object FullName) {
        $rel = $f.FullName.Substring($base.Length + 1)
        $compact = $f.Name.ToLowerInvariant().Replace(' ', '-')
        foreach ($n in $script:XpgAdminNoms) {
            if (($compact -like $n) -or ($compact.Replace('-', '_') -like $n.Replace('-', '_'))) {
                [void]$ecarts.Add("$rel : un fichier de l'outil Admin ou une cle (nom $($f.Name))")
                break
            }
        }
        $texte = $latin1.GetString([System.IO.File]::ReadAllBytes($f.FullName))
        foreach ($c in $chaines) {
            if ($texte.IndexOf($c[0], [System.StringComparison]::Ordinal) -ge 0) { [void]$ecarts.Add("$rel : $($c[1])"); break }
        }
        foreach ($m in $motifs) {
            if ($m[0].IsMatch($texte)) { [void]$ecarts.Add("$rel : $($m[1])") }
        }
    }
    return @($ecarts)
}

function Get-XpgDllImportees {
    # Les DLL qu'un exe PE importe (la table d'importation) : pour le manifeste et repair.bat.
    param([string] $Chemin)
    $b = [System.IO.File]::ReadAllBytes($Chemin)
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    $nbSections = [BitConverter]::ToUInt16($b, $pe + 6)
    $tailleOpt = [BitConverter]::ToUInt16($b, $pe + 20)
    $opt = $pe + 24
    $magic = [BitConverter]::ToUInt16($b, $opt)
    $dirs = $opt + $(if ($magic -eq 0x20b) { 112 } else { 96 })
    $rvaImport = [BitConverter]::ToUInt32($b, $dirs + 8)
    $sections = $opt + $tailleOpt
    function RvaVersFichier([uint32] $rva) {
        for ($i = 0; $i -lt $nbSections; $i++) {
            $s = $sections + 40 * $i
            $va = [BitConverter]::ToUInt32($b, $s + 12); $taille = [BitConverter]::ToUInt32($b, $s + 16); $brut = [BitConverter]::ToUInt32($b, $s + 20)
            $vtaille = [BitConverter]::ToUInt32($b, $s + 8); if ($vtaille -gt $taille) { $taille = $vtaille }
            if ($rva -ge $va -and $rva -lt ($va + $taille)) { return [int]($rva - $va + $brut) }
        }
        return -1
    }
    $noms = New-Object System.Collections.ArrayList
    if ($rvaImport -eq 0) { return @() }
    $off = RvaVersFichier $rvaImport
    while ($off -gt 0 -and $off + 20 -le $b.Length) {
        $rvaNom = [BitConverter]::ToUInt32($b, $off + 12)
        if ($rvaNom -eq 0) { break }
        $o = RvaVersFichier $rvaNom
        if ($o -lt 0) { break }
        $fin = $o; while ($fin -lt $b.Length -and $b[$fin] -ne 0) { $fin++ }
        [void]$noms.Add([System.Text.Encoding]::ASCII.GetString($b, $o, $fin - $o))
        $off += 20
    }
    return @($noms)
}

try {
    $racine = $X.Racine
    $produit = Get-XpgConfig -Section 'Produit' -Cle 'nom' -Obligatoire
    $exeNom = Get-XpgConfig -Section 'Produit' -Cle 'exe' -Defaut 'API-Studio.exe'
    $exeIhmNom = Get-XpgConfig -Section 'Produit' -Cle 'exe_ihm' -Defaut 'IHM-Studio.exe'   # 1.12.0 : IHM Studio
    $chaine = (Get-XpgOption -Noms @('chaine') -Defaut 'msvc').ToLowerInvariant()
    $exeFourni = Get-XpgOption -Noms @('exe')

    Start-XpgEtape 'Versions' | Out-Null
    $version = Test-XpgVersions
    if (-not $version) { Stop-Xpg -Code 2 -Message 'Versions incohérentes.' -Conseil 'configure.bat dit où les corriger.' }
    Complete-XpgEtape -Statut OK -Detail $version

    # Les outils d'abord, pour que les questions viennent au début : sans compilateur, build.ps1
    # pose la question du cahier des charges (six choix) et l'installation apporte aussi Inno
    # Setup ; avec un compilateur mais sans Inno Setup, on propose de l'installer ici.
    if (-not (Test-XpgOption @('sans-installateur'))) {
        $inno = Find-XpgInnoSetup
        $msvcLa = $null
        if (-not $exeFourni) { $msvcLa = Get-XpgChaineMsvc }
        if (-not $inno.Trouve -and ($exeFourni -or $msvcLa)) {
            Start-XpgEtape 'Inno Setup (pour l''installateur)' | Out-Null
            Invoke-XpgEnfant -Script 'install_build_tools.ps1' -Args2 @('/inno-seulement', '/depuis:package') -Libelle 'L''installation d''Inno Setup'
            Complete-XpgEtape -Statut OK
        }
    }

    if (-not $exeFourni) {
        Start-XpgEtape 'Compilation Release' | Out-Null
        Invoke-XpgEnfant -Script 'build.ps1' -Args2 @('-Action', 'build', '-Configuration', 'Release') -Libelle 'La compilation'
        Update-XpgConfig
        Complete-XpgEtape -Statut OK
    }
    if (-not (Test-XpgOption @('sans-tests'))) {
        Start-XpgEtape 'Tests' | Out-Null
        Invoke-XpgEnfant -Script 'test_build.ps1' -Args2 @('/sans-application') -Libelle 'Les tests'
        Complete-XpgEtape -Statut OK
    } else {
        Start-XpgEtape 'Tests' | Out-Null
        Complete-XpgEtape -Statut IGNORE -Detail '/sans-tests'
    }

    # ---- le dossier a livrer ------------------------------------------------------------
    Start-XpgEtape 'Le dossier à livrer (dist\staging)' | Out-Null
    $dist = Join-Path $racine (Get-XpgConfig -Section 'Installateur' -Cle 'sortie' -Defaut 'dist')
    $st = Join-Path $dist 'staging'
    if (Test-Path -LiteralPath $st) { Remove-Item -LiteralPath $st -Recurse -Force }
    New-Item -ItemType Directory -Path $st -Force | Out-Null
    $exe = $exeFourni
    if (-not $exe) { $exe = Join-Path $racine "build\Release\$exeNom" }
    if (-not (Test-Path -LiteralPath $exe)) { Stop-Xpg -Code 1 -Message "Exe introuvable : $exe" }
    Copy-Item -LiteralPath $exe -Destination (Join-Path $st $exeNom) -Force
    # 1.12.0 : IHM Studio, a cote (le meme dossier que l'exe de API Studio).
    $exeIhm = Join-Path (Split-Path -Parent $exe) $exeIhmNom
    if (-not (Test-Path -LiteralPath $exeIhm)) { Stop-Xpg -Code 1 -Message "Exe introuvable : $exeIhm" -Conseil 'build.bat compile les deux applications (API Studio et IHM Studio).' }
    Copy-Item -LiteralPath $exeIhm -Destination (Join-Path $st $exeIhmNom) -Force
    $sdl = Get-XpgOption -Noms @('sdl')
    if (-not $sdl) { $sdl = Join-Path $racine 'third_party\SDL3\lib\x64\SDL3.dll' }
    if (-not (Test-Path -LiteralPath $sdl)) { Stop-Xpg -Code 1 -Message "SDL3.dll introuvable : $sdl" }
    Copy-Item -LiteralPath $sdl -Destination (Join-Path $st 'SDL3.dll') -Force
    # 1.12.30 : PDFium, le moteur PDF de la visionneuse de documents (charge a la demande), a cote des exe.
    $pdfium = Get-XpgOption -Noms @('pdfium')
    if (-not $pdfium) { $pdfium = Join-Path $racine 'third_party\pdfium\win-x64\pdfium.dll' }
    if (-not (Test-Path -LiteralPath $pdfium)) { Stop-Xpg -Code 1 -Message "pdfium.dll introuvable : $pdfium" -Conseil 'python outils\pdfium\obtenir_pdfium.py le pose (third_party\pdfium\ORIGINE.txt).' }
    Copy-Item -LiteralPath $pdfium -Destination (Join-Path $st 'pdfium.dll') -Force
    $runtime = @()
    if ($chaine -eq 'msvc') {
        $msvc = Get-XpgChaineMsvc
        $rt = $null
        if ($msvc) { $rt = Find-XpgRuntimeVc -Instance $msvc.Instance.Chemin }
        if (-not $rt -or -not $rt.Dll) { Stop-Xpg -Code 1 -Message 'Le runtime Visual C++ à livrer est introuvable (VC\Redist\MSVC\...\x64\Microsoft.VC143.CRT).' -Conseil 'Il vient avec les Build Tools : check_environment.bat le vérifie.' }
        foreach ($d in $rt.Dll) { Copy-Item -LiteralPath $d -Destination $st -Force; $runtime += [System.IO.Path]::GetFileName($d) }
        Write-XpgLog -Niveau INFO -Message ("Runtime Visual C++ (déploiement local) : {0}" -f ($runtime -join ', '))
    } else {
        Write-XpgLog -Niveau INFO -Message "Chaîne $chaine : pas de runtime Visual C++ à livrer (runtime C++ lié statiquement)."
    }
    $nRes = 0
    foreach ($f in @('schneider_library.txt', 'plc_catalog.txt')) {
        $s = Join-Path $racine "resources\$f"
        if (-not (Test-Path -LiteralPath $s)) { Stop-Xpg -Code 1 -Message "Ressource absente : $s" }
        New-Item -ItemType Directory -Path (Join-Path $st 'resources') -Force | Out-Null
        Copy-Item -LiteralPath $s -Destination (Join-Path $st "resources\$f") -Force; $nRes++
    }
    $nRes += Copy-XpgArbre -De (Join-Path $racine 'resources\fonts') -Vers (Join-Path $st 'resources\fonts')
    $nLibs = Copy-XpgArbre -De (Join-Path $racine 'libs') -Vers (Join-Path $st 'libs') -Exclure @('*.bak', '*~', '~$*')
    # La maintenance : les six scripts et ce dont ils ont besoin (pas ceux de la compilation).
    $m = Join-Path $st 'maintenance'
    New-Item -ItemType Directory -Path (Join-Path $m 'lib') -Force | Out-Null
    foreach ($b in @('repair', 'verify_installation', 'restore_backup', 'resume_installation', 'reset_user_settings', 'collect_diagnostics')) {
        Copy-Item -LiteralPath (Join-Path $X.Outils "$b.bat") -Destination $m -Force
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "$b.ps1") -Destination (Join-Path $m 'lib') -Force
    }
    foreach ($f in @('XpgCommun.psm1', 'XpgInstallation.psm1', 'installer_etape.ps1', 'empreintes-livrees.txt')) {
        $s = Join-Path $PSScriptRoot $f
        if (Test-Path -LiteralPath $s) { Copy-Item -LiteralPath $s -Destination (Join-Path $m 'lib') -Force }
    }
    # config.ini de la maintenance : ce qu'elle doit savoir du produit (pas les outils de compilation).
    $ini = @"
; maintenance\config.ini - ecrit par package.bat ($((Get-Date).ToString('yyyy-MM-dd HH:mm'))) depuis outils\config.ini.
; Ce que les scripts de maintenance doivent savoir de XPGAnalyser. Ne pas modifier.
[Produit]
nom = $produit
cle = $(Get-XpgConfig -Section 'Produit' -Cle 'cle' -Defaut 'XPGAnalyser')
editeur = $(Get-XpgConfig -Section 'Produit' -Cle 'editeur')
version = $version
app_id = $(Get-XpgConfig -Section 'Produit' -Cle 'app_id' -Obligatoire)
exe = $exeNom
exe_ihm = $exeIhmNom
chaine = $chaine

[Maintenance]
sauvegardes_a_garder = $(Get-XpgConfig -Section 'Maintenance' -Cle 'sauvegardes_a_garder' -Defaut '3')
anciens_dossiers = $(Get-XpgConfig -Section 'Installateur' -Cle 'anciens_dossiers')
"@
    Write-XpgTexte -Chemin (Join-Path $m 'config.ini') -Texte ($ini -replace "`r?`n", "`r`n")
    # Les licences des bibliotheques tierces.
    $lic = Join-Path $st 'licences'
    New-Item -ItemType Directory -Path $lic -Force | Out-Null
    # 1.30.0 : le registre des editions et fonctionnalites, signe par l'editeur avec sa vraie cle (docs\LICENCES.md) :
    # installe dans {app}\licences\registre.xpgreg ; les programmes l'adoptent s'il est plus recent que celui du PC.
    $registre = Join-Path $racine 'ressources-licences\registre.xpgreg'
    if (Test-Path -LiteralPath $registre -PathType Leaf) {
        Copy-Item -LiteralPath $registre -Destination (Join-Path $lic 'registre.xpgreg') -Force
        Write-XpgLog -Niveau INFO -Message 'Registre des licences : ressources-licences\registre.xpgreg -> licences\registre.xpgreg'
    } else {
        Write-XpgLog -Niveau INFO -Message 'Registre des licences : ressources-licences\registre.xpgreg absent - aucun registre livré (il sera signé par l''éditeur avec sa vraie clé).'
    }
    # 1.32.0 : la liste de revocation signee par l'editeur, s'il y en a une : adoptee si elle est plus recente.
    $revocations = Join-Path $racine 'ressources-licences\revocations.xpgrev'
    if (Test-Path -LiteralPath $revocations -PathType Leaf) {
        Copy-Item -LiteralPath $revocations -Destination (Join-Path $lic 'revocations.xpgrev') -Force
        Write-XpgLog -Niveau INFO -Message 'Liste de révocation : ressources-licences\revocations.xpgrev -> licences\revocations.xpgrev'
    }
    Copy-Item -LiteralPath (Join-Path $racine 'third_party\SDL3\LICENSE.txt') -Destination (Join-Path $lic 'SDL3-LICENSE.txt') -Force
    foreach ($f in Get-ChildItem -LiteralPath (Join-Path $racine 'third_party') -Filter '*.LICENSE' -File) { Copy-Item -LiteralPath $f.FullName -Destination $lic -Force }
    # 1.12.30 : les licences de PDFium et de sa construction (le dossier LICENSES a cote de pdfium.dll).
    $licPdfium = Join-Path (Split-Path -Parent $pdfium) 'LICENSES'
    if (-not (Test-Path -LiteralPath $licPdfium)) { Stop-Xpg -Code 1 -Message "Les licences de PDFium manquent : $licPdfium" -Conseil 'python outils\pdfium\obtenir_pdfium.py les pose avec pdfium.dll.' }
    $licP = Join-Path $lic 'pdfium'
    New-Item -ItemType Directory -Path $licP -Force | Out-Null
    foreach ($f in Get-ChildItem -LiteralPath $licPdfium -File) { Copy-Item -LiteralPath $f.FullName -Destination $licP -Force }
    $stb = [System.IO.File]::ReadAllText((Join-Path $racine 'third_party\stb_image.h'))
    $i = $stb.IndexOf('This software is available under 2 licenses')
    if ($i -ge 0) { Write-XpgTexte -Chemin (Join-Path $lic 'stb.LICENSE') -Texte ($stb.Substring($i).Replace('*/', '').Trim() + "`r`n") -SansBom }
    $lisez = Join-Path $racine 'installateur\LISEZ-MOI.txt'
    if (Test-Path -LiteralPath $lisez) {
        $t = [System.IO.File]::ReadAllText($lisez, [System.Text.Encoding]::UTF8).Replace('{VERSION}', $version)
        Write-XpgTexte -Chemin (Join-Path $st 'LISEZ-MOI.txt') -Texte (($t -replace "`r?`n", "`n") -replace "`n", "`r`n")
    }
    Complete-XpgEtape -Statut OK -Detail ("exe, SDL3.dll, pdfium.dll, {0} DLL du runtime, {1} ressource(s), {2} fichier(s) de bibliothèque, 6 scripts de maintenance" -f $runtime.Count, $nRes, $nLibs)

    # ---- 1.30.0 : le controle du paquet (le meme que package_mingw.py) ------------------------------
    Start-XpgEtape 'Le contrôle du paquet (rien de l''outil Admin des licences)' | Out-Null
    $ecarts = @(Test-XpgLivraison -Dossier $st -Racine $racine)
    if ($ecarts.Count -gt 0) {
        foreach ($e in $ecarts) { Write-XpgLog -Niveau ERREUR -Message $e }
        Stop-Xpg -Code 1 -Message ("CONTROLE DU PAQUET : construction arrêtée, {0} écart(s) dans {1} :`r`n  {2}" -f $ecarts.Count, $st, ($ecarts -join "`r`n  ")) `
                 -Conseil 'L''outil Admin des licences, sa clé et une clé d''essai ne se livrent jamais (docs\LICENCES.md).'
    }
    Complete-XpgEtape -Statut OK -Detail "$st - rien de l'outil Admin, aucune clé privée, aucun jeton"

    # ---- le manifeste ------------------------------------------------------------------------
    Start-XpgEtape 'Le manifeste (tailles et SHA-256)' | Out-Null
    $dll = @(Get-XpgDllImportees (Join-Path $st $exeNom))
    $systeme = @('kernel32', 'user32', 'gdi32', 'shell32', 'ole32', 'oleaut32', 'advapi32', 'comdlg32', 'ws2_32', 'iphlpapi', 'winmm', 'imm32', 'setupapi', 'version',
                 'dwmapi', 'uxtheme', 'shlwapi', 'bcrypt', 'winspool', 'uuid', 'msvcrt', 'ucrtbase', 'crypt32', 'dbghelp', 'hid', 'cfgmgr32', 'dinput8', 'xinput1_4', 'powrprof')
    $aLivrer = @($dll | Where-Object { $n = $_.ToLowerInvariant() -replace '\.dll$', ''; -not ($systeme -contains $n) -and $n -notlike 'api-ms-win-*' })
    foreach ($d in $aLivrer) { if (-not (Test-Path -LiteralPath (Join-Path $st $d))) { Stop-Xpg -Code 1 -Message "L'exe demande $d, qui n'est pas dans le dossier à livrer." -Conseil 'Un PC vierge ne l''aurait pas : voir le runtime (package.bat) et SDL3.dll.' } }
    $fichiers = New-Object System.Collections.ArrayList
    $base = (Get-Item -LiteralPath $st).FullName.TrimEnd('\', '/')
    foreach ($f in Get-ChildItem -LiteralPath $st -Recurse -File | Sort-Object FullName) {
        $rel = $f.FullName.Substring($base.Length + 1).Replace('/', '\')
        $role = 'programme'
        if ($rel -like 'resources\*' -or $rel -like 'libs\*') { $role = 'donnee-livree' }
        elseif ($rel -like 'maintenance\*') { $role = 'maintenance' }
        elseif ($rel -like 'licences\*' -or $rel -like '*.txt') { $role = 'document' }
        [void]$fichiers.Add([ordered]@{ chemin = $rel; taille = $f.Length; sha256 = (Get-XpgSha256 $f.FullName); role = $role })
    }
    $manifeste = [ordered]@{
        produit = $produit; version = $version; chaine = $chaine; date = (Get-Date).ToString('s')
        exe = $exeNom; exe_ihm = $exeIhmNom; dependances = @($aLivrer); runtime_local = @($runtime)
        fichiers = @($fichiers)
    }
    Write-XpgTexte -Chemin (Join-Path $st 'manifeste.json') -Texte ($manifeste | ConvertTo-Json -Depth 5) -SansBom
    $total = ($fichiers | ForEach-Object { $_.taille } | Measure-Object -Sum).Sum
    Complete-XpgEtape -Statut OK -Detail ("{0} fichiers, {1} ; DLL à livrer : {2}" -f $fichiers.Count, (Format-XpgTaille $total), ($aLivrer -join ', '))

    if (Test-XpgOption @('sans-installateur')) {
        Start-XpgEtape 'Installateur' | Out-Null
        Complete-XpgEtape -Statut IGNORE -Detail '/sans-installateur'
        Exit-Xpg -Code 0
    }
    Start-XpgEtape 'L''installateur (Inno Setup)' | Out-Null
    Invoke-XpgEnfant -Script 'build_installer.ps1' -Args2 @("/staging:$st", "/chaine:$chaine") -Libelle 'La fabrication de l''installateur'
    Complete-XpgEtape -Statut OK -Detail (Join-Path $racine ("{0}-Setup-{1}.exe" -f (Get-XpgConfig -Section 'Produit' -Cle 'fichier' -Defaut 'API-IHM-Studio'), $version))
    Exit-Xpg -Code 0
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

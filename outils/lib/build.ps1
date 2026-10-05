#Requires -Version 5.1
# =============================================================================
#  build.ps1 - lance par build.bat, build_debug.bat, rebuild.bat, clean.bat,
#  configure.bat (-Action) : compiler XPGAnalyser SANS OUVRIR Visual Studio.
#
#    MSBuild (celui des Build Tools) sur XpgAnalyzer.sln, x64 : le meme projet,
#    le meme compilateur (v143), la meme ABI et le meme runtime que dans
#    Visual Studio. Sortie : build\Release\XpgAnalyzer.exe (ou build\Debug).
#
#  S'il n'y a pas de compilateur : le message du cahier des charges et ses six
#  choix (install_build_tools.ps1) ; apres l'installation, la compilation
#  reprend toute seule.
# =============================================================================
param(
    [ValidateSet('build', 'rebuild', 'clean', 'configure')] [string] $Action = 'build',
    [ValidateSet('Release', 'Debug')] [string] $Configuration = 'Release',
    [Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments
)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgOutils.psm1') -Force -Global -DisableNameChecking

$noms = @{ build = 'build'; rebuild = 'rebuild'; clean = 'clean'; configure = 'configure' }
$nom = $noms[$Action]
if ($Action -eq 'build' -and $Configuration -eq 'Debug') { $nom = 'build_debug' }
$titres = @{
    build = "XPGAnalyser - compilation $Configuration (MSBuild, sans Visual Studio)"
    build_debug = 'XPGAnalyser - compilation Debug (MSBuild, sans Visual Studio)'
    rebuild = "XPGAnalyser - recompilation complète $Configuration (nettoyage puis compilation)"
    clean = 'XPGAnalyser - suppression des éléments générés'
    configure = 'XPGAnalyser - configuration initiale'
}
$usage = @'
build.bat          compilation Release   -> build\Release\XpgAnalyzer.exe
build_debug.bat    compilation Debug     -> build\Debug\XpgAnalyzer.exe
rebuild.bat        clean puis build (Release ; /debug pour Debug)
clean.bat          supprime build\ et dist\staging\ (/tout : tout dist\ ; l'installateur de la racine reste)
configure.bat      vérifie config.ini, les outils, la version, prépare les dossiers
Options : /debug (rebuild) ; /tout (clean) ; /choix:1..6 (la réponse au menu des outils,
          s'il n'y a pas de compilateur) ; /oui ; /non-interactif ; /journal:<fichier> ; /details
'@
$X = Initialize-Xpg -Nom $nom -Titre $titres[$nom] -Arguments $Arguments -Usage $usage
if ($Action -eq 'rebuild' -and (Test-XpgOption @('debug'))) { $Configuration = 'Debug' }
# Pour une reprise apres un redemarrage (installation des outils) : relancer exactement ceci.
if ($Action -eq 'build' -or $Action -eq 'rebuild') { Save-XpgOrigine -Script 'build.ps1' -Arguments (@('-Action', $Action, '-Configuration', $Configuration) + @($Arguments)) }

function Remove-XpgGenere {
    param([switch] $Tout)
    $cibles = @((Join-Path $X.Racine 'build'), (Join-Path $X.Racine 'dist\staging'))
    if ($Tout) { $cibles += (Join-Path $X.Racine 'dist') }
    $n = 0
    $racine = [System.IO.Path]::GetFullPath($X.Racine).TrimEnd('\', '/')
    foreach ($c in $cibles) {
        if (Test-Path -LiteralPath $c) {
            # Garde-fou : seulement les dossiers generes, juste sous la racine du projet (le chemin
            # AU-DESSUS de la racine peut contenir src\ ou projets\ : il ne compte pas).
            $plein = [System.IO.Path]::GetFullPath($c).TrimEnd('\', '/')
            $rel = ''
            if ($plein.Length -gt $racine.Length -and $plein.StartsWith($racine, [StringComparison]::OrdinalIgnoreCase)) { $rel = $plein.Substring($racine.Length + 1).Replace('/', '\') }
            if (@('build', 'dist', 'dist\staging') -notcontains $rel) {
                Stop-Xpg -Code 1 -Message "Refus de supprimer $plein (hors des dossiers générés)."
            }
            Write-XpgLog -Niveau INFO -Message "Suppression : $plein"
            Remove-Item -LiteralPath $plein -Recurse -Force
            $n++
        }
    }
    return $n
}

function Get-XpgCompilateurOuInstaller {
    # Le compilateur ; sinon le message et les six choix (install_build_tools.ps1, dans un
    # processus a part : son journal, son resume) ; puis la detection recommence.
    $msvc = Get-XpgChaineMsvc
    if ($msvc) { return $msvc }
    Write-XpgLog -Niveau ATTENTION -Message 'Aucun compilateur C++ compatible n''a été détecté sur cet ordinateur.'
    # Qui reprendre apres un redemarrage : package, test_build (qui nous ont lance), ou nous.
    $origine = Get-XpgOption -Noms @('origine') -Defaut $nom
    Write-XpgLog -Niveau INFO -Message 'Lancement de install_build_tools (installation assistée, avec ton accord)...'
    $code = Start-XpgScriptEnfant -Script 'install_build_tools.ps1' -Arguments @('/depuis:build') -Origine $origine
    Write-XpgDetail "install_build_tools : code $code"
    if ($code -eq 3010) {
        Stop-Xpg -Code 3010 -Message 'Un redémarrage de Windows est nécessaire pour finir l''installation des outils.' `
                 -Conseil "L'état est enregistré : après le redémarrage, à l'ouverture de session, la procédure reprend toute seule et relance $origine.bat (ou relance-le toi-même)."
    }
    if ($code -ne 0) {
        Stop-Xpg -Code 10 -Message "Pas de compilateur : l'installation n'a pas été faite (code $code)." -Conseil 'Voir le journal de install_build_tools dans outils\journaux\.'
    }
    Update-XpgPathDepuisRegistre
    Update-XpgConfig
    $msvc = Get-XpgChaineMsvc
    if (-not $msvc) { Stop-Xpg -Code 10 -Message 'Les outils installés ne sont toujours pas détectés.' -Conseil 'Lance check_environment.bat et joins son journal (collect_diagnostics.bat).' }
    Write-XpgLog -Niveau OK -Message 'Compilateur installé et détecté : la compilation reprend.'
    return $msvc
}

try {
    switch ($Action) {
        'clean' {
            Start-XpgEtape 'Suppression de build\ et dist\staging\' | Out-Null
            $n = Remove-XpgGenere -Tout:(Test-XpgOption @('tout'))
            Complete-XpgEtape -Statut OK -Detail "$n dossier(s) supprimé(s) ; sources, projets, bibliothèque et données jamais touchés"
            Exit-Xpg -Code 0
        }
        'configure' {
            Start-XpgEtape 'config.ini' | Out-Null
            $manques = @()
            foreach ($s in $X.Config.Keys) { foreach ($k in $X.Config[$s].Keys) { if ([string]$X.Config[$s][$k] -match '_RENSEIGNER') { $manques += "[$s] $k" } } }
            if ($manques) { Complete-XpgEtape -Statut ATTENTION -Detail ('à renseigner (seulement si tu t''en sers) : ' + ($manques -join ', ')) }
            else { Complete-XpgEtape -Statut OK -Detail 'rien à renseigner' }
            Start-XpgEtape 'Version du produit' | Out-Null
            $v = Test-XpgVersions
            if (-not $v) { Complete-XpgEtape -Statut ECHEC; Stop-Xpg -Code 2 -Message 'Versions incohérentes.' -Conseil 'Mets la même version dans config.ini, CMakeLists.txt, src\core\Version.hpp et resources\windows\xpg_analyzer.rc.' }
            Complete-XpgEtape -Statut OK -Detail "$v partout"
            Start-XpgEtape 'Dossiers de travail' | Out-Null
            foreach ($d in @('outils\journaux', 'build', 'dist')) { New-Item -ItemType Directory -Path (Join-Path $X.Racine $d) -Force | Out-Null }
            Complete-XpgEtape -Statut OK -Detail 'outils\journaux, build, dist'
            Start-XpgEtape 'Le projet Visual Studio' | Out-Null
            $manquants = @()
            foreach ($cle in @('solution', 'vcxproj')) {
                $f = Get-XpgConfig -Section 'Projet' -Cle $cle -Defaut ''
                if ($f -and -not (Test-Path -LiteralPath (Join-Path $X.Racine $f))) { $manquants += $f }
            }
            if ($manquants) { Complete-XpgEtape -Statut ECHEC -Detail ('introuvable : ' + ($manquants -join ', ')); Stop-Xpg -Code 1 -Message 'Le projet Visual Studio est incomplet.' -Conseil 'Extrais le zip en entier ; config.ini, [Projet], donne les noms attendus.' }
            Complete-XpgEtape -Statut OK -Detail ("{0}, {1}" -f (Get-XpgConfig -Section 'Projet' -Cle 'solution' -Defaut 'XpgAnalyzer.sln'), (Get-XpgConfig -Section 'Projet' -Cle 'vcxproj' -Defaut 'XpgAnalyzer.vcxproj'))
            Start-XpgEtape 'Outils de compilation' | Out-Null
            $msvc = Get-XpgChaineMsvc
            if ($msvc) { Complete-XpgEtape -Statut OK -Detail ("MSVC {0}, MSBuild {1}" -f $msvc.Msvc.VersionOutils, (Get-XpgVersionMsBuild $msvc.MsBuild)) }
            else { Complete-XpgEtape -Statut ATTENTION -Detail 'aucun : lance install_build_tools.bat (ou build.bat, qui le propose)' }
            Start-XpgEtape 'Inno Setup' | Out-Null
            $inno = Find-XpgInnoSetup
            if ($inno.Trouve) { Complete-XpgEtape -Statut OK -Detail "$($inno.Version) : $($inno.Chemin)" } else { Complete-XpgEtape -Statut ATTENTION -Detail 'absent : install_build_tools.bat l''installe (pour package.bat)' }
            Exit-Xpg -Code 0
        }
    }

    # ---- build / rebuild --------------------------------------------------------------
    Start-XpgEtape 'Version' | Out-Null
    $version = Test-XpgVersions
    if (-not $version) { Stop-Xpg -Code 2 -Message 'Versions incohérentes (voir ci-dessus).' -Conseil 'configure.bat dit où les corriger.' }
    Complete-XpgEtape -Statut OK -Detail $version

    # Les valeurs de config.ini que seule la chaine msvc x64 accepte : AVANT le nettoyage et
    # avant une eventuelle installation des outils (plusieurs Go).
    $chaineCfg = (Get-XpgConfig -Section 'Compilateur' -Cle 'chaine' -Defaut 'msvc').ToLowerInvariant()
    if ($chaineCfg -ne 'msvc') {
        Stop-Xpg -Code 3 -Message "config.ini demande la chaîne « $chaineCfg » : build.bat n'automatise que msvc." -Conseil 'Les autres chaînes : docs\RAPPORT-ANALYSE.md, § 6 ; remets [Compilateur] chaine = msvc.'
    }
    $plateforme = Get-XpgConfig -Section 'Projet' -Cle 'plateforme' -Defaut 'x64'
    if ($plateforme -ne 'x64') { Stop-Xpg -Code 3 -Message "config.ini demande la plateforme « $plateforme » : seule x64 existe (le SDL3 fourni, l'installateur)." -Conseil 'Remets [Projet] plateforme = x64.' }

    if ($Action -eq 'rebuild') {
        Start-XpgEtape 'Nettoyage' | Out-Null
        $n = Remove-XpgGenere
        Complete-XpgEtape -Statut OK -Detail "$n dossier(s)"
    }

    Start-XpgEtape 'Compilateur' | Out-Null
    $msvc = Get-XpgCompilateurOuInstaller
    Complete-XpgEtape -Statut OK -Detail ("MSVC {0} ({1})" -f $msvc.Msvc.VersionOutils, $msvc.Instance.Chemin)

    Start-XpgEtape "MSBuild $Configuration x64" | Out-Null
    Import-XpgEnvironnementVs -Instance $msvc.Instance.Chemin
    $sln = Join-Path $X.Racine (Get-XpgConfig -Section 'Projet' -Cle 'solution' -Defaut 'XpgAnalyzer.sln')
    if (-not (Test-Path -LiteralPath $sln)) { Stop-Xpg -Code 1 -Message "Solution introuvable : $sln" }
    $cible = 'Build'; if ($Action -eq 'rebuild') { $cible = 'Rebuild' }
    $code = Invoke-XpgProcessus -Fichier $msvc.MsBuild -Arguments @($sln, "/t:$cible", "/p:Configuration=$Configuration", "/p:Platform=$plateforme", '/m', '/nologo', '/v:minimal', '/nr:false', '/clp:Summary') `
            -Dossier $X.Racine -Afficher -Libelle 'MSBuild'
    if ($code -ne 0) {
        Stop-Xpg -Code 1 -Message "La compilation a échoué (MSBuild, code $code)." -Conseil "Les erreurs sont ci-dessus et dans le journal ($($X.Journal)). Une erreur MSVC dans le code : envoie ce journal."
    }
    $exe = Join-Path $X.Racine ("build\{0}\{1}" -f $Configuration, (Get-XpgConfig -Section 'Produit' -Cle 'exe' -Defaut 'XpgAnalyzer.exe'))
    if (-not (Test-Path -LiteralPath $exe)) { Stop-Xpg -Code 1 -Message "MSBuild dit OK mais l'exe est absent : $exe" }
    Complete-XpgEtape -Statut OK -Detail ("{0} ({1}, {2})" -f $exe, (Format-XpgTaille (Get-Item -LiteralPath $exe).Length), (Get-XpgArchitecturePe $exe))

    Start-XpgEtape 'Vérification de l''exe (--version)' | Out-Null
    $sdl = Join-Path (Split-Path -Parent $exe) 'SDL3.dll'
    if (-not (Test-Path -LiteralPath $sdl)) { Complete-XpgEtape -Statut ECHEC -Detail 'SDL3.dll absent à côté de l''exe'; Stop-Xpg -Code 1 -Message 'SDL3.dll n''a pas été copié (événement après génération du projet).' }
    $sortie = Join-Path ([System.IO.Path]::GetTempPath()) ("xpg-version-{0}.txt" -f $PID)
    $p = Start-Process -FilePath $exe -ArgumentList '--version' -Wait -PassThru -NoNewWindow -RedirectStandardOutput $sortie
    $texte = ''
    if (Test-Path -LiteralPath $sortie) { $texte = ([System.IO.File]::ReadAllText($sortie)).Trim(); Remove-Item -LiteralPath $sortie -Force -ErrorAction SilentlyContinue }
    if ($p.ExitCode -ne 0 -or $texte -notmatch [regex]::Escape($version)) {
        Complete-XpgEtape -Statut ECHEC -Detail "code $($p.ExitCode), sortie « $texte »"
        Stop-Xpg -Code 1 -Message 'L''exe compilé ne démarre pas correctement.' -Conseil 'Une DLL manque souvent (SDL3.dll, runtime) : voir le journal.'
    }
    Complete-XpgEtape -Statut OK -Detail $texte
    Exit-Xpg -Code 0
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

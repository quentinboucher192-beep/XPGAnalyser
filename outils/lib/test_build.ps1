#Requires -Version 5.1
# =============================================================================
#  test_build.ps1 - lance par test_build.bat : compiler, puis lancer les tests
#
#   1. la compilation Release de l'application (MSBuild, comme build.bat) ;
#   2. les tests de CMakeLists.txt, compiles par CMake + Ninja avec le MEME
#      MSVC (vcvars64), sans SDL (ils n'ouvrent pas de fenetre) : build\cmake-tests ;
#   3. ctest : chaque test, son resultat ; le bilan.
#  Code : 0 tout passe ; 1 un test ou une compilation echoue.
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgOutils.psm1') -Force -Global -DisableNameChecking

$usage = @'
test_build.bat [options]
  /sans-application  ne recompile pas l'application (seulement les tests)
  /filtre:<regex>    ne lance que les tests dont le nom correspond (ctest -R)
  /propre            repart d'un dossier build\cmake-tests vide
'@
$X = Initialize-Xpg -Nom 'test_build' -Titre 'XPGAnalyser - compilation et tests' -Arguments $Arguments -Usage $usage
Save-XpgOrigine -Script 'test_build.ps1' -Arguments @($Arguments)

try {
    if (-not (Test-XpgOption @('sans-application'))) {
        Start-XpgEtape 'Compilation Release de l''application (build.bat)' | Out-Null
        # Sans compilateur, build.ps1 pose la question des six choix : elle reste une question.
        $origine = Get-XpgOption -Noms @('origine') -Defaut 'test_build'
        $code = Start-XpgScriptEnfant -Script 'build.ps1' -Arguments @('-Action', 'build', '-Configuration', 'Release') -Origine $origine
        if ($code -eq 3010) { Stop-Xpg -Code 3010 -Message 'Windows doit redémarrer pour finir l''installation des outils.' -Conseil "Après le redémarrage, la procédure reprend toute seule et relance $origine.bat." }
        if ($code -ne 0) { Stop-Xpg -Code $code -Message "La compilation de l'application a échoué (code $code)." -Conseil 'Voir le journal de build dans outils\journaux\.' }
        Update-XpgConfig
        Complete-XpgEtape -Statut OK
    }

    Start-XpgEtape 'Outils : MSVC, CMake, Ninja' | Out-Null
    $msvc = Get-XpgChaineMsvc
    if (-not $msvc) { Stop-Xpg -Code 10 -Message 'Aucun compilateur MSVC.' -Conseil 'Lance install_build_tools.bat (ou build.bat, qui le propose).' }
    Import-XpgEnvironnementVs -Instance $msvc.Instance.Chemin
    $cmake = Find-XpgCMake -Instance $msvc.Instance.Chemin
    $ninja = Find-XpgNinja -Instance $msvc.Instance.Chemin
    if (-not $cmake.Trouve -or -not $ninja.Trouve) {
        Stop-Xpg -Code 11 -Message 'CMake ou Ninja manque.' -Conseil 'Composant « C++ CMake tools for Windows » des Build Tools : install_build_tools.bat l''ajoute.'
    }
    Complete-XpgEtape -Statut OK -Detail ("MSVC {0} ; CMake {1} ; Ninja {2}" -f $msvc.Msvc.VersionOutils, $cmake.Version, $ninja.Version)

    $bd = Join-Path $X.Racine (Get-XpgConfig -Section 'Projet' -Cle 'cmake_tests' -Defaut 'build\cmake-tests')
    if ((Test-XpgOption @('propre')) -and (Test-Path -LiteralPath $bd)) { Remove-Item -LiteralPath $bd -Recurse -Force }

    Start-XpgEtape 'Configuration CMake (Ninja, Debug, sans SDL)' | Out-Null
    $code = Invoke-XpgProcessus -Fichier $cmake.Chemin -Arguments @('-G', 'Ninja', '-S', $X.Racine, '-B', $bd, '-DCMAKE_BUILD_TYPE=Debug',
        '-DXPG_WITH_SDL=OFF', '-DXPG_BUILD_TESTS=ON', "-DCMAKE_MAKE_PROGRAM=$($ninja.Chemin)", '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl') -Libelle 'cmake (configuration)'
    if ($code -ne 0) { Stop-Xpg -Code 1 -Message "La configuration CMake a échoué (code $code)." -Conseil "Voir le journal : $($X.Journal)" }
    Complete-XpgEtape -Statut OK -Detail $bd

    Start-XpgEtape 'Compilation des tests (Ninja)' | Out-Null
    $code = Invoke-XpgProcessus -Fichier $cmake.Chemin -Arguments @('--build', $bd, '--parallel', [string][Environment]::ProcessorCount) -Afficher -Libelle 'cmake --build'
    if ($code -ne 0) { Stop-Xpg -Code 1 -Message "La compilation des tests a échoué (code $code)." -Conseil "Les erreurs sont ci-dessus et dans le journal : $($X.Journal)" }
    Complete-XpgEtape -Statut OK

    Start-XpgEtape 'Tests (ctest)' | Out-Null
    $ctest = Join-Path (Split-Path -Parent $cmake.Chemin) 'ctest.exe'
    $a = @('--test-dir', $bd, '--output-on-failure', '-j', [string][Environment]::ProcessorCount)
    $filtre = Get-XpgOption -Noms @('filtre')
    if ($filtre) { $a += @('-R', $filtre) }
    $avant = (Get-Item -LiteralPath $X.Journal).Length
    $code = Invoke-XpgProcessus -Fichier $ctest -Arguments $a -Afficher -Libelle 'ctest'
    $bilan = ''
    try {
        $fin = [System.IO.File]::ReadAllText($X.Journal)
        $m = [regex]::Matches($fin, '(\d+)% tests passed, (\d+) tests failed out of (\d+)')
        if ($m.Count -gt 0) { $d = $m[$m.Count - 1]; $bilan = "{0} sur {1} réussis" -f ([int]$d.Groups[3].Value - [int]$d.Groups[2].Value), $d.Groups[3].Value }
    } catch { }
    if ($code -ne 0) { Complete-XpgEtape -Statut ECHEC -Detail $bilan; Stop-Xpg -Code 1 -Message 'Des tests échouent.' -Conseil 'Leur sortie est ci-dessus et dans le journal.' }
    Complete-XpgEtape -Statut OK -Detail $bilan
    Exit-Xpg -Code 0
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

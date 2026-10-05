#Requires -Version 5.1
# =============================================================================
#  check_environment.ps1 - lance par check_environment.bat
#  Cherche les outils (vswhere, instances, emplacements standards, registre,
#  variables, config.ini), puis ESSAIE le compilateur retenu : compiler, lier,
#  lancer, code de retour, architecture, nettoyage.
#  Code de retour : 0 pret ; 10 aucun compilateur utilisable ; 11 il manque
#  autre chose (SDK, Inno Setup...) ; 3 config.ini.
# =============================================================================
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Arguments)
Import-Module (Join-Path $PSScriptRoot 'XpgCommun.psm1') -Force -Global -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'XpgOutils.psm1') -Force -Global -DisableNameChecking

$usage = @'
check_environment.bat [options]
  Cherche les outils de compilation de XPGAnalyser et essaie le compilateur.
  /sans-essai     ne compile pas le programme d'essai (plus rapide, moins sûr)
  /json:<fichier> écrit aussi le résultat en JSON (pour un autre script)
'@
$X = Initialize-Xpg -Nom 'check_environment' -Titre 'XPGAnalyser - vérification de l''environnement de compilation' -Arguments $Arguments -Usage $usage

try {
    $resultat = [ordered]@{ date = (Get-Date).ToString('s'); chaine = ''; pret = $false; manquants = @(); presents = @() }
    $manquants = New-Object System.Collections.ArrayList
    $presents = New-Object System.Collections.ArrayList

    # ---- 1. Windows, PowerShell, l'archive --------------------------------------
    Start-XpgEtape 'Le système' | Out-Null
    $os = [Environment]::OSVersion.Version
    Write-XpgLog -Niveau INFO -Message ("Windows {0}.{1} (build {2}), {3} ; PowerShell {4}" -f $os.Major, $os.Minor, $os.Build, $(if ([Environment]::Is64BitOperatingSystem) { '64 bits' } else { '32 bits' }), $PSVersionTable.PSVersion)
    if (-not [Environment]::Is64BitOperatingSystem) { [void]$manquants.Add('Windows 64 bits (XPGAnalyser est x64)') }
    $zone = Get-Item -LiteralPath (Join-Path $X.Outils 'build.bat') -Stream 'Zone.Identifier' -ErrorAction SilentlyContinue
    if ($zone) {
        Write-XpgLog -Niveau ATTENTION -Message "Les fichiers viennent d'un zip téléchargé (marqués « Internet ») : Windows peut demander une confirmation à chaque script."
        Write-XpgLog -Niveau INFO -Message "Pour l'éviter : clic droit sur le zip > Propriétés > Débloquer, puis extraire à nouveau (voir docs\DEPANNAGE.md)."
    }
    $libre = (Get-PSDrive -Name ($X.Racine.Substring(0, 1)) -ErrorAction SilentlyContinue).Free
    if ($libre) { Write-XpgLog -Niveau INFO -Message ("Espace libre sur {0}: : {1}" -f $X.Racine.Substring(0, 1), (Format-XpgTaille $libre)) }
    Complete-XpgEtape -Statut OK

    # ---- 2. MSVC, MSBuild, SDK ------------------------------------------------------
    Start-XpgEtape 'Microsoft C++ (MSVC), MSBuild, SDK Windows' | Out-Null
    $vw = Find-XpgVsWhere
    if ($vw) { Write-XpgLog -Niveau INFO -Message "vswhere : $vw" } else { Write-XpgLog -Niveau INFO -Message 'vswhere : absent (normal sur un PC sans Visual Studio ni Build Tools)' }
    $instances = Get-XpgInstancesVs
    foreach ($i in $instances) { Write-XpgLog -Niveau INFO -Message ("Instance : {0} {1} - {2} ({3})" -f $i.Produit, $i.Version, $i.Chemin, $i.Origine) }
    $msvc = Get-XpgChaineMsvc
    $clPath = Find-XpgDansPath 'cl.exe'
    if ($clPath) { Write-XpgLog -Niveau INFO -Message "cl.exe dans le PATH : $clPath" }
    if ($msvc) {
        Write-XpgLog -Niveau OK -Message ("MSVC {0} (x64 : {1})" -f $msvc.Msvc.VersionOutils, $msvc.Msvc.Cl)
        Write-XpgLog -Niveau INFO -Message ("Architectures cibles disponibles : {0}" -f $msvc.Msvc.Architectures)
        $vMsb = Get-XpgVersionMsBuild $msvc.MsBuild
        Write-XpgLog -Niveau OK -Message ("MSBuild {0} : {1}" -f $vMsb, $msvc.MsBuild)
        [void]$presents.Add("MSVC $($msvc.Msvc.VersionOutils)"); [void]$presents.Add("MSBuild $vMsb")
        $resultat['msvc'] = [ordered]@{ instance = $msvc.Instance.Chemin; version = $msvc.Msvc.VersionOutils; cl = $msvc.Msvc.Cl; msbuild = $msvc.MsBuild; msbuild_version = $vMsb }
    } else {
        Write-XpgLog -Niveau ATTENTION -Message 'Aucune installation de MSVC x64 avec MSBuild (Build Tools ou Visual Studio 2022).'
        [void]$manquants.Add('Microsoft C++ Build Tools 2022 (MSVC v143 x64, MSBuild)')
    }
    $sdk = Find-XpgSdkWindows
    if ($sdk.Trouve) { Write-XpgLog -Niveau OK -Message ("SDK Windows {0} : {1}" -f $sdk.Version, $sdk.Chemin); [void]$presents.Add("SDK Windows $($sdk.Version)") }
    else { Write-XpgLog -Niveau ATTENTION -Message "SDK Windows 10/11 : absent $($sdk.Detail)"; [void]$manquants.Add('SDK Windows 10/11') }
    $rt = Find-XpgRuntimeVc -Instance $(if ($msvc) { $msvc.Instance.Chemin } else { '' })
    if ($rt.Systeme) { Write-XpgLog -Niveau INFO -Message "Runtime Visual C++ x64 installé dans Windows : $($rt.Systeme)" }
    if ($rt.Redist) { Write-XpgLog -Niveau OK -Message ("DLL du runtime à livrer à côté de l'exe : {0} ({1} DLL)" -f $rt.Redist, $rt.Dll.Count) }
    elseif ($msvc) { Write-XpgLog -Niveau ATTENTION -Message "Le dossier VC\Redist\MSVC\...\x64\Microsoft.VC143.CRT est absent : package.bat ne pourra pas livrer le runtime."; [void]$manquants.Add('Redistribuable MSVC (composant des Build Tools)') }
    Complete-XpgEtape -Statut $(if ($msvc -and $sdk.Trouve) { 'OK' } else { 'ATTENTION' })

    # ---- 3. CMake, Ninja, Inno Setup, les autres chaines -------------------------
    Start-XpgEtape 'CMake, Ninja, Inno Setup, autres compilateurs' | Out-Null
    $inst = ''; if ($msvc) { $inst = $msvc.Instance.Chemin }
    $cmake = Find-XpgCMake -Instance $inst
    $ninja = Find-XpgNinja -Instance $inst
    $inno = Find-XpgInnoSetup
    $mingw = Find-XpgMinGW
    $clang = Find-XpgClang -Instance $inst
    foreach ($o in @($cmake, $ninja, $inno)) {
        if ($o.Trouve) { Write-XpgLog -Niveau OK -Message ("{0} {1} : {2}" -f $o.Nom, $o.Version, $o.Chemin); [void]$presents.Add("$($o.Nom) $($o.Version)") }
        else { Write-XpgLog -Niveau ATTENTION -Message "$($o.Nom) : absent"; [void]$manquants.Add($o.Nom) }
    }
    foreach ($o in @($mingw, $clang)) {
        if ($o.Trouve) { Write-XpgLog -Niveau INFO -Message ("{0} {1} : {2} {3}" -f $o.Nom, $o.Version, $o.Chemin, $o.Detail) }
        else { Write-XpgLog -Niveau INFO -Message "$($o.Nom) : absent (facultatif)" }
    }
    $resultat['cmake'] = $cmake; $resultat['ninja'] = $ninja; $resultat['innosetup'] = $inno; $resultat['mingw'] = $mingw; $resultat['clang'] = $clang
    Complete-XpgEtape -Statut $(if ($cmake.Trouve -and $ninja.Trouve -and $inno.Trouve) { 'OK' } else { 'ATTENTION' })

    # ---- 4. Le projet -----------------------------------------------------------------
    Start-XpgEtape 'Le projet et ses dépendances' | Out-Null
    $sln = Join-Path $X.Racine (Get-XpgConfig -Section 'Projet' -Cle 'solution' -Defaut 'XpgAnalyzer.sln')
    $sdl = Join-Path $X.Racine 'third_party\SDL3'
    $ok = $true
    foreach ($f in @($sln, (Join-Path $X.Racine 'XpgAnalyzer.vcxproj'), (Join-Path $X.Racine 'CMakeLists.txt'),
                     (Join-Path $sdl 'include\SDL3\SDL.h'), (Join-Path $sdl 'lib\x64\SDL3.lib'), (Join-Path $sdl 'lib\x64\SDL3.dll'),
                     (Join-Path $X.Racine 'resources\schneider_library.txt'), (Join-Path $X.Racine 'libs\index.txt'),
                     (Join-Path $X.Racine 'installateur\XPGAnalyser.iss'))) {
        if (Test-Path -LiteralPath $f) { Write-XpgDetail "présent : $f" }
        else { Write-XpgLog -Niveau ERREUR -Message "absent : $f"; $ok = $false; [void]$manquants.Add("fichier du projet : $f") }
    }
    if ($ok) { Write-XpgLog -Niveau OK -Message 'Solution, projet, SDL3 3.4.12 (x64), ressources, bibliothèque et script d''installation présents.' }
    Complete-XpgEtape -Statut $(if ($ok) { 'OK' } else { 'ECHEC' })

    # ---- 5. L'essai reel ----------------------------------------------------------------
    $chaine = 'aucune'
    if ($X.Options.ContainsKey('sans-essai')) {
        Start-XpgEtape 'Essai du compilateur' | Out-Null
        Complete-XpgEtape -Statut IGNORE -Detail '/sans-essai'
        if ($msvc) { $chaine = 'msvc (non essayée)' }
    } elseif ($msvc) {
        Start-XpgEtape 'Essai de MSVC : compiler, lier, lancer, code de retour, architecture' | Out-Null
        Import-XpgEnvironnementVs -Instance $msvc.Instance.Chemin
        $essai = Test-XpgCompilation -Chaine msvc -Compilateur $msvc.Msvc.Cl
        $resultat['essai'] = $essai
        Write-XpgLog -Niveau INFO -Message ("Compilé : {0} ; lié : {1} ; lancé : {2} ; code : {3} ; architecture : {4} ; temporaires supprimés : {5}" -f `
            $essai.Compile, $essai.Lie, $essai.Lance, $essai.Code, $essai.Architecture, (-not (Test-Path -LiteralPath $essai.Dossier)))
        if ($essai.Ok) { $chaine = 'msvc'; Complete-XpgEtape -Statut OK -Detail "MSVC $($msvc.Msvc.VersionOutils) x64 fonctionne" }
        else { Complete-XpgEtape -Statut ECHEC -Detail $essai.Raison; [void]$manquants.Add("un MSVC qui fonctionne ($($essai.Raison))") }
    } else {
        Start-XpgEtape 'Essai du compilateur' | Out-Null
        Complete-XpgEtape -Statut IGNORE -Detail 'aucun MSVC à essayer'
    }

    # ---- 6. Le verdict -------------------------------------------------------------------
    Start-XpgEtape 'Compatibilité avec le projet' | Out-Null
    $resultat['chaine'] = $chaine
    $resultat['manquants'] = @($manquants)
    $resultat['presents'] = @($presents)
    $pret = ($chaine -like 'msvc*') -and $sdk.Trouve -and $ok
    $resultat['pret'] = $pret
    Write-XpgLog -Niveau INFO -Message "Chaîne sélectionnée : $chaine"
    if ($pret) {
        Write-XpgLog -Niveau OK -Message 'Compatible : même compilateur, même ABI et même runtime que le projet Visual Studio (v143, x64, /MD) et le paquet SDL3 fourni.'
        if (-not $inno.Trouve) { Write-XpgLog -Niveau ATTENTION -Message 'Inno Setup manque pour fabriquer l''installateur : install_build_tools.bat l''installe.' }
        if (-not ($cmake.Trouve -and $ninja.Trouve)) { Write-XpgLog -Niveau ATTENTION -Message 'CMake/Ninja manquent pour les tests : composant « C++ CMake tools » des Build Tools.' }
    } else {
        Write-XpgLog -Niveau ATTENTION -Message ('Pas prêt : il manque ' + (($manquants | Select-Object -Unique) -join ' ; '))
        Write-XpgLog -Niveau INFO -Message 'install_build_tools.bat propose de les installer (rien n''est installé sans ton accord).'
    }
    Complete-XpgEtape -Statut $(if ($pret) { 'OK' } else { 'ATTENTION' })

    $json = Get-XpgOption -Noms @('json')
    if ($json) { Write-XpgTexte -Chemin $json -Texte ($resultat | ConvertTo-Json -Depth 6); Write-XpgLog -Niveau INFO -Message "Résultat JSON : $json" }
    # 0 : tout est la ; 11 : le compilateur marche mais il manque autre chose (Inno Setup,
    # CMake/Ninja, un fichier du projet) ; 10 : pas de compilateur utilisable.
    $tout = $pret -and $inno.Trouve -and $cmake.Trouve -and $ninja.Trouve
    if ($tout) { Exit-Xpg -Code 0 }
    if ($chaine -like 'msvc*') { Exit-Xpg -Code 11 }
    Exit-Xpg -Code 10
} catch {
    Stop-Xpg -Code 1 -Message "Erreur inattendue : $($_.Exception.Message)" -Exception $_
}

#Requires -Version 5.1
# =============================================================================
#  XpgOutils.psm1 - trouver les outils de compilation, et les essayer
# -----------------------------------------------------------------------------
#  Pas seulement le PATH : vswhere, les instances de Visual Studio enregistrees
#  (C:\ProgramData\Microsoft\VisualStudio\Packages\_Instances), les
#  emplacements standards, le registre (SDK Windows, Inno Setup), les
#  variables d'environnement, les dossiers de config.ini ([Compilateur]
#  compilateur_manuel, [Outils]).
#
#  Un compilateur n'est "fonctionnel" qu'apres un essai reel (Test-XpgCompilation) :
#  compiler, lier, lancer, lire le code de retour, verifier l'architecture du .exe.
# =============================================================================

Set-StrictMode -Off
$ErrorActionPreference = 'Stop'

function New-XpgOutil {
    param([string] $Nom, [string] $Chemin = '', [string] $Version = '', [string] $Origine = '', [string] $Detail = '')
    return [pscustomobject]@{ Nom = $Nom; Trouve = [bool]$Chemin; Chemin = $Chemin; Version = $Version; Origine = $Origine; Detail = $Detail }
}

function Find-XpgDansPath {
    param([string] $Exe)
    $c = Get-Command $Exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($c) { return $c.Source }
    return ''
}

function Get-XpgVersionFichier {
    param([string] $Chemin)
    try {
        $v = (Get-Item -LiteralPath $Chemin).VersionInfo
        if ($v.ProductVersion) { return ($v.ProductVersion -replace '\s.*$', '') }
        return $v.FileVersion
    } catch { return '' }
}

# ------------------------------------------------------- Visual Studio / Build Tools ----
function Join-XpgSpecial {
    # Un dossier de Windows ([Environment]::GetFolderPath : ProgramFiles...) suivi d'un chemin ;
    # "" si Windows ne le donne pas (on ne fabrique jamais un chemin relatif par erreur).
    param([string] $Dossier, [string] $Suite)
    $b = [Environment]::GetFolderPath($Dossier)
    if (-not $b) { return '' }
    return (Join-Path $b $Suite)
}

function Find-XpgVsWhere {
    $candidats = @()
    $cfg = Get-XpgConfig -Section 'Outils' -Cle 'vswhere' -Chemin
    if ($cfg) { $candidats += $cfg }
    $candidats += (Join-XpgSpecial 'ProgramFilesX86' 'Microsoft Visual Studio\Installer\vswhere.exe')
    $candidats += (Join-XpgSpecial 'ProgramFiles' 'Microsoft Visual Studio\Installer\vswhere.exe')
    $p = Find-XpgDansPath 'vswhere.exe'
    if ($p) { $candidats += $p }
    foreach ($c in $candidats) { if ($c -and (Test-Path -LiteralPath $c)) { return $c } }
    return ''
}

function Get-XpgInstancesVs {
    # Les installations de Visual Studio 2017+ (Build Tools compris) : par vswhere si on l'a,
    # sinon par les fichiers state.json des instances enregistrees, sinon par les dossiers
    # standards. Chacune : Chemin, Version, Produit, Origine.
    $instances = New-Object System.Collections.ArrayList
    $vw = Find-XpgVsWhere
    if ($vw) {
        $json = Get-XpgSortie -Fichier $vw -Arguments @('-products', '*', '-all', '-prerelease', '-format', 'json', '-utf8')
        try {
            foreach ($i in ($json | ConvertFrom-Json)) {
                [void]$instances.Add([pscustomobject]@{ Chemin = $i.installationPath; Version = $i.installationVersion; Produit = $i.productId; Nom = $i.displayName; Origine = 'vswhere' })
            }
        } catch { Write-XpgDetail "vswhere : sortie illisible" }
    }
    $dossierInstances = Join-XpgSpecial 'CommonApplicationData' 'Microsoft\VisualStudio\Packages\_Instances'
    if ($dossierInstances -and (Test-Path -LiteralPath $dossierInstances)) {
        foreach ($d in Get-ChildItem -LiteralPath $dossierInstances -Directory -ErrorAction SilentlyContinue) {
            $f = Join-Path $d.FullName 'state.json'
            if (-not (Test-Path -LiteralPath $f)) { continue }
            try {
                $s = [System.IO.File]::ReadAllText($f) | ConvertFrom-Json
                if ($s.installationPath -and -not ($instances | Where-Object { $_.Chemin -eq $s.installationPath })) {
                    $produit = ''
                    if ($s.product) { $produit = $s.product.id }
                    [void]$instances.Add([pscustomobject]@{ Chemin = $s.installationPath; Version = $s.installationVersion; Produit = $produit; Nom = ''; Origine = 'registre des instances' })
                }
            } catch { }
        }
    }
    foreach ($pf in @([Environment]::GetFolderPath('ProgramFilesX86'), [Environment]::GetFolderPath('ProgramFiles'))) {
        if (-not $pf) { continue }
        foreach ($edition in @('BuildTools', 'Community', 'Professional', 'Enterprise')) {
            $c = Join-Path $pf "Microsoft Visual Studio\2022\$edition"
            if ((Test-Path -LiteralPath (Join-Path $c 'VC\Tools\MSVC')) -and -not ($instances | Where-Object { $_.Chemin -eq $c })) {
                [void]$instances.Add([pscustomobject]@{ Chemin = $c; Version = '17'; Produit = $edition; Nom = "Visual Studio 2022 $edition"; Origine = 'emplacement standard' })
            }
        }
    }
    $manuel = Get-XpgConfig -Section 'Compilateur' -Cle 'compilateur_manuel' -Chemin
    if ($manuel -and (Test-Path -LiteralPath $manuel) -and -not ($instances | Where-Object { $_.Chemin -eq $manuel })) {
        [void]$instances.Add([pscustomobject]@{ Chemin = $manuel; Version = ''; Produit = 'manuel'; Nom = 'choisi dans config.ini'; Origine = 'config.ini' })
    }
    return @($instances)
}

function Find-XpgMsvc {
    # Dans une instance : le cl.exe x64 le plus recent (VC\Tools\MSVC\<version>\bin\Hostx64\x64).
    param([string] $Instance)
    $racine = Join-Path $Instance 'VC\Tools\MSVC'
    if (-not (Test-Path -LiteralPath $racine)) { return $null }
    $versions = Get-ChildItem -LiteralPath $racine -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+' } |
        Sort-Object { [version]($_.Name -replace '[^\d\.].*$', '') } -Descending
    foreach ($v in $versions) {
        $cl = Join-Path $v.FullName 'bin\Hostx64\x64\cl.exe'
        if (Test-Path -LiteralPath $cl) {
            return [pscustomobject]@{ Cl = $cl; VersionOutils = $v.Name; Instance = $Instance; Architectures = (Get-ChildItem -LiteralPath (Join-Path $v.FullName 'bin\Hostx64') -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.Name }) -join ', ' }
        }
    }
    return $null
}

function Find-XpgMsBuild {
    param([string] $Instance)
    foreach ($rel in @('MSBuild\Current\Bin\amd64\MSBuild.exe', 'MSBuild\Current\Bin\MSBuild.exe')) {
        $p = Join-Path $Instance $rel
        if (Test-Path -LiteralPath $p) { return $p }
    }
    return ''
}

function Get-XpgVersionMsBuild {
    param([string] $MsBuild)
    $s = Get-XpgSortie -Fichier $MsBuild -Arguments @('-version', '-nologo')
    $m = [regex]::Match($s, '(\d+\.\d+\.\d+(\.\d+)?)\s*$')
    if ($m.Success) { return $m.Groups[1].Value }
    return (Get-XpgVersionFichier $MsBuild)
}

function Find-XpgCMake {
    param([string] $Instance = '')
    $candidats = @()
    if ($Instance) { $candidats += (Join-Path $Instance 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe') }
    $p = Find-XpgDansPath 'cmake.exe'; if ($p) { $candidats += $p }
    $candidats += (Join-XpgSpecial 'ProgramFiles' 'CMake\bin\cmake.exe')
    foreach ($c in $candidats) {
        if ($c -and (Test-Path -LiteralPath $c)) {
            $v = [regex]::Match((Get-XpgSortie -Fichier $c -Arguments @('--version')), 'version\s+(\S+)').Groups[1].Value
            return (New-XpgOutil -Nom 'CMake' -Chemin $c -Version $v -Origine 'trouvé')
        }
    }
    return (New-XpgOutil -Nom 'CMake')
}

function Find-XpgNinja {
    param([string] $Instance = '')
    $candidats = @()
    if ($Instance) { $candidats += (Join-Path $Instance 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe') }
    $p = Find-XpgDansPath 'ninja.exe'; if ($p) { $candidats += $p }
    foreach ($c in $candidats) {
        if ($c -and (Test-Path -LiteralPath $c)) {
            return (New-XpgOutil -Nom 'Ninja' -Chemin $c -Version (Get-XpgSortie -Fichier $c -Arguments @('--version')) -Origine 'trouvé')
        }
    }
    return (New-XpgOutil -Nom 'Ninja')
}

function Find-XpgSdkWindows {
    # Registre (HKLM\...\Microsoft SDKs\Windows\v10.0), puis les dossiers Include\10.0.* qui ont
    # um\windows.h : la version la plus recente complete.
    $racine = ''
    foreach ($k in @('HKLM:\SOFTWARE\WOW6432Node\Microsoft\Microsoft SDKs\Windows\v10.0', 'HKLM:\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0')) {
        try { $v = Get-ItemProperty -Path $k -ErrorAction Stop; if ($v.InstallationFolder) { $racine = $v.InstallationFolder; break } } catch { }
    }
    if (-not $racine) { $racine = Join-XpgSpecial 'ProgramFilesX86' 'Windows Kits\10' }
    if (-not $racine) { return (New-XpgOutil -Nom 'SDK Windows') }
    $inc = Join-Path $racine 'Include'
    if (-not (Test-Path -LiteralPath $inc)) { return (New-XpgOutil -Nom 'SDK Windows') }
    $versions = Get-ChildItem -LiteralPath $inc -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' -and (Test-Path -LiteralPath (Join-Path $_.FullName 'um\windows.h')) } |
        Sort-Object { [version]$_.Name } -Descending
    if (-not $versions) { return (New-XpgOutil -Nom 'SDK Windows' -Detail "aucun Include\10.0.*\um\windows.h dans $racine") }
    return (New-XpgOutil -Nom 'SDK Windows' -Chemin $racine -Version $versions[0].Name -Origine 'registre')
}

function Find-XpgInnoSetup {
    $candidats = @()
    $cfg = Get-XpgConfig -Section 'Outils' -Cle 'iscc' -Chemin
    if ($cfg) { $candidats += $cfg }
    foreach ($k in @('HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
                     'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
                     'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1')) {
        try { $v = Get-ItemProperty -Path $k -ErrorAction Stop; if ($v.InstallLocation) { $candidats += (Join-Path $v.InstallLocation 'ISCC.exe') } } catch { }
    }
    $candidats += (Join-XpgSpecial 'ProgramFilesX86' 'Inno Setup 6\ISCC.exe')
    $candidats += (Join-XpgSpecial 'ProgramFiles' 'Inno Setup 6\ISCC.exe')
    $candidats += (Join-XpgSpecial 'LocalApplicationData' 'Programs\Inno Setup 6\ISCC.exe')
    $p = Find-XpgDansPath 'ISCC.exe'; if ($p) { $candidats += $p }
    foreach ($c in $candidats) {
        if ($c -and (Test-Path -LiteralPath $c)) {
            return (New-XpgOutil -Nom 'Inno Setup' -Chemin $c -Version (Get-XpgVersionFichier $c) -Origine 'trouvé')
        }
    }
    return (New-XpgOutil -Nom 'Inno Setup')
}

function Find-XpgMinGW {
    $candidats = @()
    $p = Find-XpgDansPath 'g++.exe'; if ($p) { $candidats += $p }
    foreach ($d in @('C:\msys64\mingw64\bin', 'C:\msys64\ucrt64\bin', 'C:\mingw64\bin', (Join-XpgSpecial 'ProgramFiles' 'mingw-w64\mingw64\bin'))) {
        if ($d) { $candidats += (Join-Path $d 'g++.exe') }
    }
    foreach ($c in $candidats) {
        if ($c -and (Test-Path -LiteralPath $c)) {
            $v = [regex]::Match((Get-XpgSortie -Fichier $c -Arguments @('-dumpfullversion')), '[\d\.]+').Value
            $cible = Get-XpgSortie -Fichier $c -Arguments @('-dumpmachine')
            return (New-XpgOutil -Nom 'MinGW-w64 (g++)' -Chemin $c -Version $v -Origine 'trouvé' -Detail $cible)
        }
    }
    return (New-XpgOutil -Nom 'MinGW-w64 (g++)')
}

function Find-XpgClang {
    param([string] $Instance = '')
    $candidats = @()
    $p = Find-XpgDansPath 'clang-cl.exe'; if ($p) { $candidats += $p }
    $candidats += (Join-XpgSpecial 'ProgramFiles' 'LLVM\bin\clang-cl.exe')
    if ($Instance) { $candidats += (Join-Path $Instance 'VC\Tools\Llvm\x64\bin\clang-cl.exe') }
    foreach ($c in $candidats) {
        if ($c -and (Test-Path -LiteralPath $c)) {
            $v = [regex]::Match((Get-XpgSortie -Fichier $c -Arguments @('--version')), 'version\s+(\S+)').Groups[1].Value
            return (New-XpgOutil -Nom 'LLVM/Clang (clang-cl)' -Chemin $c -Version $v -Origine 'trouvé')
        }
    }
    return (New-XpgOutil -Nom 'LLVM/Clang (clang-cl)')
}

function Find-XpgRuntimeVc {
    # Le redistribuable x64 installe dans Windows (HKLM\...\VC\Runtimes\x64), et les DLL a
    # deployer a cote de l'exe (VC\Redist\MSVC\<version>\x64\Microsoft.VC143.CRT).
    param([string] $Instance = '')
    $r = [pscustomobject]@{ Systeme = ''; Redist = ''; Dll = @() }
    try {
        $k = Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64' -ErrorAction Stop
        if ($k.Installed -eq 1) { $r.Systeme = $k.Version }
    } catch { }
    if ($Instance) {
        $base = Join-Path $Instance 'VC\Redist\MSVC'
        if (Test-Path -LiteralPath $base) {
            $v = Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^\d+\.\d+\.\d+' } |
                Sort-Object { [version]($_.Name -replace '[^\d\.].*$', '') } -Descending
            foreach ($d in $v) {
                $crt = Get-ChildItem -LiteralPath (Join-Path $d.FullName 'x64') -Directory -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($crt) {
                    $r.Redist = $crt.FullName
                    $r.Dll = @(Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' | ForEach-Object { $_.FullName })
                    break
                }
            }
        }
    }
    return $r
}

# ------------------------------------------------------------- l'environnement ----
function Import-XpgEnvironnementVs {
    # vcvars64.bat, puis ses variables dans ce processus : cl, link, rc, les SDK, CMake et
    # Ninja des Build Tools se trouvent ensuite par le PATH.
    param([string] $Instance)
    $vcvars = Join-Path $Instance 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat introuvable dans $Instance" }
    $sortie = & cmd.exe /d /c "`"$vcvars`" >nul 2>&1 && set" 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $sortie) { throw "vcvars64.bat a échoué (code $LASTEXITCODE)" }
    $n = 0
    foreach ($ligne in $sortie) {
        $i = $ligne.IndexOf('=')
        if ($i -gt 0) { [Environment]::SetEnvironmentVariable($ligne.Substring(0, $i), $ligne.Substring($i + 1), 'Process'); $n++ }
    }
    Write-XpgDetail "vcvars64 : $n variables importées"
}

function Update-XpgPathDepuisRegistre {
    # Apres une installation : le PATH de la machine et de l'utilisateur relus (sans
    # redemarrer la session).
    $m = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $u = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = (@($m, $u) | Where-Object { $_ }) -join ';'
}

# --------------------------------------------------------------- les essais ----
function Get-XpgArchitecturePe {
    # La machine d'un .exe : l'en-tete PE (0x8664 x64, 0x14c x86, 0xAA64 ARM64).
    param([string] $Chemin)
    $fs = [System.IO.File]::OpenRead($Chemin)
    try {
        $br = New-Object System.IO.BinaryReader $fs
        $fs.Position = 0x3C
        $pe = $br.ReadInt32()
        $fs.Position = $pe + 4
        $m = $br.ReadUInt16()
        switch ($m) { 0x8664 { return 'x64' } 0x14c { return 'x86' } 0xAA64 { return 'ARM64' } default { return ('0x{0:X}' -f $m) } }
    } finally { $fs.Close() }
}

function Test-XpgCompilation {
    # L'essai reel : un petit programme C++20, compile, lie, lance ; son code de retour (42)
    # et son architecture (x64) verifies ; les fichiers temporaires supprimes.
    param([ValidateSet('msvc', 'mingw')] [string] $Chaine = 'msvc', [string] $Compilateur = '')
    $r = [pscustomobject]@{ Chaine = $Chaine; Compile = $false; Lie = $false; Lance = $false; Code = $null; Architecture = ''; Ok = $false; Raison = ''; Dossier = '' }
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('xpg-essai-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    $r.Dossier = $tmp
    try {
        $src = Join-Path $tmp 'essai.cpp'
        $code = @'
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
int main() {
    std::vector<std::string> v{"XPGAnalyser", "essai"};
    std::printf("%s %s : C++%ld, %zu bits\n", v[0].c_str(), v[1].c_str(), static_cast<long>(__cplusplus), sizeof(void*) * 8);
    return sizeof(void*) == 8 && std::filesystem::path("a/b").has_parent_path() ? 42 : 1;
}
'@
        [System.IO.File]::WriteAllText($src, $code, (New-Object System.Text.UTF8Encoding $false))
        $exe = Join-Path $tmp 'essai.exe'
        if ($Chaine -eq 'msvc') {
            $cl = 'cl.exe'; if ($Compilateur) { $cl = $Compilateur }
            $c = Invoke-XpgProcessus -Fichier $cl -Arguments @('/nologo', '/std:c++20', '/EHsc', '/Zc:__cplusplus', '/utf-8', '/c', $src, "/Fo$tmp\essai.obj") -Dossier $tmp -Libelle 'cl (compilation)'
            $r.Compile = ($c -eq 0)
            if (-not $r.Compile) { $r.Raison = "la compilation a échoué (code $c)"; return $r }
            $c = Invoke-XpgProcessus -Fichier 'link.exe' -Arguments @('/nologo', "$tmp\essai.obj", "/OUT:$exe", '/SUBSYSTEM:CONSOLE') -Dossier $tmp -Libelle 'link (édition des liens)'
            $r.Lie = ($c -eq 0) -and (Test-Path -LiteralPath $exe)
            if (-not $r.Lie) { $r.Raison = "l'édition des liens a échoué (code $c)"; return $r }
        } else {
            $gpp = 'g++.exe'; if ($Compilateur) { $gpp = $Compilateur }
            $c = Invoke-XpgProcessus -Fichier $gpp -Arguments @('-std=c++20', '-c', $src, '-o', "$tmp\essai.o") -Dossier $tmp -Libelle 'g++ (compilation)'
            $r.Compile = ($c -eq 0)
            if (-not $r.Compile) { $r.Raison = "la compilation a échoué (code $c)"; return $r }
            $c = Invoke-XpgProcessus -Fichier $gpp -Arguments @("$tmp\essai.o", '-o', $exe, '-static') -Dossier $tmp -Libelle 'g++ (édition des liens)'
            $r.Lie = ($c -eq 0) -and (Test-Path -LiteralPath $exe)
            if (-not $r.Lie) { $r.Raison = "l'édition des liens a échoué (code $c)"; return $r }
        }
        $p = Start-Process -FilePath $exe -WorkingDirectory $tmp -NoNewWindow -Wait -PassThru -RedirectStandardOutput (Join-Path $tmp 'sortie.txt')
        $r.Lance = $true
        $r.Code = $p.ExitCode
        Write-XpgDetail ("Sortie de l'essai : " + ([System.IO.File]::ReadAllText((Join-Path $tmp 'sortie.txt'))).Trim())
        $r.Architecture = Get-XpgArchitecturePe $exe
        if ($r.Code -ne 42) { $r.Raison = "le programme d'essai a rendu $($r.Code) au lieu de 42"; return $r }
        if ($r.Architecture -ne 'x64') { $r.Raison = "le programme d'essai est $($r.Architecture), pas x64"; return $r }
        $r.Ok = $true
        return $r
    } catch {
        $r.Raison = $_.Exception.Message
        return $r
    } finally {
        Start-Sleep -Milliseconds 200
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $tmp) { Write-XpgDetail "Dossier temporaire non supprimé : $tmp" }
    }
}

function Get-XpgChaineMsvc {
    # La meilleure instance utilisable : cl x64 + MSBuild + vcvars64. $null si aucune.
    $voulue = Get-XpgConfig -Section 'Compilateur' -Cle 'compilateur_manuel' -Chemin
    $instances = Get-XpgInstancesVs
    if ($voulue) { $instances = @($instances | Where-Object { $_.Chemin -eq $voulue }) + @($instances | Where-Object { $_.Chemin -ne $voulue }) }
    foreach ($i in $instances) {
        $msvc = Find-XpgMsvc $i.Chemin
        $msb = Find-XpgMsBuild $i.Chemin
        if ($msvc -and $msb -and (Test-Path -LiteralPath (Join-Path $i.Chemin 'VC\Auxiliary\Build\vcvars64.bat'))) {
            return [pscustomobject]@{ Instance = $i; Msvc = $msvc; MsBuild = $msb }
        }
    }
    return $null
}

function Get-XpgVersionsDuProjet {
    # (utilise le contexte de XpgCommun : $script:X n'y est pas ; on passe par Get-XpgRacine)
    # La version dans config.ini, CMakeLists.txt, src\core\Version.hpp et le .rc : elles
    # doivent concorder.
    $r = [ordered]@{}
    $r['config.ini'] = Get-XpgConfig -Section 'Produit' -Cle 'version'
    $cm = Join-Path (Get-XpgRacine) 'CMakeLists.txt'
    if (Test-Path -LiteralPath $cm) { $r['CMakeLists.txt'] = [regex]::Match([System.IO.File]::ReadAllText($cm), 'project\([^)]*VERSION\s+([\d\.]+)').Groups[1].Value }
    $vh = Join-Path (Get-XpgRacine) 'src\core\Version.hpp'
    if (Test-Path -LiteralPath $vh) { $r['Version.hpp'] = [regex]::Match([System.IO.File]::ReadAllText($vh), 'XPG_ANALYZER_VERSION\s+"([\d\.]+)"').Groups[1].Value }
    $rc = Join-Path (Get-XpgRacine) 'resources\windows\xpg_analyzer.rc'
    if (Test-Path -LiteralPath $rc) { $r['xpg_analyzer.rc'] = [regex]::Match([System.IO.File]::ReadAllText($rc), 'VALUE "ProductVersion",\s*"([\d\.]+)"').Groups[1].Value }
    return $r
}

function Test-XpgVersions {
    $v = Get-XpgVersionsDuProjet
    $valeurs = @($v.Values | Where-Object { $_ } | Select-Object -Unique)
    foreach ($k in $v.Keys) { Write-XpgDetail ("version dans {0} : {1}" -f $k, $v[$k]) }
    if ($valeurs.Count -ne 1) {
        Write-XpgLog -Niveau ERREUR -Message ('Les versions ne concordent pas : ' + (($v.Keys | ForEach-Object { "$_ = $($v[$_])" }) -join ' ; '))
        return ''
    }
    return $valeurs[0]
}


Export-ModuleMember -Function *

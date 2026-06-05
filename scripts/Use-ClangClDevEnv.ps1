param(
    [string]$Arch = "x64",
    [string]$HostArch = "x64",
    [string]$VsDevCmd = ""
)

$ErrorActionPreference = "Stop"

function Find-VsDevCmd {
    if ($VsDevCmd -and (Test-Path -LiteralPath $VsDevCmd)) {
        return (Resolve-Path -LiteralPath $VsDevCmd).Path
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installPath) {
            $candidate = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    $versions = @("18", "2022")
    $editions = @("BuildTools", "Community", "Professional", "Enterprise", "Preview")
    foreach ($version in $versions) {
        foreach ($edition in $editions) {
            $candidate = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\$version\$edition\Common7\Tools\VsDevCmd.bat"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    throw "Could not find VsDevCmd.bat. Install Visual Studio 2026 or Visual Studio 2022 Build Tools with Desktop development with C++, MSVC build tools, and a Windows 10/11 SDK."
}

$devCmd = Find-VsDevCmd
$command = "`"$devCmd`" -arch=$Arch -host_arch=$HostArch >nul && set"
$environment = & cmd.exe /s /c $command

foreach ($line in $environment) {
    if ($line -match "^([^=]+)=(.*)$") {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}

$libPaths = ($env:LIB -split ";") | Where-Object { $_ }
$kernel32Path = $libPaths | Where-Object { Test-Path -LiteralPath (Join-Path $_ "kernel32.lib") } | Select-Object -First 1
$oldnamesPath = $libPaths | Where-Object { Test-Path -LiteralPath (Join-Path $_ "oldnames.lib") } | Select-Object -First 1

if (-not $kernel32Path) {
    throw "Developer environment loaded, but kernel32.lib is still missing from LIB. Install or repair the Windows SDK."
}

if (-not $oldnamesPath) {
    throw "Developer environment loaded, but oldnames.lib is still missing from LIB. Install or repair the MSVC v143 build tools."
}

$vsRoot = Split-Path (Split-Path (Split-Path $devCmd -Parent) -Parent) -Parent
$toolsetRoot = Join-Path $vsRoot "MSBuild\Microsoft\VC\v180\Platforms\x64\PlatformToolsets\ClangCL"
$fallbackToolsetRoot = Join-Path $vsRoot "MSBuild\Microsoft\VC\v170\Platforms\x64\PlatformToolsets\ClangCL"

if (-not (Test-Path -LiteralPath $toolsetRoot) -and -not (Test-Path -LiteralPath $fallbackToolsetRoot)) {
    throw @"
Developer environment loaded, but the Visual Studio ClangCL MSBuild platform toolset is missing.

Install it in Visual Studio Installer:
  Modify Visual Studio Community 2026 or Build Tools 2022
  Individual components
  Add the C++ Clang/LLVM tools for Windows component

Component IDs to look for:
  Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset
  Microsoft.VisualStudio.Component.VC.Llvm.Clang

Standalone clang-cl.exe on PATH is not enough for the Visual Studio generator when CMake uses -T ClangCL.
"@
}

Write-Host "Loaded ClangCL/MSVC developer environment from: $devCmd"
Write-Host "LIB contains Windows SDK and MSVC runtime libraries."
Write-Host "Visual Studio ClangCL MSBuild platform toolset is available."

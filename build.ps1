param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$QtDir = $env:WORKSHOPDL_QT
)
$ErrorActionPreference = 'Stop'
if (-not $QtDir) {
    $qmake = Get-Command qmake.exe -ErrorAction SilentlyContinue
    if ($qmake) {
        $QtDir = Split-Path (Split-Path $qmake.Source)
    } else {
        $kits = Get-ChildItem -Path 'C:\Qt\*\msvc*_64\bin\qmake.exe' -File -ErrorAction SilentlyContinue |
            Sort-Object { [version](Split-Path (Split-Path (Split-Path $_.DirectoryName)) -Leaf) } -Descending
        if ($kits) { $QtDir = Split-Path $kits[0].DirectoryName }
    }
}
if (-not $QtDir -or -not (Test-Path -LiteralPath (Join-Path $QtDir 'bin\qmake.exe'))) {
    throw 'Provide -QtDir or set WORKSHOPDL_QT to a Qt 6.10+ MSVC x64 kit.'
}
$QtDir = (Resolve-Path -LiteralPath $QtDir).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vcvars = $null
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio with Desktop development with C++.' }
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No Visual Studio C++ x64 toolchain was found.' }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) { throw "Missing compiler environment: $vcvars" }
}
$buildDir = Join-Path $PSScriptRoot "build\$Configuration-x64"
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$mode = $Configuration.ToLowerInvariant()
$other = if ($mode -eq 'release') { 'debug' } else { 'release' }
$commands = @('@echo off')
if ($vcvars) { $commands += "call `"$vcvars`" || exit /b 1" }
$commands += "`"$QtDir\bin\qmake.exe`" `"$PSScriptRoot\WorkshopDL.pro`" CONFIG+=$mode CONFIG-=$other || exit /b 1"
$commands += 'nmake /nologo || exit /b 1'
$commands += 'exit /b 0'
$commandFile = Join-Path $buildDir 'build-command.cmd'
$commands | Set-Content -LiteralPath $commandFile -Encoding Default
Push-Location $buildDir
try {
    & $env:ComSpec /d /c "`"$commandFile`""
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }
} finally {
    Pop-Location
    Remove-Item -LiteralPath $commandFile -Force
}
Write-Output "Built: $buildDir\bin\WorkshopDL.exe"

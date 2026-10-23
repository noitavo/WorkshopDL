$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$bin = Join-Path $root 'build\Release-x64\bin'
$exe = Join-Path $bin 'WorkshopDL.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw 'Run build.bat before packaging.'
}
if (-not (Test-Path -LiteralPath (Join-Path $bin 'Qt6Core.dll') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $bin 'platforms\qwindows.dll') -PathType Leaf)) {
    throw 'Qt deployment is incomplete. Run build.bat again.'
}
$version = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if ($version -notmatch '^\d+\.\d+\.\d+(?:\.\d+)?$') {
    throw 'The executable has no valid product version.'
}
$version = ($version -split '\.')[0..2] -join '.'
$name = "WorkshopDL++-$version-win-x64"
$dist = Join-Path $root 'dist'
$package = [IO.Path]::GetFullPath((Join-Path $dist $name))
if (-not $package.StartsWith([IO.Path]::GetFullPath($dist) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package directory is outside dist.'
}
$archive = Join-Path $dist "$name.zip"

if (Test-Path -LiteralPath $package) {
    if (Test-Path -LiteralPath (Join-Path $package 'data')) {
        throw 'Remove personal data from the existing release folder before packaging.'
    }
    Remove-Item -LiteralPath $package -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $package 'licenses\WebView2') -Force | Out-Null
Get-ChildItem -LiteralPath $bin -Force | Where-Object {
    $_.Name -ne 'data' -and $_.Extension -notin @('.pdb', '.ilk', '.obj', '.log')
} | Copy-Item -Destination $package -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'third_party\webview2\LICENSE.txt') -Destination (Join-Path $package 'licenses\WebView2\LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $root 'third_party\webview2\NOTICE.txt') -Destination (Join-Path $package 'licenses\WebView2\NOTICE.txt')
Copy-Item -LiteralPath (Join-Path $root 'assets\about\license.txt') -Destination (Join-Path $package 'licenses\WorkshopDL.txt')
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $package 'licenses\CC-BY-NC-SA-4.0.txt')

Compress-Archive -LiteralPath $package -DestinationPath $archive -Force
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$archive.sha256", "$hash  $name.zip`r`n", [Text.Encoding]::ASCII)
Write-Output "Package: $archive"
Write-Output "SHA256: $hash"

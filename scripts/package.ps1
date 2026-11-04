param(
    [string]$BuildDir = 'out/build/win-amd64-release',
    [string]$OutputDir = 'out/package'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

Push-Location $repoRoot
try {
    $match = Select-String -Path 'CMakeLists.txt' -Pattern '^project\(rerevved_tools VERSION (\d+\.\d+\.\d+) '
    if (-not $match) { throw 'Project version not found in CMakeLists.txt.' }
    $name = "rerevved-tools-v$($match.Matches[0].Groups[1].Value)-windows-x64"

    $stage = Join-Path $OutputDir $name
    $archive = "$stage.zip"
    if ((Test-Path $stage) -or (Test-Path $archive)) { throw "$name already exists in $OutputDir." }

    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item (Join-Path $BuildDir 'rerevved-tools.exe'), 'LICENSE', 'README.md' $stage
    Copy-Item 'licenses' (Join-Path $stage 'licenses') -Recurse
    Compress-Archive -Path "$stage/*" -DestinationPath $archive
    Write-Host "package: $archive"
}
finally {
    Pop-Location
}

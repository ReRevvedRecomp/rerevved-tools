param(
    [ValidateSet('win-amd64-debug', 'win-amd64-release', 'linux-amd64-debug')]
    [string]$Preset = 'win-amd64-debug',
    [switch]$SkipAppSmoke
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$requiredClangFormatMajor = 22
$clangFormat = Get-Command clang-format -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $clangFormat) {
    throw "clang-format $requiredClangFormatMajor.x is required but was not found in PATH."
}
$versionOutput = @(& $clangFormat.Source --version 2>&1)
$versionExitCode = $LASTEXITCODE
$versionText = ($versionOutput -join ' ').Trim()
$versionMatch = [regex]::Match($versionText, '(?i)\bclang-format version (?<version>\d+\.\d+\.\d+)(?=\s|$|\()')
if ($versionExitCode -ne 0) {
    throw "clang-format $requiredClangFormatMajor.x version query failed at $($clangFormat.Source): $versionText"
}
if (-not $versionMatch.Success -or -not $versionMatch.Groups['version'].Value.StartsWith("$requiredClangFormatMajor.")) {
    $reportedVersion = if ($versionMatch.Success) { $versionMatch.Groups['version'].Value } else { 'unknown' }
    throw "clang-format $requiredClangFormatMajor.x is required; found $reportedVersion at $($clangFormat.Source)."
}

Push-Location $repoRoot
try {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

    & ctest --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

    if (-not $SkipAppSmoke) {
        $executableName = if ($env:OS -eq 'Windows_NT') {
            'rerevved-tools.exe'
        }
        else {
            'rerevved-tools'
        }
        $executable = Join-Path $repoRoot "out/build/$Preset/$executableName"
        & $executable --smoke-test (Join-Path $repoRoot 'README.md')
        if ($LASTEXITCODE -ne 0) { throw 'Application smoke test failed.' }

        $inputScripts = @('input-basics.txt')
        # Tools draws its own title bar only on Windows.
        if ($IsWindows) {
            $inputScripts += 'input-chrome.txt'
        }
        foreach ($script in $inputScripts) {
            & $executable --smoke-test --smoke-input (Join-Path $repoRoot "tests/smoke/$script")
            if ($LASTEXITCODE -ne 0) { throw "Scripted input smoke failed: $script." }
        }

        # Map scripts share one new folder: the first saves a map, which then stands in for a
        # recovery copy left by a session that ended unsaved, and the second restores and opens it.
        $data = Join-Path $repoRoot "out/build/$Preset/smoke-data/$([guid]::NewGuid().ToString('N'))"
        New-Item -ItemType Directory -Path $data | Out-Null
        & $executable --smoke-test --smoke-data $data --smoke-input (Join-Path $repoRoot 'tests/smoke/input-map-save.txt')
        if ($LASTEXITCODE -ne 0) { throw 'Scripted input smoke failed: input-map-save.txt.' }
        $recovery = New-Item -ItemType Directory -Force -Path (Join-Path $data 'Recovery')
        Copy-Item -LiteralPath (Join-Path $data 'Maps/Smoke Isles.rrmap') -Destination (Join-Path $recovery 'map-recovered.rrmap')
        & $executable --smoke-test --smoke-data $data --smoke-input (Join-Path $repoRoot 'tests/smoke/input-map-open.txt')
        if ($LASTEXITCODE -ne 0) { throw 'Scripted input smoke failed: input-map-open.txt.' }
        if (Get-ChildItem -LiteralPath $recovery -Filter '*.rrmap') { throw 'Closing a restored map without saving left its recovery copy.' }

        $data = Join-Path $repoRoot "out/build/$Preset/smoke-data/$([guid]::NewGuid().ToString('N'))"
        New-Item -ItemType Directory -Path $data | Out-Null
        & $executable --smoke-test --smoke-data $data --smoke-input (Join-Path $repoRoot 'tests/smoke/input-wizard.txt')
        if ($LASTEXITCODE -ne 0) { throw 'Scripted input smoke failed: input-wizard.txt.' }

        # Two maps with the same name export into one folder. The script types the relative folder
        # name, so it runs from inside its own data folder.
        $data = Join-Path $repoRoot "out/build/$Preset/smoke-data/$([guid]::NewGuid().ToString('N'))"
        $packs = Join-Path $data 'packs'
        New-Item -ItemType Directory -Path $packs | Out-Null
        Push-Location $data
        try {
            & $executable --smoke-test --smoke-data $data --smoke-input (Join-Path $repoRoot 'tests/smoke/input-map-folder.txt')
            if ($LASTEXITCODE -ne 0) { throw 'Scripted input smoke failed: input-map-folder.txt.' }
        }
        finally {
            Pop-Location
        }
        $manifests = @(Get-ChildItem -LiteralPath $packs -Filter 'asset-pack.toml' -Recurse)
        if ($manifests.Count -ne 2) { throw 'Choosing a folder with a pack did not give the second map its own pack.' }
        $slots = @($manifests | ForEach-Object { (Select-String -LiteralPath $_.FullName -Pattern '^\s*slot\s*=\s*(\d+)').Matches[0].Groups[1].Value } | Sort-Object -Unique)
        if ($slots.Count -ne 2) { throw 'Choosing a folder with a pack did not move the second map to a free slot.' }
    }

    $sources = Get-ChildItem -Path 'api', 'src', 'tests' -Recurse -File |
        Where-Object { $_.Extension -in '.cpp', '.h' } |
        ForEach-Object { $_.FullName }
    & $clangFormat.Source --dry-run --Werror $sources
    if ($LASTEXITCODE -ne 0) { throw 'clang-format check failed.' }

    & git diff --check
    if ($LASTEXITCODE -ne 0) { throw 'git diff --check failed.' }
}
finally {
    Pop-Location
}

$smokeStatus = if ($SkipAppSmoke) { 'app-smoke-skipped' } else { 'app-smoke' }
Write-Host "verify: passed configure build tests $smokeStatus format diff-check"

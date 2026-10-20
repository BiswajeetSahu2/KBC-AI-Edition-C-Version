$ErrorActionPreference = "Stop"

$sqliteVersion = "3540100"
$buildDirectory = Join-Path $PSScriptRoot "build"
$dependencyDirectory = Join-Path $buildDirectory "dependencies"
$sqliteDirectory = Join-Path $dependencyDirectory "sqlite"
$sqliteSource = Join-Path $sqliteDirectory "sqlite3.c"
if (-not (Test-Path $sqliteSource)) {
    $legacyDirectory = Join-Path $PSScriptRoot ".deps\sqlite-amalgamation-$sqliteVersion"
    New-Item -ItemType Directory -Path $dependencyDirectory -Force | Out-Null
    if (Test-Path $legacyDirectory) {
        Move-Item -Path $legacyDirectory -Destination $sqliteDirectory -Force
    } else {
        $archivePath = Join-Path $buildDirectory "sqlite-$sqliteVersion.zip"
        $extractedDirectory = Join-Path $dependencyDirectory "sqlite-amalgamation-$sqliteVersion"
        New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
        Invoke-WebRequest -Uri "https://www.sqlite.org/2026/sqlite-amalgamation-$sqliteVersion.zip" -OutFile $archivePath
        Expand-Archive -Path $archivePath -DestinationPath $dependencyDirectory -Force
        if (Test-Path $sqliteDirectory) {
            Remove-Item -Path $sqliteDirectory -Recurse -Force
        }
        Move-Item -Path $extractedDirectory -Destination $sqliteDirectory
        Remove-Item $archivePath
    }
}

$sources = Get-ChildItem -Path "src" -Filter "*.c" -Recurse | Select-Object -ExpandProperty FullName
$sources += $sqliteSource

Push-Location $PSScriptRoot
& gcc -std=c11 -Wall -Wextra -pedantic -D_WIN32_WINNT=0x0600 -include include/kbc/platform/windows_compat.h -I include -I $sqliteDirectory @sources -o kbc-backend.exe -lws2_32
$buildExitCode = $LASTEXITCODE
Pop-Location
if ($buildExitCode -ne 0) {
    exit $buildExitCode
}
<#
    Builds the Yasmarang weak-entropy scanner and KEEPS the binary on disk.

    PowerShell equivalent of build/threads-choose.sh, for when you are not in
    an MSYS2/Git Bash shell.

    Usage:
        .\build\threads-choose.ps1            # build only
        .\build\threads-choose.ps1 -Run       # build, then run it
        .\build\threads-choose.ps1 -Force     # rebuild even if binary is current
#>

param(
    [switch]$Run,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'BruteForce\BruteForce-Ratrix-Examples\threads-choose.cpp'
$out  = Join-Path $root 'threads-choose.exe'

# MinGW must be on PATH both to compile and to run (the binary links its DLLs).
$mingw = 'C:\msys64\mingw64\bin'
if (Test-Path $mingw) {
    $env:Path = "$mingw;$env:Path"
} else {
    Write-Warning "MinGW not found at $mingw - g++ and the runtime DLLs must already be on PATH."
}

# Skip the slow rebuild when nothing changed.
if (-not $Force -and (Test-Path $out)) {
    $outTime = (Get-Item $out).LastWriteTime
    $deps = @(
        $src,
        (Join-Path $root 'RTX_LIBS.h'),
        (Join-Path $root 'BruteForce\targetLists.h'),
        (Join-Path $root 'BruteForce\targets.h')
    ) + (Get-ChildItem (Join-Path $root 'Libs') -Recurse -File -ErrorAction SilentlyContinue).FullName

    $changed = $deps | Where-Object { $_ -and (Test-Path $_) -and (Get-Item $_).LastWriteTime -gt $outTime }

    if (-not $changed) {
        Write-Host "|| OK  $out is up to date (use -Force to rebuild)"
        if ($Run) { & $out }
        return
    }
}

# A still-running copy holds a lock on the binary and the linker fails with a
# bare "Permission denied", so name the real problem instead.
if (Get-Process -Name 'threads-choose' -ErrorAction SilentlyContinue) {
    Write-Error "threads-choose.exe is still running - close it first: Stop-Process -Name threads-choose -Force"
    return
}

Write-Host "|| Compiling: $src"
Write-Host "|| Output:    $out"
Write-Host "|| This takes 1-2 minutes (targets.h is ~78k string literals)."

# No -O2 on purpose: cc1plus runs out of memory optimizing targets.h, and the
# runtime is dominated by PBKDF2 inside OpenSSL anyway.
& g++ -std=c++17 $src -o $out -lcurl -lssl -lcrypto -lws2_32 -lcrypt32 -lwldap32 -lz -pthread

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    return
}

Write-Host "|| Build successful, binary kept at: $out"

if ($Run) {
    Write-Host "|| Running..."
    Write-Host ""
    & $out
}

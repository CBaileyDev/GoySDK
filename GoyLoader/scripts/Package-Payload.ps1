# Packages CMake Release *runtime* output into GoyLoader\Resources\payload.zip (DLLs + models/).
# Excludes CMakeFiles, object files, and other build noise so the embedded loader stays smaller.
# Run after internal_bot\BUILD.bat (or CMake Release). Models: embed in GoySDKCore via resources.rc;
# optional on-disk copies under internal_bot\models are merged into build\models and zipped.
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$buildDir = Join-Path $root "internal_bot\build"
$outZip = Join-Path $root "GoyLoader\Resources\payload.zip"

function Get-LibTorchRootFromCMakeCache {
    param([string]$CachePath)

    if (-not (Test-Path $CachePath)) { return $null }
    $line = Select-String -Path $CachePath -Pattern '^CMAKE_PREFIX_PATH:' | Select-Object -First 1
    if (-not $line) { return $null }

    $parts = $line.Line -split '=', 2
    if ($parts.Count -lt 2) { return $null }

    $candidate = $parts[1].Trim()
    if ([string]::IsNullOrWhiteSpace($candidate)) { return $null }
    if (Test-Path (Join-Path $candidate "lib")) { return $candidate }
    return $null
}

if (-not (Test-Path (Join-Path $buildDir "GoySDK.dll"))) {
    Write-Error "Missing GoySDK.dll in build folder. Build internal_bot first (BUILD.bat or CMake Release)."
}

$libTorchRoot = $env:LIBTORCH_DIR
if ([string]::IsNullOrWhiteSpace($libTorchRoot)) {
    $libTorchRoot = Get-LibTorchRootFromCMakeCache -CachePath (Join-Path $buildDir "CMakeCache.txt")
}
if (-not [string]::IsNullOrWhiteSpace($libTorchRoot)) {
    $libTorchLib = Join-Path $libTorchRoot "lib"
    if (Test-Path $libTorchLib) {
        foreach ($src in (Get-ChildItem -Path $libTorchLib -Filter *.dll -File)) {
            $dest = Join-Path $buildDir $src.Name
            if (-not (Test-Path $dest)) {
                Copy-Item $src.FullName $dest -Force
                Write-Host "Filled missing Torch runtime DLL: $($src.Name)"
            }
        }
    }
}

$modelsSrc = Join-Path $root "internal_bot\models"
if (Test-Path $modelsSrc) {
    $modelsDest = Join-Path $buildDir "models"
    New-Item -ItemType Directory -Force -Path $modelsDest | Out-Null
    Copy-Item -Path (Join-Path $modelsSrc "*") -Destination $modelsDest -Recurse -Force
    Write-Host "Merged internal_bot\models into build\models for payload."
}

New-Item -ItemType Directory -Force -Path (Split-Path $outZip) | Out-Null
if (Test-Path $outZip) { Remove-Item $outZip -Force }

$staging = Join-Path ([System.IO.Path]::GetTempPath()) ("GoyPayload_" + [Guid]::NewGuid().ToString("n"))
New-Item -ItemType Directory -Force -Path $staging | Out-Null
try {
    Get-ChildItem -Path $buildDir -Filter *.dll -File | Copy-Item -Destination $staging -Force
    $builtModels = Join-Path $buildDir "models"
    if (Test-Path $builtModels) {
        Copy-Item -Path $builtModels -Destination (Join-Path $staging "models") -Recurse -Force
    }

    $dllCount = (Get-ChildItem -Path $staging -Filter *.dll -File).Count
    if ($dllCount -lt 1) { Write-Error "Staging folder has no DLLs." }

    Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $outZip -CompressionLevel Optimal
    Write-Host "Wrote $outZip ($dllCount DLL(s), models: $(Test-Path (Join-Path $staging 'models')))."
}
finally {
    Remove-Item -Path $staging -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Rebuild GoyLoader to embed the new payload."

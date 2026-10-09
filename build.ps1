$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
& (Join-Path $PSScriptRoot 'tools\fetch_pdfium.ps1')
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswherePath) {
        $cmakePath = & $vswherePath -latest -products '*' -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
        if ($cmakePath) { $env:Path = (Split-Path -Parent $cmakePath) + ';' + $env:Path }
    }
}
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Get-Item -LiteralPath 'dist/包子PDF.exe' | Select-Object FullName,Length

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Get-Item -LiteralPath 'dist/包子PDF.exe' | Select-Object FullName,Length

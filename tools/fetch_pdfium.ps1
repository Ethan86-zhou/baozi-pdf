$ErrorActionPreference = 'Stop'
$dependencyRoot = Join-Path $PSScriptRoot '..\third_party\pdfium'
$expectedArchiveHash = '1FD8AF952832DBB0EB16D9249F68FE09E5F5EBF7C3DD9F6066EA2720CC28487D'
$version = '157.0.8086.0'
if ((Test-Path -LiteralPath (Join-Path $dependencyRoot 'include\fpdf_text.h')) -and
    (Test-Path -LiteralPath (Join-Path $dependencyRoot 'bin\pdfium.dll')) -and
    (Test-Path -LiteralPath (Join-Path $dependencyRoot '.verified-8086'))) { return }
New-Item -ItemType Directory -Force -Path $dependencyRoot | Out-Null
$dependencyArchive = Join-Path ([IO.Path]::GetTempPath()) ('baozi-pdfium-' + [guid]::NewGuid().ToString('N') + '.tgz')
try {
    Write-Output "Downloading PDFium $version (Windows x64, no V8/XFA)..."
    Invoke-WebRequest -Uri 'https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/8086/pdfium-win-x64.tgz' -OutFile $dependencyArchive
    if ((Get-FileHash -LiteralPath $dependencyArchive -Algorithm SHA256).Hash -ne $expectedArchiveHash) { throw 'PDFium archive SHA-256 mismatch.' }
    & tar -xzf $dependencyArchive -C $dependencyRoot
    if ($LASTEXITCODE -ne 0) { throw 'PDFium extraction failed.' }
    $expectedArchiveHash | Set-Content -LiteralPath (Join-Path $dependencyRoot '.verified-8086') -Encoding ascii
} finally {
    if (Test-Path -LiteralPath $dependencyArchive) { Remove-Item -LiteralPath $dependencyArchive }
}

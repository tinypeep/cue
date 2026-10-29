# Install the source-built engine from the matching Cue Windows/source package.
param([string]$Dest = "build\Cue", [Parameter(Mandatory=$true)][string]$LocalDll)
$ErrorActionPreference = "Stop"
$expected = "26fa28434e30aa319a8ab39fba76dbc4a84757b89349c1fc69b5e55ea6c02bca"
$dll = (Resolve-Path -LiteralPath $LocalDll).Path
if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
  throw "Esta DLL nao corresponde ao motor Cue 1.1.4. Confira a versao e o manifesto."
}
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
$target = Join-Path $Dest "libmpv-2.dll"
if ([IO.Path]::GetFullPath($dll) -ne [IO.Path]::GetFullPath($target)) {
  Copy-Item -LiteralPath $dll -Destination $target -Force
}
$notices = Join-Path (Split-Path $PSScriptRoot -Parent) "third_party\libmpv"
$third = Join-Path $Dest "third_party"
New-Item -ItemType Directory -Force -Path $third | Out-Null
Copy-Item -LiteralPath $notices -Destination $third -Recurse -Force
Write-Host "ok: motor Cue 1.1.4 verificado e avisos copiados. Publique tambem o ZIP de fontes."

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath
)

$ErrorActionPreference = 'Stop'

$resolvedPath = (Resolve-Path -LiteralPath $InstallerPath).Path
$installerName = [System.IO.Path]::GetFileName($resolvedPath)
$hash = (Get-FileHash -LiteralPath $resolvedPath -Algorithm SHA256).Hash.ToUpperInvariant()
$checksumPath = "$resolvedPath.sha256"

"$hash  $installerName" | Set-Content -LiteralPath $checksumPath -Encoding ascii
Write-Output "Created: $checksumPath"
Write-Output "$hash  $installerName"

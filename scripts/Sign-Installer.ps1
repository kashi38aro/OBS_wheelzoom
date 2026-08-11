[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,

    [Parameter(Mandatory = $true)]
    [string]$CertificateThumbprint,

    [string]$SignToolPath,

    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'

$resolvedPath = (Resolve-Path -LiteralPath $InstallerPath).Path

if (-not $SignToolPath) {
    $signToolCommand = Get-Command signtool.exe -ErrorAction Stop
    $SignToolPath = $signToolCommand.Source
} else {
    $SignToolPath = (Resolve-Path -LiteralPath $SignToolPath).Path
}

$thumbprint = $CertificateThumbprint.Replace(' ', '').ToUpperInvariant()
$certificate = Get-ChildItem -Path 'Cert:\CurrentUser\My' |
    Where-Object { $_.Thumbprint -eq $thumbprint } |
    Select-Object -First 1

if (-not $certificate) {
    throw "Code-signing certificate was not found in Cert:\CurrentUser\My: $thumbprint"
}

if (-not $certificate.HasPrivateKey) {
    throw 'The selected certificate does not have a private key.'
}

if ($certificate.Subject -eq $certificate.Issuer) {
    throw 'A self-signed certificate is not suitable for a publicly trusted release.'
}

& $SignToolPath sign /fd SHA256 /sha1 $thumbprint /tr $TimestampUrl /td SHA256 $resolvedPath
if ($LASTEXITCODE -ne 0) {
    throw "signtool failed with exit code $LASTEXITCODE"
}

$signature = Get-AuthenticodeSignature -LiteralPath $resolvedPath
if ($signature.Status -ne 'Valid') {
    throw "The resulting signature is not valid: $($signature.Status)"
}

Write-Output "Signed: $resolvedPath"
Write-Output "Signer: $($certificate.Subject)"

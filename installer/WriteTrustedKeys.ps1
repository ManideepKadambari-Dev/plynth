#Requires -Version 7
param(
    [string]$Folder = (Join-Path $env:USERPROFILE 'Downloads\plynth-public-keys')
)

# Writes core/src/update/trusted-keys.txt from the owner's two public key files, plynth-main.pub and
# plynth-spare.pub. It reads those two files only, checks that each is a minisign public key, and prints both
# key lines and ids so they can be compared with the paper copy. It never reads or writes a secret key.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'PackageRules.ps1')
$target = Join-Path $root 'core/src/update/trusted-keys.txt'

if (Test-Path (Join-Path $Folder '*.key')) {
    throw "A .key file is in $Folder. Secret keys belong only on the signing sticks. Move it there first, then run this again."
}

# Reads one public key file and returns its key line and key id.
function Read-PublicKey([string]$name) {
    $file = Join-Path $Folder $name
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "$file is missing" }
    if ((Get-Item -LiteralPath $file).Length -gt 1024) { throw "$file is too big to be a public key" }
    $lines = @(Get-Content -LiteralPath $file | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    if ($lines.Count -ne 2 -or $lines[0] -notmatch '^untrusted comment: minisign public key ([0-9A-F]{16})$') {
        throw "$file is not a minisign public key file"
    }
    $shownId = $Matches[1]
    $line = $lines[1]
    if (-not (Test-PublicKeyLine $line)) { throw "the second line of $file is not a minisign public key" }
    $bytes = [Convert]::FromBase64String($line)
    $id = [byte[]]$bytes[2..9]
    $forward = [Convert]::ToHexString($id)
    [array]::Reverse($id)
    $reversed = [Convert]::ToHexString($id)
    if ($shownId -ne $reversed -and $shownId -ne $forward) { throw "the key id in the first line of $file does not match its key" }
    return [pscustomobject]@{ Name = $name; Line = $line; Id = $shownId }
}

$main = Read-PublicKey 'plynth-main.pub'
$spare = Read-PublicKey 'plynth-spare.pub'
if ($main.Line -eq $spare.Line -or $main.Id -eq $spare.Id) { throw 'the main and the spare key are the same key' }
foreach ($key in $main, $spare) {
    if ((Get-TestKeys $root) -contains $key.Line) { throw "$($key.Name) is a key from the tests, not the owner's key" }
}

$text = "# main key, id $($main.Id)`n$($main.Line)`n# spare key, id $($spare.Id)`n$($spare.Line)`n"
[System.IO.File]::WriteAllText($target, $text, [System.Text.UTF8Encoding]::new($false))
Write-Host "Wrote $target"
Write-Host ''
Write-Host 'Compare these with the paper copy, character by character:'
Write-Host "  main   $($main.Id)   $($main.Line)"
Write-Host "  spare  $($spare.Id)   $($spare.Line)"

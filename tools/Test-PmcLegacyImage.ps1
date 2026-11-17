param(
    [ValidateSet('pmc_02_aa_8axis_legacy', 'pmc_02_aa_8axis_legacy_debug')]
    [string]$Environment = 'pmc_02_aa_8axis_legacy',
    [string]$SlaveImage
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$path = Join-Path $root ".pio/build/$Environment/firmware.bin"
$bytes = [System.IO.File]::ReadAllBytes($path)
if ($bytes.Length -lt 520 -or $bytes.Length -gt 0x70000 -or $bytes.Length % 4) {
    throw 'Legacy image length is invalid.'
}
$terminator = [Array]::IndexOf($bytes, [byte]0, 0, 64)
if ($terminator -lt 1) {
    throw 'Legacy version must be nonempty and terminated within 64 bytes.'
}
for ($index = $terminator; $index -lt 64; $index++) {
    if ($bytes[$index] -ne 0) {
        throw 'Legacy version padding must be zero.'
    }
}
$version = [System.Text.Encoding]::ASCII.GetString($bytes, 0, $terminator)
$stack = [BitConverter]::ToUInt32($bytes, 512)
$entry = [BitConverter]::ToUInt32($bytes, 516)
if (($stack -band 0x2FFE0000) -ne 0x20000000 -or $stack -le 0x20000000 -or
    $stack -ge 0x20020000 -or ($stack -band 7)) {
    throw 'Initial stack is rejected by the immutable bootloader.'
}
$entryAddress = $entry -band 0xFFFFFFFE
if (!($entry -band 1) -or $entryAddress -lt 0x08010200 -or
    $entryAddress -ge (0x08010000 + $bytes.Length)) {
    throw 'Legacy reset entry is outside the application image or lacks Thumb state.'
}
if ($SlaveImage) {
    $slave = [System.IO.File]::ReadAllBytes((Resolve-Path $SlaveImage).Path)
    if ($slave.Length -lt 520 -or $slave.Length -gt 0x70000 -or $slave.Length % 4) {
        throw 'Slave package image length is invalid.'
    }
    for ($index = 0; $index -lt 64; $index++) {
        if ($slave[$index] -ne $bytes[$index]) {
            throw 'Master/slave legacy version fields differ.'
        }
    }
    Write-Output 'Master/slave 64-byte version fields match.'
}
[pscustomobject]@{
    Environment = $Environment
    Version = $version
    ImageBytes = $bytes.Length
    VectorAddress = '0x08010200'
    Stack = ('0x{0:X8}' -f $stack)
    ResetEntry = ('0x{0:X8}' -f $entry)
}
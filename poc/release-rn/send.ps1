# Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Sends an ELF to the PS5's ELF loader (port 9021, as etaHEN starts one) and
# prints what the ELF says. For who has no payload sender on Windows:
#
#   .\send.ps1 -Ps5 192.168.1.50 -Elf .\BrowserPS5.elf        (192.168.1.50: your PS5's address)
#
# If PowerShell refuses to run scripts:
#   powershell -ExecutionPolicy Bypass -File .\send.ps1 -Ps5 192.168.1.50 -Elf .\BrowserPS5.elf

param(
    [Parameter(Mandatory)][string]$Ps5,
    [Parameter(Mandatory)][string]$Elf,
    [int]$Port = 9021,
    [int]$TimeoutSec = 300
)

$ErrorActionPreference = "Stop"

$path = (Resolve-Path $Elf).ProviderPath
$bytes = [IO.File]::ReadAllBytes($path)
Write-Host ("Sending {0} ({1:N1} MiB) to {2}:{3} ..." -f (Split-Path $path -Leaf), ($bytes.Length / 1MB), $Ps5, $Port)

$client = [Net.Sockets.TcpClient]::new()
try {
    $client.Connect($Ps5, $Port)
} catch {
    Write-Host "No answer from ${Ps5}:${Port}. Is the jailbreak loaded (an ELF loader on port $Port), and is that the PS5's IP address?"
    exit 1
}
$sock = $client.Client
$stream = $client.GetStream()
$stream.Write($bytes, 0, $bytes.Length)
$sock.Shutdown([Net.Sockets.SocketShutdown]::Send)  # the loader has the whole ELF

$buf = New-Object byte[] 8192
$decoder = [Text.Encoding]::UTF8.GetDecoder()
$chars = New-Object char[] 8192
$deadline = (Get-Date).AddSeconds($TimeoutSec)
$last = ""
while ((Get-Date) -lt $deadline) {
    try {
        if (-not $sock.Poll(1000000, [Net.Sockets.SelectMode]::SelectRead)) { continue }
        $n = $stream.Read($buf, 0, $buf.Length)
    } catch {
        break
    }
    if ($n -le 0) { break }
    $count = $decoder.GetChars($buf, 0, $n, $chars, 0)
    $text = [string]::new($chars, 0, $count)
    $last += $text
    # the installer names every file it writes: only its own notices are shown here
    foreach ($line in ($text -split "`n")) {
        if ($line -match "PASS|FAIL|install|remov|open|BrowserPS5\] [0-9]") { Write-Host $line.TrimEnd() }
    }
}
$client.Close()
if ($last -match "is installed|was removed|nothing to remove") { exit 0 }
# It said why not (the browser is open, or something could not be written): that is the result.
if ($last -match "nothing was installed|nothing was removed|was NOT installed|could not be removed") { exit 1 }
Write-Host "Look at the notification on the PS5 for the result."
exit 0

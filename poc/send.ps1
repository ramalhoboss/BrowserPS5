# Copyright (C) 2026 BrowserPS5 contributors
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Send a payload to elfldr on the PS5, print what it writes to stdout, and
# save that output under poc/results/. Works without plink, nc or socat.
#
# Usage: .\send.ps1 -Ps5Host 192.168.1.51 -Payload spawn_ipc\spawn_ipc.elf
#
# -InputFile sends a file right after the ELF on the same connection. elfldr
# reads only the ELF, so the payload gets the file on its stdin (e.g. a
# script for the jsc shell).

param(
    [Parameter(Mandatory)][string]$Ps5Host,
    [Parameter(Mandatory)][string]$Payload,
    [string]$InputFile,
    [int]$Port = 9021,
    [int]$TimeoutSec = 180
)

$ErrorActionPreference = "Stop"

$path = (Resolve-Path $Payload).ProviderPath
$bytes = [IO.File]::ReadAllBytes($path)
$name = [IO.Path]::GetFileNameWithoutExtension($path)
$resultsDir = Join-Path $PSScriptRoot "results"
New-Item -ItemType Directory -Force $resultsDir | Out-Null
$log = Join-Path $resultsDir ("{0}-{1:yyyyMMdd-HHmmss}.log" -f $name, (Get-Date))

$client = [Net.Sockets.TcpClient]::new()
$client.Connect($Ps5Host, $Port)
$sock = $client.Client
$stream = $client.GetStream()
$stream.Write($bytes, 0, $bytes.Length)
if ($InputFile) {
    $stdinBytes = [IO.File]::ReadAllBytes((Resolve-Path $InputFile).ProviderPath)
    $stream.Write($stdinBytes, 0, $stdinBytes.Length)
}
# Half-close: elfldr has the whole ELF, and the payload sees EOF on stdin.
$sock.Shutdown([Net.Sockets.SocketShutdown]::Send)

# The log is written as the output comes in: when the console goes down
# (2026-11-03, it rebooted during a hello_page run) the connection is reset
# and nothing after that point would save it.
$buf = New-Object byte[] 8192
$decoder = [Text.Encoding]::UTF8.GetDecoder()  # a character may span reads
$chars = New-Object char[] 8192
$deadline = (Get-Date).AddSeconds($TimeoutSec)
$closed = $false
$note = $null
Set-Content -Path $log -Value "" -NoNewline -Encoding utf8

while ((Get-Date) -lt $deadline) {
    try {
        if (-not $sock.Poll(1000000, [Net.Sockets.SelectMode]::SelectRead)) {
            continue
        }
        $n = $sock.Receive($buf)
    } catch [Net.Sockets.SocketException] {
        $note = "`n[send.ps1] connection reset ($($_.Exception.Message.Trim())): did the console go down?`n"
        break
    }
    if ($n -le 0) {
        $closed = $true
        break
    }
    $count = $decoder.GetChars($buf, 0, $n, $chars, 0)
    $text = [string]::new($chars, 0, $count)
    Write-Host -NoNewline $text
    [IO.File]::AppendAllText($log, $text)
}
$client.Close()

if (-not $closed -and -not $note) {
    $note = "`n[send.ps1] stopped after $TimeoutSec s without the payload closing the connection`n"
}
if ($note) {
    Write-Host $note
    [IO.File]::AppendAllText($log, $note)
}
Write-Host "[send.ps1] output saved to $log"

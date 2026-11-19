# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Builds the installer with the C# compiler that Windows itself brings (.NET Framework 4): nothing is installed or
# downloaded. Two programs, from the same sources:
#   LibreELEC-Hades-Installer.exe        the installer, the one that is published: without the tests;
#   LibreELEC-Hades-Installer-tests.exe  the same installer with its tests (--selftest, --capture, --vhd-test, the
#                                        --simulate-* commands, --make-test-image), built with /define:HADES_TESTS.
# The tests carry data that is no part of the installer: a throw-away private key, certificates and small disk images
# that they write to files. In the published program the words of the tests only say where the tests are. Installing,
# updating and removing are the same code in both.
# Usage: build.ps1 [-Out <directory>]   (-Salida, its first name, still works)
param([Alias('Salida')][string]$Out = (Join-Path $PSScriptRoot 'bin'))
$ErrorActionPreference = 'Stop'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $csc)) { throw "The C# compiler of Windows was not found: $csc" }
New-Item -ItemType Directory -Force $Out | Out-Null
$product = 'AssemblyInfo.cs', 'Model.cs', 'Image.cs', 'BootMenus.cs', 'Diagnostics.cs', 'Disks.cs', 'SecureBoot.cs', 'Ext4.cs', 'Fat.cs',
           'SecureJob.cs', 'Operations.cs', 'Ui.cs', 'Window.cs', 'FirmwareBoot.cs', 'Program.cs'
$tests = 'TestVectors.cs', 'SecureBootTests.cs', 'SecureBootCapture.cs', 'SecureBootVhd.cs', 'Tests.cs', 'BootTests.cs', 'VhdTest.cs',
         'FirmwareBootTests.cs'
# The sources are UTF-8 without a byte order mark: /codepage says so, or the accents and quotes of the texts
# would depend on what the compiler guesses.
# There is no /deterministic: the compiler that Windows brings is the one of C# 5 (.NET Framework 4), which does
# not have that option (nor /pathmap: they came with the Roslyn compiler), and an option that it does not know
# stops the build. Two builds of the same sources can therefore differ in their bytes.
function Build-Installer([string]$name, [string[]]$files, [string[]]$extra)
{
  $exe = Join-Path $Out $name
  $sources = $files | ForEach-Object { Join-Path $PSScriptRoot $_ }
  & $csc /nologo /target:winexe /platform:anycpu /optimize+ /warn:4 /warnaserror+ /codepage:65001 @extra `
    "/win32manifest:$(Join-Path $PSScriptRoot 'app.manifest')" "/win32icon:$(Join-Path $PSScriptRoot 'app.ico')" `
    "/resource:$(Join-Path $PSScriptRoot 'app.ico'),Hades.Installer.app.ico" "/out:$exe" `
    /r:System.dll /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Management.dll $sources | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "The build of $name failed ($LASTEXITCODE)" }
  $exe
}
$published = Build-Installer 'LibreELEC-Hades-Installer.exe' $product @()
$withTests = Build-Installer 'LibreELEC-Hades-Installer-tests.exe' ($product + $tests) @('/define:HADES_TESTS')
# The published program must carry nothing of the tests: no piece of the data of TestVectors.cs (40 characters of each
# of its values: the test key, the certificates, the two packed images, the hashes), no PEM key (the header followed by
# its body; the installer itself writes and reads the PEM of the key that it makes for a PC, so the header alone is
# its own), no packed image (gzip in base64 starts "H4sI"), and no class of the tests. Strings of a .NET program are
# UTF-16 and its names UTF-8, so both are looked for.
$bytes = [System.IO.File]::ReadAllBytes($published)
$text = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
$wide = [System.Text.Encoding]::Unicode.GetString($bytes, 0, $bytes.Length - ($bytes.Length % 2)) +
        [System.Text.Encoding]::Unicode.GetString($bytes, 1, ($bytes.Length - 1) - (($bytes.Length - 1) % 2))
$words = @("PRIVATE KEY-----`nMII", 'H4sI', 'SbVectors', 'SelfTest', 'VhdTest', 'FakeFirmware', 'TestKeyPem', 'Ext4Small', 'Ext4Big')
$vectors = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'TestVectors.cs'))
foreach ($m in [regex]::Matches($vectors, '"([^"]{64,})"')) { $words += [regex]::Match($m.Groups[1].Value, '(?<!\\)[A-Za-z0-9+/=]{40}').Value }
if ($words.Count -lt 18) { throw "TestVectors.cs gave $($words.Count - 9) values to look for, not the 9 or more that it has" }
foreach ($word in $words)
{
  if ($word.Length -eq 0 -or $text.Contains($word) -or $wide.Contains($word)) { throw "The published program carries '$word', which belongs to the tests" }
}
$published
$withTests

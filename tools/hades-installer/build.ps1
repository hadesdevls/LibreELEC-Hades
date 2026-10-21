# Builds LibreELEC-Hades-Installer.exe with the C# compiler that Windows itself brings (.NET Framework 4): nothing
# is installed or downloaded. Usage: build.ps1 [-Out <directory>]   (-Salida, its first name, still works)
param([Alias('Salida')][string]$Out = (Join-Path $PSScriptRoot 'bin'))
$ErrorActionPreference = 'Stop'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $csc)) { throw "The C# compiler of Windows was not found: $csc" }
New-Item -ItemType Directory -Force $Out | Out-Null
$exe = Join-Path $Out 'LibreELEC-Hades-Installer.exe'
$sources = 'Model.cs', 'Image.cs', 'Diagnostics.cs', 'Disks.cs', 'Operations.cs', 'Window.cs', 'Tests.cs', 'VhdTest.cs', 'Program.cs' |
  ForEach-Object { Join-Path $PSScriptRoot $_ }
# The sources are UTF-8 without a byte order mark: /codepage says so, or the accents and quotes of the texts
# would depend on what the compiler guesses.
& $csc /nologo /target:winexe /platform:anycpu /optimize+ /warn:4 /warnaserror+ /codepage:65001 `
  "/win32manifest:$(Join-Path $PSScriptRoot 'app.manifest')" "/out:$exe" `
  /r:System.dll /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Management.dll $sources
if ($LASTEXITCODE -ne 0) { throw "The build failed ($LASTEXITCODE)" }
$exe

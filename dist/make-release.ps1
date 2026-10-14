# Compile SACoop puis assemble dist\out\SACoop-<version>.zip (sans aucune donnee du jeu).
# -Publier : cree la release v<Version> sur GitHub (par defaut zebra032-cyber/SACoop, "Zebra's forked repo", voir -Repo) avec le zip.
# -Notes : texte de la release. Exemple : .\dist\make-release.ps1 -Version 0.52.0-vcport -Publier -Notes "..."
param([string]$Version = (Get-Date -Format 'yyyy.MM.dd'), [switch]$Publier, [string]$Notes = '', [string]$Repo = 'zebra032-cyber/SACoop')
$root = Split-Path $PSScriptRoot
$out = cmd /c "`"$root\build.cmd`"" 2>&1
if ($LASTEXITCODE -ne 0) { $out | Select-String 'error'; throw "echec de compilation" }
$ErrorActionPreference = 'Stop'   # apres la compilation : vcvars ecrit sur stderr

$stage = "$PSScriptRoot\out\SACoop-$Version"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item "$root\build\dinput8.dll" $stage
Copy-Item "$root\build\SACoop.exe" $stage   # lanceur : mises a jour automatiques
Copy-Item "$PSScriptRoot\files\*" $stage -Recurse
Set-Content "$stage\SACoop\version.txt" $Version -NoNewline -Encoding ASCII

$zip = "$PSScriptRoot\out\SACoop-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$z = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
foreach ($f in Get-ChildItem $stage -File -Recurse) {
    $rel = $f.FullName.Substring($stage.Length + 1).Replace('\', '/')
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z, $f.FullName, $rel) | Out-Null
}
$z.Dispose()
"Ecrit : $zip ($([math]::Round((Get-Item $zip).Length / 1KB)) Ko)"
if ($Publier) {
    if (-not $Notes) { $Notes = "SACoop $Version" }
    $nf = [System.IO.Path]::GetTempFileName()
    [System.IO.File]::WriteAllText($nf, $Notes, (New-Object System.Text.UTF8Encoding $false))
    gh release create "v$Version" $zip --repo $Repo --title "SACoop $Version (pre-alpha)" --notes-file $nf --prerelease
    $rc = $LASTEXITCODE
    Remove-Item $nf
    if ($rc -ne 0) { throw "echec de la publication GitHub" }
    "Publie : https://github.com/$Repo/releases/tag/v$Version"
}

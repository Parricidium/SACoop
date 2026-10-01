# Arrivee en cours de partie : l'hote (Joueur1) d'abord, l'invite (Joueur2) $Delay secondes plus tard.
param([int]$Delay = 60, [int]$After = 45, [switch]$NoBuild)
$root = Split-Path $PSScriptRoot
$base = 'D:\Games\COOPTEST\GTA San Andreas'
if (-not $NoBuild) {
  $out = cmd /c "`"$root\build.cmd`"" 2>&1
  if ($LASTEXITCODE -ne 0) { $out | Select-String 'error'; throw "echec de compilation" }
}
Get-Process gta_sa -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$base\*" } | Stop-Process -Force
Start-Sleep -Milliseconds 500
$procs = @()
foreach ($n in 1, 2) {
  $g = "$base\SACoop-Joueur$n"
  Copy-Item "$root\build\dinput8.dll" $g -Force
  if ($n -eq 2) { Start-Sleep $Delay }
  $procs += Start-Process "$g\gta_sa.exe" -WorkingDirectory $g -PassThru
}
Start-Sleep $After
foreach ($p in $procs) { $p.Refresh(); "pid $($p.Id) vivant=$(-not $p.HasExited)" }
& "$PSScriptRoot\capture.ps1" -Prefix late
$procs | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }

# Instances deja lancees (test2.ps1 -KeepOpen) : N captures a intervalle, puis fermeture.
param([int]$Count = 6, [double]$Every = 1.0, [string]$Prefix = 'b', [switch]$KeepOpen)
for ($i = 0; $i -lt $Count; $i++) { & "$PSScriptRoot\capture.ps1" -Prefix "$Prefix$i" | Out-Null; Start-Sleep -Milliseconds ([int]($Every * 1000)) }
if (-not $KeepOpen) { Get-Process gta_sa -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'D:\Games\COOPTEST\GTA San Andreas\*' } | Stop-Process -Force }
"captures : $PSScriptRoot\$Prefix*-*.png"

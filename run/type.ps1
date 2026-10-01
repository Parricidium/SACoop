# Tape un message dans le tchat d'une instance de test : messages postes a SA fenetre (jamais au premier plan de JD).
param([int]$Player = 2, [string]$Text = "Salut", [switch]$Open = $true)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class SaPost { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l); }
"@
$p = Get-Process gta_sa -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "D:\Games\COOPTEST\GTA San Andreas\SACoop-Joueur$Player\*" } | Select-Object -First 1
if (-not $p) { throw "instance $Player absente" }
$h = $p.MainWindowHandle
$WM_KEYDOWN = 0x100; $WM_CHAR = 0x102
[SaPost]::PostMessage($h, $WM_KEYDOWN, [IntPtr]0x54, [IntPtr]0) | Out-Null   # T
Start-Sleep -Milliseconds 200
foreach ($c in $Text.ToCharArray()) { [SaPost]::PostMessage($h, $WM_CHAR, [IntPtr][int]$c, [IntPtr]0) | Out-Null; Start-Sleep -Milliseconds 30 }
Start-Sleep -Milliseconds 300
[SaPost]::PostMessage($h, $WM_KEYDOWN, [IntPtr]0x0D, [IntPtr]0) | Out-Null   # Entree
"tape dans $Player : $Text"

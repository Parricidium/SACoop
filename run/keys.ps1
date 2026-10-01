# Poste des touches a la fenetre d'une instance de test (jamais au premier plan de JD) : keys.ps1 -Player 1 -Keys F10,DOWN,RIGHT
param([int]$Player = 1, [string[]]$Keys, [int]$Delay = 250)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class SaKeys { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l); }
"@
$vk = @{ F10 = 0x79; F5 = 0x74; UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27; ENTER = 0x0D; ESC = 0x1B; G = 0x47; T = 0x54 }
$p = Get-Process gta_sa -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "D:\Games\COOPTEST\GTA San Andreas\SACoop-Joueur$Player\*" } | Select-Object -First 1
if (-not $p) { throw "instance $Player absente" }
foreach ($k in $Keys) {
  $code = $vk[$k.ToUpper()]
  [SaKeys]::PostMessage($p.MainWindowHandle, 0x100, [IntPtr]$code, [IntPtr]0) | Out-Null
  [SaKeys]::PostMessage($p.MainWindowHandle, 0x101, [IntPtr]$code, [IntPtr]0) | Out-Null
  Start-Sleep -Milliseconds $Delay
}
"touches $($Keys -join ' ') -> joueur $Player"

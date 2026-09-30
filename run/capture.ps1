# Capture chaque fenetre gta_sa via DWM (PrintWindow/PW_RENDERFULLCONTENT) : marche fenetre masquee, ne vole jamais le focus.
param([string]$Prefix = "win")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class SaWin32 {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
[SaWin32]::SetProcessDPIAware() | Out-Null
$i = 0
foreach ($p in (Get-Process gta_sa -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'D:\Games\COOPTEST\*' } | Sort-Object StartTime)) {
  $h = $p.MainWindowHandle
  $r = New-Object SaWin32+RECT
  [SaWin32]::GetWindowRect($h, [ref]$r) | Out-Null
  $w = $r.R - $r.L; $hh = $r.B - $r.T
  if ($w -le 0) { continue }
  if ([SaWin32]::IsHungAppWindow($h)) { "pid $($p.Id) : fenetre figee, pas de capture"; $i++; continue }
  $b = New-Object System.Drawing.Bitmap($w, $hh)
  $g = [System.Drawing.Graphics]::FromImage($b)
  $hdc = $g.GetHdc()
  [SaWin32]::PrintWindow($h, $hdc, 2) | Out-Null
  $g.ReleaseHdc($hdc)
  $out = "$PSScriptRoot\$Prefix-$i.png"
  $b.Save($out); $b.Dispose()
  "$out = pid $($p.Id) ${w}x${hh} '$($p.MainWindowTitle)'"; $i++
}

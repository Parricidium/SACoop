# Decompile des fonctions de gta_sa 1.0 US via Ghidra (projet deja analyse).
# Exemples : .\decomp.ps1 -Out winmain 0x748710 xref:0xc8d4c0
#            .\decomp.ps1 -Out asm -Script Disasm.java 0x53e920 40
# Premiere fois : .\decomp.ps1 -Import (importe et analyse l'exe, long).
[CmdletBinding(PositionalBinding=$false)]
param([string]$Out = 'x', [string]$Script = 'Decomp.java', [switch]$Write, [switch]$Import, [Parameter(ValueFromRemainingArguments)][string[]]$Targets)
$S = 'C:\Users\JD\AppData\Local\Temp\claude\sacoop-re'
$gh = "$S\gh"; $re = "$S\sare"
if (-not (Test-Path $S)) { New-Item -ItemType Directory -Force $S | Out-Null }
if (-not (Test-Path "$gh\support")) { New-Item -ItemType Junction -Path $gh -Target 'D:\1 - AI\ClaudeAI\MafiaCoop\tools\ghidra_12.1.3_PUBLIC' | Out-Null }
if (-not (Test-Path "$re\scripts")) { New-Item -ItemType Junction -Path $re -Target $PSScriptRoot | Out-Null }
if (-not (Test-Path "$re\proj")) { New-Item -ItemType Directory -Force "$re\proj" | Out-Null }
$env:JAVA_HOME = 'C:\Program Files\Microsoft\jdk-21.0.12.101-hotspot'
if ($Import) {
  & "$gh\support\analyzeHeadless.bat" "$re\proj" SARE -import "$re\gta-sa-1.0.exe" -overwrite *> "$re\out\import.log"
  Get-Content "$re\out\import.log" -Tail 5
  return
}
$outFile = "$re\out\$Out.c"
& "$gh\support\analyzeHeadless.bat" "$re\proj" SARE -process gta-sa-1.0.exe -noanalysis $(if (-not $Write) { '-readOnly' }) `
  -scriptPath "$re\scripts" -postScript $Script $outFile @Targets *> "$re\out\decomp.log"
if (Test-Path $outFile) { "Ecrit : $PSScriptRoot\out\$Out.c ($((Get-Content $outFile).Count) lignes)" } else { Get-Content "$re\out\decomp.log" -Tail 20 }

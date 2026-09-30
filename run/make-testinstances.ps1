# Cree les instances de test SACoop-Joueur1..N a partir de la copie 1.0 US de JD (jamais modifiee).
# Les fichiers du jeu sont des liens durs (aucune copie), sauf les archives .img (lues en E/S asynchrone :
# partagees par lien dur entre deux instances, les lectures se bloquaient sur VCCoop). sacoop.ini, la DLL et les
# sauvegardes sont propres a chaque instance. Port de test : 7898 (jamais celui d'une vraie partie).
param([int]$Count = 2)
$src = 'D:\Games\COOPTEST\Grand Theft Auto San Andreas'
$base = 'D:\Games\COOPTEST\GTA San Andreas'
for ($n = 1; $n -le $Count; $n++) {
  $dst = "$base\SACoop-Joueur$n"
  foreach ($f in Get-ChildItem -LiteralPath $src -Recurse -File) {
    $rel = $f.FullName.Substring($src.Length + 1)
    if ($rel -like 'backups\*' -or $rel -like 'GTA San Andreas User Files*' -or $rel -eq 'gta-sa.exe' -or $rel -like 'sacoop*' -or $rel -eq 'dinput8.dll') { continue }
    $target = Join-Path $dst $rel
    if (Test-Path -LiteralPath $target) { continue }
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    if ($f.Extension -eq '.img') { Copy-Item -LiteralPath $f.FullName $target } else { New-Item -ItemType HardLink -Path $target -Target $f.FullName | Out-Null }
  }
  $x = -4000 + ($n - 1) * 700
  $role = if ($n -eq 1) { 'hote' } else { 'invite' }
  $test = if ($n -eq 1) { 'marche' } else { '' }
  Set-Content "$dst\sacoop.ini" -Encoding ascii -Value @"
[SACoop]
Fenetre=1
FenetreX=$x
FenetreY=100
TailleFenetre=960x540
ArrierePlan=1
ImagesParSeconde=30
Role=$role
Adresse=127.0.0.1
Port=7898
AutoDemarrer=1
Reseau=1
JournalScripts=1
Autotest=$test
"@
  Set-Content "$dst\sacoop-joueur.ini" -Encoding ascii -Value @"
[SACoop]
Pseudo=Joueur$n
Adresse=127.0.0.1
Port=7898
"@
  "instance $n : $dst"
}

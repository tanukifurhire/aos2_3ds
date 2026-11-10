# Ghidra headless wrapper using the project-local JDK.
#   ghidra.ps1 analyze              import + full auto-analysis of aos2.exe (slow, once)
#   ghidra.ps1 index                export functions/strings/vtables to re\out
#   ghidra.ps1 decomp <list> <out>  decompile addresses listed in a file
param([string]$cmd, [string]$a1, [string]$a2)
$ErrorActionPreference = 'Stop'
$T = Resolve-Path "$PSScriptRoot\.."
$env:JAVA_HOME = (Get-ChildItem "$T\jdk" -Directory | Select-Object -First 1).FullName
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
$H = "$T\ghidra\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat"
$P = "$T\re\project"; New-Item -ItemType Directory -Force $P | Out-Null
$EXE = Resolve-Path "$T\..\..\Acceleration of SUGURI 2\aos2.exe"
$S = "$T\re\ghidra_scripts"
switch ($cmd) {
  'analyze' { & $H $P aos2 -import "$EXE" -overwrite -analysisTimeoutPerFile 14400 }
  'index'   { & $H $P aos2 -process aos2.exe -noanalysis -readOnly -scriptPath $S -postScript ExportIndex.java "$T\re\out" }
  'decomp'  { & $H $P aos2 -process aos2.exe -noanalysis -readOnly -scriptPath $S -postScript Decompile.java (Resolve-Path $a1) $a2 }
  default   { throw "usage: ghidra.ps1 analyze|index|decomp <list> <out>" }
}
exit $LASTEXITCODE

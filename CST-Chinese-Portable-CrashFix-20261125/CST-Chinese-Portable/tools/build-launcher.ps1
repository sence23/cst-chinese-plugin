param([switch]$Test)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (!(Test-Path -LiteralPath $csc)) { throw '.NET Framework 4.x compiler missing.' }
& $csc /nologo /target:winexe /platform:anycpu /optimize+ "/out:$root\CstChineseLauncher.exe" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll "$root\src\Launcher.cs"
if ($LASTEXITCODE -ne 0) { throw 'Launcher compilation failed.' }
if ($Test) {
    $testDir = Join-Path $root '.test-work'
    New-Item -ItemType Directory -Path $testDir -Force | Out-Null
    & $csc /nologo /target:exe /main:LauncherTests "/out:$testDir\LauncherTests.exe" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll "$root\src\Launcher.cs" "$root\tests\LauncherTests.cs"
    if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
    & "$testDir\LauncherTests.exe" $testDir
    if ($LASTEXITCODE -ne 0) { throw 'Launcher tests failed.' }
}

param(
    [switch] $Verify,
    [string] $CstRoot = $env:CST_2025_ROOT,
    [string] $QtSdk = $env:CST_QT_SDK,
    [string] $VsInstallPath = '',
    [switch] $CheckEnvironment
)

$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
if (!$CstRoot) { throw 'Pass -CstRoot <CST installation directory> or set CST_2025_ROOT.' }
$vendor = Join-Path $CstRoot 'AMD64'
# Accept official Qt (include/QtCore) and conda Qt (include/qt/QtCore).
$problems = @()
if (!$QtSdk) {
    $mocCommand = Get-Command moc.exe -ErrorAction SilentlyContinue
    if ($mocCommand) { $QtSdk = Split-Path (Split-Path $mocCommand.Source) }
}
if (!$QtSdk) {
    $problems += 'Qt 5 SDK missing. Pass -QtSdk <directory> or set CST_QT_SDK.'
} else {
    $include = Join-Path $QtSdk 'include'
    if (Test-Path -LiteralPath (Join-Path $include 'qt\QtCore')) {
        $include = Join-Path $include 'qt'
    }
    $moc = Join-Path $QtSdk 'bin\moc.exe'
    $rcc = Join-Path $QtSdk 'bin\rcc.exe'
    foreach ($required in @($moc, $rcc, (Join-Path $include 'QtCore\qglobal.h'))) {
        if (!(Test-Path -LiteralPath $required)) { $problems += "Missing Qt SDK file: $required" }
    }
}
foreach ($module in 'Core','Gui','Widgets') {
    $required = Join-Path $vendor "Qt5${module}CST_AMD64.dll"
    if (!(Test-Path -LiteralPath $required)) { $problems += "Missing CST runtime: $required" }
}
$pluginRoot = Join-Path $source 'plugins'
$destination = Join-Path $pluginRoot 'generic'
$work = Join-Path ([IO.Path]::GetTempPath()) ("CstRibbonZh-build-" + [guid]::NewGuid().ToString('N'))

function Check-Exit([string] $step) {
    if ($LASTEXITCODE -ne 0) { throw "$step failed with exit code $LASTEXITCODE" }
}

function Build-ImportLibrary([string] $module) {
    $dll = "Qt5${module}CST_AMD64.dll"
    $binary = Join-Path $vendor $dll
    $exports = & $link /dump /nologo /exports $binary
    Check-Exit "Reading exports from $dll"
    $symbols = @(foreach ($line in $exports) {
        if ($line -match '^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') {
            '    ' + $Matches[1]
        }
    })
    if (!$symbols.Count) { throw "No named exports found in $binary" }
    $definition = Join-Path $work "$module.def"
    $library = Join-Path $work "$module.lib"
    @("LIBRARY $dll", 'EXPORTS') + $symbols | Set-Content -LiteralPath $definition -Encoding ASCII
    & $libTool /nologo /machine:x64 "/def:$definition" "/out:$library" | Out-Host
    Check-Exit "Generating $module import library"
    return $library
}

$vs = $VsInstallPath
if (!$vs -and !(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (!$vs) { $problems += 'MSVC x64 compiler missing. Install Desktop development with C++ (including Windows SDK), or pass -VsInstallPath.' }
}
if ($vs) {
    $devShell = Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    if (!(Test-Path -LiteralPath $devShell)) { $problems += "Missing Visual Studio DevShell: $devShell" }
}
if ($problems.Count) { throw ($problems -join [Environment]::NewLine) }
if ($vs) {
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $vs -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}
foreach ($tool in 'cl.exe','link.exe','lib.exe') {
    if (!(Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing build tool: $tool" }
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64' -and $env:VSCMD_ARG_TGT_ARCH -ne 'amd64') {
    throw 'Use an x64 Visual Studio Developer PowerShell, or pass -VsInstallPath.'
}
$mocVersion = & $moc -v 2>&1
if ($LASTEXITCODE -ne 0 -or "$mocVersion" -notmatch '5\.15\.') {
    throw "Qt 5.15 SDK required for this CST plugin. Found: $mocVersion"
}
if ($CheckEnvironment) { Write-Host "PASS MSVC x64, Qt SDK $QtSdk, CST $CstRoot"; return }
$link = (Get-Command link.exe).Source
$libTool = (Get-Command lib.exe).Source
$cl = (Get-Command cl.exe).Source
$header = Join-Path $source 'ribbonplugin.h'
if (!(Test-Path -LiteralPath $header) -or !(Test-Path -LiteralPath $moc)) {
    throw 'Missing ribbonplugin.h or Qt SDK moc.exe.'
}

New-Item -ItemType Directory -Path $work | Out-Null
Push-Location -LiteralPath $work
try {
    $libs = @(foreach ($module in 'Core', 'Gui', 'Widgets') { Build-ImportLibrary $module })
    $includes = @("/I$source", "/I$include", "/I$include\QtCore", "/I$include\QtGui", "/I$include\QtWidgets")
    $flags = @('/nologo', '/EHsc', '/std:c++17', '/O2', '/MD', '/utf-8', '/DNDEBUG', '/DQT_NO_DEBUG',
               '/DQT_CORE_LIB', '/DQT_GUI_LIB', '/DQT_WIDGETS_LIB') + $includes
    & $moc "-I$include" "-I$include\QtCore" $header -o 'moc_ribbonplugin.cpp'
    Check-Exit 'Running Qt moc'
    $dictionary = @(Get-ChildItem -LiteralPath (Join-Path $source 'i18n') -Filter '*.tsv' -File |
        Where-Object Name -ne 'user.tsv' | Sort-Object Name)
    if (!$dictionary.Count) { throw 'No built-in translation catalogs.' }
    $resources = foreach ($file in $dictionary) {
        $alias = [Security.SecurityElement]::Escape($file.Name)
        $path = [Security.SecurityElement]::Escape($file.FullName.Replace('\','/'))
        "<file alias=`"$alias`">$path</file>"
    }
    $webResources = foreach ($name in 'wizard.js','wizard-zh.json') {
        $path = [Security.SecurityElement]::Escape((Join-Path $source "web\$name").Replace('\','/'))
        "<file alias=`"$name`">$path</file>"
    }
    @('<RCC><qresource prefix="/cst-zh">') + $resources + '</qresource><qresource prefix="/cst-zh-web">' + $webResources + '</qresource></RCC>' |
        Set-Content -LiteralPath 'catalog.qrc' -Encoding UTF8
    & $rcc 'catalog.qrc' -name cstzh -o 'qrc_catalog.cpp'
    Check-Exit 'Embedding translation catalogs'
    $sources = @(foreach ($name in 'ribbonplugin.cpp','catalog.cpp','surface.cpp','nativeui.cpp','webui.cpp') {
        Join-Path $source $name
    })
    & $cl @flags /LD @sources 'moc_ribbonplugin.cpp' 'qrc_catalog.cpp' @libs user32.lib /link /out:CstRibbonZh.dll
    Check-Exit 'Compiling Qt plugin'

    if ($Verify) {
        $testPlugins = Join-Path $work 'plugins'
        $testGeneric = Join-Path $testPlugins 'generic'
        [void][IO.Directory]::CreateDirectory($testGeneric)
        $plugin = Join-Path $testGeneric 'CstRibbonZh.dll'
        Copy-Item -LiteralPath (Join-Path $work 'CstRibbonZh.dll') -Destination $plugin
        $smokeSource = Join-Path $source 'smoke.cpp'
        & $moc "-I$include" "-I$include\QtCore" $smokeSource -o 'smoke.moc'
        Check-Exit 'Running Qt moc on smoke test'
        & $cl @flags "/I$work" $smokeSource @libs user32.lib /link /out:CstRibbonZhSmoke.exe
        Check-Exit 'Compiling smoke test'
        $oldPath = $env:Path
        $oldPlatform = $env:QT_QPA_PLATFORM
        $oldPlugins = $env:QT_PLUGIN_PATH
        $oldGeneric = $env:QT_QPA_GENERIC_PLUGINS
        $oldCatalog = $env:CST_ZH_USER_CATALOG
        try {
            $env:Path = "$vendor;$oldPath"
            $env:QT_PLUGIN_PATH = "$testPlugins;$vendor"
            $env:QT_QPA_PLATFORM = 'offscreen'
            $env:QT_QPA_GENERIC_PLUGINS = 'cstribbonzh'
            $env:CST_ZH_USER_CATALOG = Join-Path $work 'test-override.tsv'
            [IO.File]::WriteAllText($env:CST_ZH_USER_CATALOG,
                [char]0xfeff + "Fixture override`t" + [char]0x6D4B + [char]0x8BD5 +
                "`nReject %1`tbad`nValue %1`t" + [char]0x503C + " %1`nBad %ld`tbad %lf`nOrder %s %d`tbad %d %s`n")
            & (Join-Path $work 'CstRibbonZhSmoke.exe') '--verify-plugin' $plugin
            Check-Exit 'Offscreen Qt plugin smoke test'
            $env:QT_QPA_GENERIC_PLUGINS = $null
            & (Join-Path $work 'CstRibbonZhSmoke.exe') '--verify-host-guard' $plugin
            Check-Exit 'Qt plugin host guard test'
        } finally {
            $env:Path = $oldPath
            $env:QT_QPA_PLATFORM = $oldPlatform
            $env:QT_PLUGIN_PATH = $oldPlugins
            $env:QT_QPA_GENERIC_PLUGINS = $oldGeneric
            $env:CST_ZH_USER_CATALOG = $oldCatalog
        }
    }
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $plugin = Join-Path $destination 'CstRibbonZh.dll'
    Copy-Item -LiteralPath (Join-Path $work 'CstRibbonZh.dll') -Destination $plugin -Force
    Write-Host "Built $plugin"
} finally {
    Pop-Location
    # This private build directory contains only files produced by the build commands.
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    $checked = [IO.Path]::GetFullPath($work)
    if ([IO.Path]::GetDirectoryName($checked) -eq $tempRoot -and
        [IO.Path]::GetFileName($checked) -match '^CstRibbonZh-build-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $checked -Recurse -Force
    }
}

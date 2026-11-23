param(
    [Parameter(Mandatory = $true)][string]$Candidate,
    [Parameter(Mandatory = $true)][string]$NativeCompiler
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$dest = (Resolve-Path -LiteralPath $Candidate).Path
$workstreams = Join-Path (Split-Path $source -Parent) 'weapon-workstreams'
$managedCompiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$nativeLibrary = Join-Path $dest 'TacticalWorkshopNative.dll'
$editor = Join-Path $dest 'GoldenEye Tactical Loadout Editor.exe'
$sources = (Get-ChildItem -LiteralPath $PSScriptRoot -File -Filter '*.cs').FullName

& $NativeCompiler -O2 -shared -D_LANGUAGE_C "-I$source/port/include" "-I$source/include" "-I$source/src" "-I$source" "$PSScriptRoot/workshop_native.c" "$source/port/src/gevr_tactical_gear.c" "$source/port/src/gevr_tactical_stats.c" -o $nativeLibrary
if ($LASTEXITCODE -ne 0) { throw 'Native Workshop compilation failed.' }

& $managedCompiler /nologo /target:winexe /platform:x64 /optimize+ /codepage:65001 /reference:System.Windows.Forms.dll,System.Drawing.dll "/out:$editor" $sources
if ($LASTEXITCODE -ne 0) { throw 'Loadout editor compilation failed.' }

python "$PSScriptRoot/prepare_catalogue.py" $dest $workstreams
if ($LASTEXITCODE -ne 0) { throw 'Campaign catalogue generation failed.' }

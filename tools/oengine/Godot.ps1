[CmdletBinding()]
param(
    [ValidateSet('Projects', 'Build', 'Rebuild', 'Clean', 'GenerateMetadata', 'BuildEditor', 'BuildEditorNoMono', 'BuildDotNet', 'BuildTemplates', 'OpenRider', 'DryRun', 'DryRunNoMono')]
    [string]$Action = 'Projects',
    [ValidateSet('Engine', 'Engine Shipping', 'Engine Mono', 'Engine Mono Shipping')]
    [string]$Configuration = 'Engine',
    [ValidateRange(1, 128)][int]$Jobs = 8,
    [switch]$FullProject,
    [string]$Workspace
)
$ErrorActionPreference = 'Stop'
if ($Workspace) { $env:OENGINE_WORKSPACE = [System.IO.Path]::GetFullPath($Workspace) }
. "$PSScriptRoot\Enter-GodotEnv.ps1"
$source = $OEngineSource
$deps = if ($env:OENGINE_DEPS) { $env:OENGINE_DEPS } else { Join-Path $OEngineTools 'Godot\build_deps' }
$deps = $deps.Replace('\', '/')
$python = $OEnginePython
if ($Action -in @('BuildEditor', 'BuildDotNet', 'BuildTemplates')) { $Configuration = 'Engine Mono' }
if ($Action -in @('BuildEditorNoMono', 'DryRunNoMono')) { $Configuration = 'Engine' }
$variants = Get-Content "$PSScriptRoot\EngineConfigurations.json" -Raw | ConvertFrom-Json
$variant = $variants | Where-Object name -EQ $Configuration
if (-not $variant) { throw "Unknown engine configuration: $Configuration" }
$output = Join-Path (Join-Path $OEngineWorkspace 'Package') $variant.folder
$editor = Join-Path "$source\bin" ($variant.binary + '.exe')
$monoEnabled = if ($variant.mono) { 'yes' } else { 'no' }
$objectSuffix = if ($variant.mono) { 'mono' } else { 'nomono' }
$common = @('platform=windows', 'arch=x86_64', "module_mono_enabled=$monoEnabled", "extra_suffix=$objectSuffix", "-j$Jobs",
    "accesskit_sdk_path=$deps/accesskit", "mesa_libs=$deps/mesa", "agility_sdk_path=$deps/agility_sdk",
    "pix_path=$deps/pix", "angle_libs=$deps/angle")
$editorOptions = @('target=editor', "windows_binary_name=$($variant.binary)")
if ($variant.shipping) {
    $editorOptions += @('dev_build=no', 'production=yes', 'optimize=speed', 'lto=none', 'debug_symbols=yes')
} else {
    $editorOptions += @('dev_build=yes', 'optimize=none', 'debug_symbols=yes')
}
function Invoke-SCons([string[]]$Options) {
    & $python -m SCons @common @Options
    if ($LASTEXITCODE -ne 0) { throw "SCons failed: $LASTEXITCODE" }
}
function Build-ManagedLibraries {
    if (-not (Test-Path -LiteralPath $editor)) { throw "Build $Configuration first: $editor is missing." }
    $glue = Start-Process -FilePath $editor -ArgumentList @('--headless', '--generate-mono-glue', 'modules/mono/glue') -WorkingDirectory $source -Wait -PassThru -WindowStyle Hidden
    if ($glue.ExitCode -ne 0) { throw "Generating .NET glue failed: $($glue.ExitCode)" }
    & $python modules/mono/build_scripts/build_assemblies.py --godot-output-dir bin --godot-platform windows --push-nupkgs-local (Join-Path $OEngineTools 'nuget-local')
    if ($LASTEXITCODE -ne 0) { throw 'Building .NET assemblies failed.' }
}
function Publish-Editor {
    New-Item -ItemType Directory -Force $output | Out-Null
    foreach ($extension in @('.exe', '.pdb', '.console.exe', '.console.pdb')) {
        $artifact = Join-Path "$source\bin" ($variant.binary + $extension)
        if (Test-Path -LiteralPath $artifact) { Copy-Item -LiteralPath $artifact -Destination $output -Force }
    }
    foreach ($dll in @('D3D12Core.dll', 'd3d12SDKLayers.dll')) {
        Copy-Item -LiteralPath "$source\bin\$dll" -Destination $output -Force
    }
    if ($variant.mono) {
        $managedOutput = Join-Path $output 'GodotSharp'
        New-Item -ItemType Directory -Force $managedOutput | Out-Null
        Get-ChildItem -LiteralPath "$source\bin\GodotSharp" | Copy-Item -Destination $managedOutput -Recurse -Force
    }
    Write-Host "Ready: $(Join-Path $output ($variant.binary + '.exe'))"
}
function Build-Editor {
    Invoke-SCons $editorOptions
    if ($variant.mono) { Build-ManagedLibraries }
    Publish-Editor
}
Push-Location $source
try {
    switch ($Action) {
        'Projects' {
            & $python "$PSScriptRoot\configure_rider.py"
            if ($LASTEXITCODE -ne 0) { throw 'Rider project generation failed.' }
        }
        'GenerateMetadata' {
            $metadata = $editorOptions + @('vsproj=yes', 'vsproj_gen_only=yes')
            if (-not $FullProject) { $metadata += 'vsproj_props_only=yes' }
            Invoke-SCons $metadata
        }
        'Build' { Build-Editor }
        'BuildEditor' { Build-Editor }
        'BuildEditorNoMono' { Build-Editor }
        'Rebuild' {
            Invoke-SCons ($editorOptions + @('--clean'))
            Build-Editor
        }
        'Clean' { Invoke-SCons ($editorOptions + @('--clean')) }
        'DryRun' { Invoke-SCons ($editorOptions + @('--dry-run')) }
        'DryRunNoMono' { Invoke-SCons ($editorOptions + @('--dry-run')) }
        'BuildDotNet' { Build-ManagedLibraries; Publish-Editor }
        'BuildTemplates' {
            Invoke-SCons @('target=template_debug')
            Invoke-SCons @('target=template_release')
        }
        'OpenRider' {
            $rider = if ($env:OENGINE_RIDER) { $env:OENGINE_RIDER } else { Join-Path $env:ProgramFiles 'JetBrains\Rider\bin\rider64.exe' }
            if (-not (Test-Path -LiteralPath $rider)) { throw 'Set OENGINE_RIDER to the installed rider64.exe path.' }
            Start-Process -FilePath $rider -ArgumentList ('"{0}"' -f "$source\godot.sln") -WindowStyle Hidden
        }
    }
} finally { Pop-Location }

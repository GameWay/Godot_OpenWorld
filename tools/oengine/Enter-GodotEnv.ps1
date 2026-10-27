$ErrorActionPreference = 'Stop'
$OEngineSource = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$OEngineWorkspace = if ($env:OENGINE_WORKSPACE) {
    [System.IO.Path]::GetFullPath($env:OENGINE_WORKSPACE)
} else {
    Split-Path -Parent $OEngineSource
}
$env:OENGINE_WORKSPACE = $OEngineWorkspace
$OEngineTools = Join-Path $OEngineWorkspace '.tools'
if ($env:OENGINE_PYTHON) {
    $OEnginePython = (Get-Command $env:OENGINE_PYTHON -CommandType Application -ErrorAction Stop).Source
} elseif (Test-Path -LiteralPath (Join-Path $OEngineTools 'python\Scripts\python.exe')) {
    $OEnginePython = Join-Path $OEngineTools 'python\Scripts\python.exe'
} else {
    $OEnginePython = (Get-Command python -CommandType Application -ErrorAction Stop).Source
}
$env:OENGINE_PYTHON = $OEnginePython
$env:PATH = "$(Split-Path -Parent $OEnginePython);$env:PATH"
$env:TEMP = Join-Path $OEngineTools 'tmp'
$env:TMP = $env:TEMP
$env:NUGET_PACKAGES = Join-Path $OEngineTools 'nuget-packages'
$env:PYTHONUTF8 = '1'
New-Item -ItemType Directory -Force -Path $env:TEMP,$env:NUGET_PACKAGES,(Join-Path $OEngineTools 'nuget-local') | Out-Null
# Managed projects discover the workspace config through their parent folders.
# An existing workspace configuration belongs to the user and is left intact.
$OEngineNuGetConfig = Join-Path $OEngineWorkspace 'NuGet.Config'
if (-not (Test-Path -LiteralPath $OEngineNuGetConfig)) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'NuGet.Config.in') -Destination $OEngineNuGetConfig
}
# MSBuild reads this environment property even when Workspace is not an ancestor
# of the source checkout.
$env:RestoreConfigFile = $OEngineNuGetConfig

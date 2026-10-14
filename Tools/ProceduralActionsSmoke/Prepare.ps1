param(
    [Parameter(Mandatory = $true)]
    [string]$HostProject,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$templateRoot = $PSScriptRoot
$hostProjectPath = (Resolve-Path -LiteralPath $HostProject -ErrorAction Stop).Path
if ([IO.Path]::GetExtension($hostProjectPath) -ine '.uproject') {
    throw 'HostProject must name a .uproject file.'
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputPath) {
    throw "OutputDirectory already exists: $outputPath"
}

$hostRoot = Split-Path -Parent $hostProjectPath
$hostDescriptor = Get-Content -LiteralPath $hostProjectPath -Raw | ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace([string]$hostDescriptor.EngineAssociation)) {
    throw 'HostProject has no EngineAssociation to copy.'
}

$pluginCandidates = @((Join-Path $hostRoot 'Plugins'))
foreach ($entry in @($hostDescriptor.AdditionalPluginDirectories)) {
    if ([string]::IsNullOrWhiteSpace([string]$entry)) { continue }
    $candidate = [string]$entry
    if (-not [IO.Path]::IsPathRooted($candidate)) {
        $candidate = Join-Path $hostRoot $candidate
    }
    $pluginCandidates += $candidate
}
$resolvedPlugins = $null
foreach ($candidate in $pluginCandidates) {
    $fullCandidate = [IO.Path]::GetFullPath($candidate)
    if (Test-Path -LiteralPath (Join-Path $fullCandidate 'CkFoundation/CkFoundation.uplugin') -PathType Leaf) {
        $resolvedPlugins = (Resolve-Path -LiteralPath $fullCandidate).Path
        break
    }
}
if ($null -eq $resolvedPlugins) {
    throw 'No host Plugins directory contains CkFoundation/CkFoundation.uplugin.'
}

$project = Get-Content -LiteralPath (Join-Path $templateRoot 'ProceduralActionsSmoke.uproject') -Raw |
    ConvertFrom-Json
$association = [string]$hostDescriptor.EngineAssociation
if (-not [IO.Path]::IsPathRooted($association)) {
    $relativeEngine = Join-Path $hostRoot $association
    if (Test-Path -LiteralPath (Join-Path $relativeEngine 'Engine/Build/BatchFiles') -PathType Container) {
        $association = (Resolve-Path -LiteralPath $relativeEngine).Path
    } elseif ($association.StartsWith('.') -or $association.Contains('/') -or $association.Contains('\')) {
        throw "Relative EngineAssociation does not resolve from HostProject: $association"
    }
}
$project.EngineAssociation = $association
$project.AdditionalPluginDirectories = @($resolvedPlugins.Replace('\', '/'))
$seenNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$uniquePlugins = [System.Collections.Generic.List[object]]::new()
foreach ($plugin in @($project.Plugins)) {
    if ([string]::IsNullOrWhiteSpace([string]$plugin.Name)) {
        throw 'Template contains an unnamed plugin declaration.'
    }
    if ($seenNames.Add([string]$plugin.Name)) {
        $uniquePlugins.Add($plugin)
    }
}
$project.Plugins = @($uniquePlugins.ToArray())
$foundation = @($project.Plugins | Where-Object { $_.Name -ieq 'CkFoundation' })
$angelscript = @($project.Plugins | Where-Object { $_.Name -ieq 'Angelscript' })
if ($foundation.Count -ne 1 -or $foundation[0].Enabled -ne $true -or
    $angelscript.Count -ne 1 -or $angelscript[0].Enabled -ne $false) {
    throw 'Template must enable CkFoundation and explicitly disable Angelscript.'
}

New-Item -ItemType Directory -Path $outputPath -ErrorAction Stop | Out-Null
Copy-Item -LiteralPath (Join-Path $templateRoot 'Source') -Destination $outputPath -Recurse
Copy-Item -LiteralPath (Join-Path $templateRoot 'Config') -Destination $outputPath -Recurse
$project | ConvertTo-Json -Depth 20 |
    Set-Content -LiteralPath (Join-Path $outputPath 'ProceduralActionsSmoke.uproject') -Encoding utf8
Write-Output (Join-Path $outputPath 'ProceduralActionsSmoke.uproject')

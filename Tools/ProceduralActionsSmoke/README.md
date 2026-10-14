# Procedural Actions native smoke host

This is a relocatable copy of the task-owned native AS-off host. Its runtime module rejects `WITH_ANGELSCRIPT_CK`, creates a body-only articulation and loaded action, checks accepted presentation movement, and exits only after a completed terminal receipt with successful recovery. The template has no engine registration or absolute plugin path; `Prepare.ps1` takes both from a host `.uproject`. It refuses to replace an existing output directory.

The following is the verification sequence for a **new** output directory. Run it from the Orion repository root with the editor closed. Use a new output path for each run; do not reuse `Saved/ProceduralActionsMatrix/ASOff`.

```powershell
$hostProject = (Resolve-Path -LiteralPath '.\Mars.uproject').Path
$smokeRoot = Join-Path (Get-Location).Path ('Saved/ProceduralActionsMatrix/ASOff-' + [guid]::NewGuid().ToString('N'))
$smokeProject = & '.\Plugins\CkTests\Tools\ProceduralActionsSmoke\Prepare.ps1' -HostProject $hostProject -OutputDirectory $smokeRoot
[IO.Directory]::CreateDirectory((Join-Path $smokeRoot 'Saved/Logs')) | Out-Null
$toolbox = (Resolve-Path -LiteralPath '.\CkAuto\UnrealToolbox.exe').Path
& $toolbox --build --target=Editor --project="$smokeRoot" --output="$smokeRoot/Saved/Logs/EditorBuild.log"
if ($LASTEXITCODE -ne 0) { throw 'Editor build failed' }
& $toolbox --build --target=Game --project="$smokeRoot" --output="$smokeRoot/Saved/Logs/GameBuild.log"
if ($LASTEXITCODE -ne 0) { throw 'Game build failed' }
$gameBuildLog = Get-Content -LiteralPath (Join-Path $smokeRoot 'Saved/Logs/GameBuild.log') -Raw
$configs = @([regex]::Matches($gameBuildLog,
    'ProceduralActionsSmoke Win64 (Development|DebugGame|Shipping|Test)\b') |
    ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
if ($configs.Count -ne 1) { throw 'Cannot identify the actual Game build configuration from the Toolbox receipt' }
$gameConfig = $configs[0]
```

Cooking only uses UAT after both Toolbox builds. `-skipbuild` and `-skipbuildeditor` keep it out of the build lane. The engine resolver is the host's project association, not a hardcoded installation path. This follows the existing `Saved/RuntimeMesh/Run-CookGate.ps1` cook/stage invocation.

For the already prepared `Saved/ProceduralActionsMatrix/ASOff-Repro-01` host, skip preparation and use `Saved/ProceduralActionsMatrix/Run-NativeSmoke.ps1 -Stage Editor`, then `Game`, `Cook`, and `Run` in order. Its runtime stage has a 180-second outer process deadline in addition to the module's 90-second watchdog; a timeout invalidates the run.

```powershell
$engineRoot = & '.\CkAuto\Get-ProjectEnginePath.ps1' -ProjectRoot (Split-Path -Parent $hostProject)
$stageRoot = Join-Path $smokeRoot 'Saved/NativeSmokeStage'
$cookRoot = Join-Path $smokeRoot 'Saved/NativeSmokeCook/Windows'
& (Join-Path $engineRoot 'Engine/Build/BatchFiles/RunUAT.bat') `
    -nocompile BuildCookRun -nop4 -utf8output -unattended `
    "-project=$smokeProject" -target=ProceduralActionsSmoke -platform=Win64 `
    "-clientconfig=$gameConfig" -skipbuild -skipbuildeditor -nocompileeditor `
    -cook -stage -package '-map=/Engine/Maps/Entry' `
    "-CookOutputDir=$cookRoot" "-stagingdirectory=$stageRoot"
if ($LASTEXITCODE -ne 0) { throw 'Cook/stage failed' }
$gameBinaryName = if ($gameConfig -eq 'Development') { 'ProceduralActionsSmoke' }
    else { "ProceduralActionsSmoke-Win64-$gameConfig" }
$gameExecutables = @(Get-ChildItem -LiteralPath $stageRoot -Recurse -File -Filter '*.exe' |
    Where-Object { $_.BaseName -eq $gameBinaryName -and
        $_.DirectoryName -match '[\\/]Binaries[\\/]Win64$' })
if ($gameExecutables.Count -ne 1) {
    throw "Inspect the staged Binaries/Win64 path: expected one $gameBinaryName executable, found $($gameExecutables.Count)"
}
& $gameExecutables[0].FullName -game -nullrhi -unattended -stdout -FullStdOutLogOutput
if ($LASTEXITCODE -ne 0) { throw "Native runtime smoke exited $LASTEXITCODE" }
```

The runtime pass marker is `PROCEDURAL_ACTIONS_NATIVE_SMOKE result=PASS accepted=... reason=ObservedTerminal WITH_EDITOR=0`; the process must also exit 0. A compile alone does not certify the runtime path. The copied C++ source, config, and target files are the same as the original scratch host at the time this template was created. This new template and preparation flow have not yet been built, cooked, or run; record those results separately before claiming this recipe is verified.

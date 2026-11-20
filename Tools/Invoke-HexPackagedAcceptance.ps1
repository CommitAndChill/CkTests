<#
.SYNOPSIS
Exercises a staged Win64 Development server with a separately launched late client.
.DESCRIPTION
Package /CkTests/CkGrid/HexGrid/HexPackagedAcceptance for both Development Game and Server. Set
CK_HEX_PACKAGED_ACCEPTANCE_COOK=1 on the packaging processes so CkTestsEditor
includes /CkTests/CkGrid/HexGrid/DA_HexGrid_PersistenceBoard. Supply the inner
target binaries from CkPlugins/Binaries/Win64 to monitor the actual processes.
Both logs and the per-run handshake receipts go to EvidenceDirectory.
server-metrics.csv and client-metrics.csv contain a single functional-fixture sample.
The save byte count covers the main slot. Load and connection timings end when the
0.1-second fixture poll observes validated state; they include world and engine work.
.PARAMETER DenseDensity
Selects the separate two-board radius-32 durable fixture. Each process pair supplies
one sample. Save bytes are counted per reflected payload type from the persisted slot.
The runner requests 25 ms outgoing packet lag on each peer and 1,250,000 B/s game
connection budgets. These settings do not establish the physical link rate or RTT.
Check the recorded effective connection netspeed and observed lag; zero lag can mean
no completed statistics period. Connection byte counters include non-grid traffic.
Use network trace for attributable serialized bits. A qualified 10 Mbps / 50 ms link
requires independently verified shaping. Repeat fresh process pairs for percentiles.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ServerExecutable,
    [Parameter(Mandatory)][string]$ClientExecutable,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [ValidateRange(1024,65535)][int]$Port = 17777,
    [ValidateRange(0,65535)][int]$ClientConnectPort = 0,
    [ValidateRange(0,10000)][int]$PacketLagMs = 25,
    [ValidateSet(50,90)][int]$DenseDensity,
    [switch]$ObserveDenseRelayCollision
)

$ErrorActionPreference = 'Stop'
$relayMode = $ClientConnectPort -ne 0
if ($relayMode -and ($ClientConnectPort -lt 1024 -or $ClientConnectPort -eq $Port)) {
    throw 'Relay frontend must use a distinct nonprivileged port.'
}
$connectPort = if ($relayMode) { $ClientConnectPort } else { $Port }
$serverPath = (Resolve-Path -LiteralPath $ServerExecutable).Path
$clientPath = (Resolve-Path -LiteralPath $ClientExecutable).Path
if ($serverPath -eq $clientPath) { throw 'Use separate staged Development Server and Game target binaries.' }
if (![IO.Path]::IsPathRooted($EvidenceDirectory)) { throw 'Use an absolute evidence directory.' }
$basePath = [IO.Path]::GetFullPath($EvidenceDirectory)
[void][IO.Directory]::CreateDirectory($basePath)
$runId = [Guid]::NewGuid().ToString('N')
$evidence = Join-Path $basePath $runId
$serverLog = Join-Path $basePath "$runId-server.log"
$clientLog = Join-Path $basePath "$runId-client.log"
$dense = $PSBoundParameters.ContainsKey('DenseDensity')
if ($ObserveDenseRelayCollision -and !$dense) { throw 'Relay collision observation requires -DenseDensity.' }
$receiptPrefix = if ($ObserveDenseRelayCollision) { 'observation-' } else { '' }
$verdictPrefix = if ($dense) { 'HexDense' } else { 'HexPackaged' }
$timeoutMinutes = if ($dense) { 90 } else { 8 }
$common = @('-unattended', '-nosplash', '-nosound', "-CkHexRun=$runId",
    ('-CkHexEvidence="{0}"' -f $basePath))
$serverOnly = @()
$clientOnly = @()
if ($ObserveDenseRelayCollision) {
    $common += '-CkHexObserveRelayCollision'
    $serverOnly = @('-MULTIHOME=127.0.0.1',('-UserDir="{0}"' -f (Join-Path $basePath "$runId-server-user")))
    $clientOnly = @(('-UserDir="{0}"' -f (Join-Path $basePath "$runId-client-user")))
}
$serverTrace = @()
$clientTrace = @()
if ($relayMode) { $serverOnly += '-MULTIHOME=127.0.0.1' }
if ($dense) {
    $common += @('-CkHexDenseAcceptance', "-CkHexDensity=$DenseDensity", "-PktLag=$PacketLagMs", '-NetTrace=3', '-trace=net',
        '-ini:Engine:[/Script/Engine.Player]:ConfiguredInternetSpeed=1250000',
        '-ini:Engine:[/Script/Engine.Player]:ConfiguredLanSpeed=1250000',
        '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:MaxClientRate=1250000',
        '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:MaxInternetClientRate=1250000')
    $serverTrace = @(('-tracefile="{0}"' -f (Join-Path $basePath "$runId-server.utrace")))
    $clientTrace = @(('-tracefile="{0}"' -f (Join-Path $basePath "$runId-client.utrace")))
} else {
    $common += '-CkHexPackagedAcceptance'
}
$ready = Join-Path $evidence ($receiptPrefix + 'server-ready')
$server = $null
$client = $null
try {
    $server = Start-Process -FilePath $serverPath -ArgumentList (@('/CkTests/CkGrid/HexGrid/HexPackagedAcceptance',
        "-port=$Port", ('-abslog="{0}"' -f $serverLog)) + $common + $serverOnly + $serverTrace) -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddMinutes($timeoutMinutes)
    while (!(Test-Path -LiteralPath $ready -PathType Leaf)) {
        if ($server.HasExited) { throw "Server exited before readiness ($($server.ExitCode)). See $serverLog" }
        if ([DateTime]::UtcNow -ge $deadline) { throw "No restored-server receipt. See $serverLog" }
        Start-Sleep -Milliseconds 300
    }
    $client = Start-Process -FilePath $clientPath -ArgumentList (@("127.0.0.1:$connectPort", '-nullrhi',
        ('-abslog="{0}"' -f $clientLog)) + $common + $clientOnly + $clientTrace) -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddMinutes($timeoutMinutes)
    while (!$server.HasExited -or !$client.HasExited) {
        foreach ($process in @($server, $client)) {
            if ($process.HasExited -and $process.ExitCode -ne 0) { throw "Process $($process.Id) failed with $($process.ExitCode). See $basePath" }
        }
        if ([DateTime]::UtcNow -ge $deadline) { throw "Two-process acceptance timed out. See $basePath" }
        Start-Sleep -Milliseconds 300
    }
    if ($ObserveDenseRelayCollision) {
        $dig = [IO.File]::ReadAllText($ready)
        foreach ($name in 'client-observed','client-teardown') {
            if ([IO.File]::ReadAllText((Join-Path $evidence ($receiptPrefix+$name))) -cne $dig) { throw "Observation receipt mismatch $name" }
        }
        foreach ($name in 'authority-manifest.csv','client-manifest.csv') {
            if ((Get-FileHash -LiteralPath (Join-Path $evidence ($receiptPrefix+$name)) -Algorithm SHA1).Hash -ne $dig) { throw "Manifest mismatch $name" }
        }
        foreach ($name in 'server-ready','client-observed','client-teardown') {
            if (Test-Path -LiteralPath (Join-Path $evidence $name)) { throw "Unexpected default acceptance artifact $name" }
        }
        $diagnostic = Get-Content (Join-Path $evidence 'observation-inspection-rejected.txt')
        if ($diagnostic[0] -ne 'table_parsed=1 errors=1' -or $diagnostic[1] -notmatch '^DuplicateSaveKey ') { throw 'Missing exact inspector rejection.' }
        $witnesses = @(Import-Csv (Join-Path $evidence 'observation-relay-witnesses.csv'))
        if ($witnesses.Count -ne 4) { throw 'Expected two source and two restored relay witnesses.' }
        foreach ($stage in 'capture','restored') {
            $pair = @($witnesses | Where-Object stage -eq $stage)
            if ($pair.Count -ne 2 -or @($pair.live_id | Sort-Object -Unique).Count -ne 2 -or
                @($pair | Where-Object { [long]$_.live_id -le 0 -or $_.group -cne 'ActorRelay.Generic' -or
                    $_.key -ne '55ECF44A40F43F42B177522EE2DB9F6F' }).Count) { throw "Invalid $stage relay witnesses." }
        }
        $filled = [math]::Floor(3169*$DenseDensity/100)
        $payloads = @(Import-Csv (Join-Path $evidence 'observation-payload-rows.csv'))
        $capture = @(Import-Csv (Join-Path $evidence 'observation-server-metrics.csv'))
        $bounds = @(Import-Csv (Join-Path $evidence 'observation-client-upper-bound.csv'))
        $network = @(Import-Csv (Join-Path $evidence 'observation-client-metrics.csv'))
        $serverNetwork = @(Import-Csv (Join-Path $evidence 'observation-server-connection.csv'))
        if ($capture.Count -ne 1 -or $bounds.Count -ne 1 -or $network.Count -ne 1 -or $serverNetwork.Count -ne 1 -or
            [int]$capture[0].placements_per_board -ne $filled -or [int]$capture[0].boards -ne 2 -or
            @($payloads.owner_saved_id | Sort-Object -Unique).Count -ne $payloads.Count -or $payloads.Count -ne (2+2*$filled) -or
            [double]$bounds[0].observed_open_to_verified_manifest_ms -le 0 -or
            [double]$bounds[0].subsystem_init_before_browse_to_verified_manifest_ms -lt [double]$bounds[0].observed_open_to_verified_manifest_ms -or
            $bounds[0].observed_open_to_verified_manifest_ms -ne $network[0].observed_open_to_verified_manifest_ms -or
            [long]$network[0].connection_in_bytes -le 0 -or [long]$serverNetwork[0].connection_out_bytes -le 0) { throw 'Observation metrics failed verification.' }
        foreach ($entry in @(@('Ck_RepData_HexGridState',2,'board_payload_bytes'),
                             @('Ck_RepData_HexGridObjectIdentity',(2*$filled),'object_identity_bytes'))) {
            $typed = @($payloads | Where-Object type_path -ceq "/Script/CkGrid.$($entry[0])")
            $sum = ($typed | Measure-Object -Property payload_bytes -Sum).Sum
            if ($typed.Count -ne $entry[1] -or $sum -ne [long]$capture[0].($entry[2]) -or
                @($typed | Where-Object { [long]$_.payload_bytes -le 0 }).Count) { throw "Bad typed rows $($entry[0])" }
        }
        $slotName = "HexDense_$runId"
        $inspected = @(Import-Csv -LiteralPath (Join-Path $evidence 'observation-inspected-slot.csv'))
        $saved = @(Get-ChildItem -LiteralPath (Join-Path $basePath "$runId-server-user") -Recurse -File -Filter "$slotName.sav")
        if ($inspected.Count -ne 1 -or $saved.Count -ne 1 -or $inspected[0].slot -ne $slotName -or
            [long]$inspected[0].bytes -ne $saved[0].Length -or $saved[0].Length -ne [long]$capture[0].main_slot_bytes -or
            $inspected[0].sha256 -ne (Get-FileHash -LiteralPath $saved[0].FullName -Algorithm SHA256).Hash) {
            throw 'Physical main slot differs from the bytes inspected by the authority.'
        }
        $roleEvidence = [ordered]@{}
        $totalDiagnostics = 0
        foreach ($result in @(@($server,$serverLog,'server'),@($client,$clientLog,'client'))) {
            $role = $result[2]
            $logText = Get-Content -LiteralPath $result[1] -Raw
            if ($result[0].ExitCode -ne 0 -or !$logText.Contains("HexDense [$role] OBSERVED_INSPECTION_REJECTED") -or
                $logText -match '(?im)Fatal error:|HexDense \[.*?\] (FAIL|PASS)') { throw "Unverified observation on $role" }
            $trace = Join-Path $basePath "$runId-$role.utrace"
            if (!(Test-Path -LiteralPath $trace) -or (Get-Item -LiteralPath $trace).Length -le 0) { throw "Missing trace $trace" }
            $problemPattern = '(?i):\s*(?:Error|Fatal):|fatal error:|ensure condition failed|handled ensure|CkEnsure:\s*Warning:|script compile error|ZenLoader.*unable to FlushAsyncLoading'
            $problemLines = @(Select-String -LiteralPath $result[1] -Pattern $problemPattern | ForEach-Object Line)
            $diagnosticFile = Join-Path $evidence "$receiptPrefix$role-diagnostics.txt"
            [IO.File]::WriteAllLines($diagnosticFile, [string[]]$problemLines)
            $roleEvidence[$role] = [ordered]@{bootRuntimeClean=($problemLines.Count -eq 0);
                diagnosticLogLineCount=$problemLines.Count;log=$result[1];diagnostics=$diagnosticFile;networkTrace=$trace}
            $totalDiagnostics += $problemLines.Count
        }
        $status = if ($totalDiagnostics) { 'observed-inspection-rejected-boot-runtime-diagnostics-present' }
                  else { 'observed-inspection-rejected' }
        [pscustomobject]@{status=$status;cleanReleaseGate=$false;physicalLinkQualified=$false;inspectionPassed=$false;
           bootRuntimeClean=($totalDiagnostics -eq 0);diagnosticLogLineCount=$totalDiagnostics;roles=$roleEvidence;
           run=$runId;density=$DenseDensity;authorityClientManifestMatched=$true;authorityAndClientTeardownVerified=$true;
           manifestSha1=$dig;authorityManifest=(Join-Path $evidence 'observation-authority-manifest.csv');
           clientManifest=(Join-Path $evidence 'observation-client-manifest.csv');
           clientObserved=(Join-Path $evidence 'observation-client-observed');clientTeardown=(Join-Path $evidence 'observation-client-teardown');
           slotName=$slotName;serverSlot=$saved[0].FullName;slotBytes=$saved[0].Length;inspectedSlotSha256=$inspected[0].sha256;
           inspectorDiagnostics=(Join-Path $evidence 'observation-inspection-rejected.txt');
           clientUpperBound=$bounds[0];clientNetwork=$network[0];serverNetwork=$serverNetwork[0]} |
           ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidence 'observation-result.json')
        Write-Output "$status $evidence ($dig)"
        return
    }
    foreach ($result in @(@($server,$serverLog,'server'),@($client,$clientLog,'client'))) {
        $lines = Get-Content -LiteralPath $result[1] -Raw
        if ($result[0].ExitCode -ne 0 -or !$lines.Contains("$verdictPrefix [$($result[2])] PASS") -or
            $lines -match '(?im)\bCkEnsure:\s*Error:|Ensure condition failed|Script compile error|AngelScript.*: Error:|Fatal error:|Hex(?:Dense|Packaged) \[.*?\] FAIL') {
            throw "Did not obtain a clean verdict from $($result[2]). See $($result[1])"
        }
    }
    $authority = [IO.File]::ReadAllText($ready)
    foreach ($name in @('client-observed','client-teardown')) {
        if ([IO.File]::ReadAllText((Join-Path $evidence $name)) -cne $authority) { throw "Receipt mismatch at $name" }
    }
    Write-Output "PASS $evidence ($authority)"
} finally {
    foreach ($process in @($client,$server)) {
        if ($null -ne $process -and !$process.HasExited) { $process.Kill() }
    }
}

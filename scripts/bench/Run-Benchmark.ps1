param(
    [Parameter(Mandatory = $true)]
    [string]$Manifest,
    [string]$OutputLabel = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $scriptDirectory)

. (Join-Path $scriptDirectory "Benchmark.Common.ps1")
. (Join-Path $scriptDirectory "scenarios\Invoke-ChattingScenario.ps1")

function New-BenchmarkSequenceRow
{
    param(
        [Parameter(Mandatory = $true)]
        [System.Collections.IDictionary]$RunResult,
        [Parameter(Mandatory = $true)]
        [int]$Iteration
    )

    $clientSummary = Get-BenchmarkMapValue -Map $RunResult -Key "ClientSummary" -DefaultValue ([ordered]@{})
    $serverStats = Get-BenchmarkMapValue -Map $RunResult -Key "ServerStats" -DefaultValue ([ordered]@{})
    $contentStats = Get-BenchmarkMapValue -Map $RunResult -Key "ContentStats" -DefaultValue ([ordered]@{})

    return [pscustomobject][ordered]@{
        RunName = $RunResult["RunName"]
        Iteration = $Iteration
        Succeeded = $RunResult["Succeeded"]
        FailureReason = $RunResult["FailureReason"]
        ClientExitCode = $RunResult["ClientExitCode"]
        ServerReady = $RunResult["ServerReady"]
        ServerExitedEarly = $RunResult["ServerExitedEarly"]
        ClientTimedOut = $RunResult["ClientTimedOut"]
        connectSuccess = Get-BenchmarkMapValue -Map $clientSummary -Key "connectSuccess" -DefaultValue $null
        loginSuccess = Get-BenchmarkMapValue -Map $clientSummary -Key "loginSuccess" -DefaultValue $null
        roomChangeSuccess = Get-BenchmarkMapValue -Map $clientSummary -Key "roomChangeSuccess" -DefaultValue $null
        chattingSuccess = Get-BenchmarkMapValue -Map $clientSummary -Key "chattingSuccess" -DefaultValue $null
        broadcastReceive = Get-BenchmarkMapValue -Map $clientSummary -Key "broadcastReceive" -DefaultValue $null
        timeout = Get-BenchmarkMapValue -Map $clientSummary -Key "timeout" -DefaultValue $null
        permanentFailure = Get-BenchmarkMapValue -Map $clientSummary -Key "permanentFailure" -DefaultValue $null
        sendTPS = Get-BenchmarkMapValue -Map $serverStats -Key "sendTPS" -DefaultValue $null
        recvTPS = Get-BenchmarkMapValue -Map $serverStats -Key "recvTPS" -DefaultValue $null
        cpuPercent = Get-BenchmarkMapValue -Map $serverStats -Key "cpuPercent" -DefaultValue $null
        metricSampleCount = Get-BenchmarkMapValue -Map $serverStats -Key "metricSampleCount" -DefaultValue $null
        avgSendTPS = Get-BenchmarkMapValue -Map $serverStats -Key "avgSendTPS" -DefaultValue $null
        avgRecvTPS = Get-BenchmarkMapValue -Map $serverStats -Key "avgRecvTPS" -DefaultValue $null
        avgCpuPercent = Get-BenchmarkMapValue -Map $serverStats -Key "avgCpuPercent" -DefaultValue $null
        maxSendTPS = Get-BenchmarkMapValue -Map $serverStats -Key "maxSendTPS" -DefaultValue $null
        maxRecvTPS = Get-BenchmarkMapValue -Map $serverStats -Key "maxRecvTPS" -DefaultValue $null
        roomPacketTPS = Get-BenchmarkMapValue -Map $contentStats -Key "roomPacketTPS" -DefaultValue $null
        OutputDirectory = $RunResult["OutputDirectory"]
    }
}

function New-BenchmarkFailureResult
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Scenario,
        [Parameter(Mandatory = $true)]
        [string]$RunName,
        [Parameter(Mandatory = $true)]
        [string]$RunDirectory,
        [Parameter(Mandatory = $true)]
        [datetime]$StartedAt,
        [Parameter(Mandatory = $true)]
        [string]$FailureReason
    )

    return [ordered]@{
        Scenario = $Scenario
        RunName = $RunName
        StartedAt = $StartedAt.ToString("o")
        FinishedAt = (Get-Date).ToString("o")
        Succeeded = $false
        FailureReason = $FailureReason
        ServerReady = $false
        ServerExitedEarly = $false
        ClientTimedOut = $false
        ClientExitCode = -1
        OutputDirectory = $RunDirectory
        EffectiveServerConfigPath = (Join-Path $RunDirectory "effective.server.yaml")
        EffectiveClientConfigPath = (Join-Path $RunDirectory "effective.client.yaml")
        ClientSummary = $null
        ServerStats = $null
        ContentStats = $null
    }
}

$manifestPath = Resolve-BenchmarkPath -RootPath $repositoryRoot -PathText $Manifest
$manifestData = ConvertFrom-BenchmarkYamlFile -Path $manifestPath

if (-not (Test-BenchmarkDictionary -Value $manifestData))
{
    throw "Manifest root must be a mapping."
}

$scenarioName = [string](Get-BenchmarkMapValue -Map $manifestData -Key "Scenario" -DefaultValue "")
if ([string]::IsNullOrWhiteSpace($scenarioName))
{
    throw "Manifest 'Scenario' is required."
}

$runs = @(Get-BenchmarkMapValue -Map $manifestData -Key "Runs" -DefaultValue @())
if ($runs.Count -eq 0)
{
    throw "Manifest must contain at least one run in 'Runs'."
}

$outputRootText = [string](Get-BenchmarkMapValue -Map $manifestData -Key "OutputRoot" -DefaultValue "Out/bench")
$outputRoot = Resolve-BenchmarkPath -RootPath $repositoryRoot -PathText $outputRootText
$continueOnError = ConvertTo-BenchmarkBoolean -Value (Get-BenchmarkMapValue -Map $manifestData -Key "ContinueOnError" -DefaultValue $false) -DefaultValue $false
$defaults = Get-BenchmarkMapValue -Map $manifestData -Key "Defaults" -DefaultValue ([ordered]@{})

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$sequenceName = if ([string]::IsNullOrWhiteSpace($OutputLabel)) { $timestamp } else { "{0}_{1}" -f $timestamp, (ConvertTo-BenchmarkSafeName -Text $OutputLabel) }
$sequenceDirectory = Join-Path $outputRoot $sequenceName
New-BenchmarkDirectory -Path $sequenceDirectory | Out-Null
Write-BenchmarkUtf8File -Path (Join-Path $sequenceDirectory "manifest.snapshot.yaml") -Content (Get-Content -Path $manifestPath -Raw -Encoding utf8)

$results = New-Object System.Collections.Generic.List[object]
$summaryRows = New-Object System.Collections.Generic.List[object]
$failedRuns = New-Object System.Collections.Generic.List[string]
$abortRequested = $false

foreach ($run in $runs)
{
    if (-not (Test-BenchmarkDictionary -Value $run))
    {
        throw "Each run entry must be a mapping."
    }

    if ($abortRequested)
    {
        break
    }

    $enabled = ConvertTo-BenchmarkBoolean -Value (Get-BenchmarkMapValue -Map $run -Key "Enabled" -DefaultValue $true) -DefaultValue $true
    if (-not $enabled)
    {
        continue
    }

    $runName = [string](Get-BenchmarkMapValue -Map $run -Key "Name" -DefaultValue "")
    if ([string]::IsNullOrWhiteSpace($runName))
    {
        throw "Each run entry must have a 'Name'."
    }

    $repeatCount = ConvertTo-BenchmarkInt -Value (Get-BenchmarkMapValue -Map $run -Key "RepeatCount" -DefaultValue 1) -DefaultValue 1
    if ($repeatCount -lt 1)
    {
        $repeatCount = 1
    }

    for ($iteration = 1; $iteration -le $repeatCount; ++$iteration)
    {
        $effectiveRun = Merge-BenchmarkObjects -BaseValue $defaults -OverrideValue $run
        $runInstanceName = if ($repeatCount -gt 1) { "{0}.r{1}" -f $runName, $iteration } else { $runName }
        $runDirectory = Join-Path $sequenceDirectory (ConvertTo-BenchmarkSafeName -Text $runInstanceName)
        New-BenchmarkDirectory -Path $runDirectory | Out-Null
        $effectiveRun["Name"] = $runInstanceName

        $runStartedAt = Get-Date
        try
        {
            switch ($scenarioName.ToLowerInvariant())
            {
                "chatting"
                {
                    $runResult = Invoke-ChattingScenario `
                        -RepositoryRoot $repositoryRoot `
                        -RunDirectory $runDirectory `
                        -RunDefinition $effectiveRun
                }
                default
                {
                    throw "Unsupported scenario: $scenarioName"
                }
            }
        }
        catch
        {
            $failureReason = $_.Exception.Message
            if ([string]::IsNullOrWhiteSpace($failureReason))
            {
                $failureReason = "Benchmark run failed before a scenario result was produced."
            }

            $runResult = New-BenchmarkFailureResult `
                -Scenario $scenarioName `
                -RunName $runInstanceName `
                -RunDirectory $runDirectory `
                -StartedAt $runStartedAt `
                -FailureReason $failureReason

            Write-BenchmarkUtf8File `
                -Path (Join-Path $runDirectory "run-summary.json") `
                -Content ($runResult | ConvertTo-Json -Depth 8)
        }

        $results.Add($runResult)
        $summaryRows.Add((New-BenchmarkSequenceRow -RunResult $runResult -Iteration $iteration))

        if (-not (ConvertTo-BenchmarkBoolean -Value $runResult["Succeeded"] -DefaultValue $false))
        {
            $failedRuns.Add($runInstanceName)
            if (-not $continueOnError)
            {
                $abortRequested = $true
                break
            }
        }
    }
}

$summaryCsvPath = Join-Path $sequenceDirectory "sequence-summary.csv"
$summaryJsonPath = Join-Path $sequenceDirectory "sequence-summary.json"
$failedRunsPath = Join-Path $sequenceDirectory "failed-runs.txt"

$summaryRows.ToArray() | Export-Csv -Path $summaryCsvPath -NoTypeInformation -Encoding utf8
Write-BenchmarkUtf8File -Path $summaryJsonPath -Content (($results.ToArray() | ConvertTo-Json -Depth 10))

if ($failedRuns.Count -gt 0)
{
    Write-BenchmarkUtf8File -Path $failedRunsPath -Content (($failedRuns | ForEach-Object { $_ }) -join [Environment]::NewLine)
    exit 1
}

exit 0

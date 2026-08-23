function Invoke-ChattingScenario
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepositoryRoot,
        [Parameter(Mandatory = $true)]
        [string]$RunDirectory,
        [Parameter(Mandatory = $true)]
        [System.Collections.IDictionary]$RunDefinition
    )

    $serverSpec = Get-BenchmarkMapValue -Map $RunDefinition -Key "Server" -DefaultValue ([ordered]@{})
    $clientSpec = Get-BenchmarkMapValue -Map $RunDefinition -Key "Client" -DefaultValue ([ordered]@{})
    $timingSpec = Get-BenchmarkMapValue -Map $RunDefinition -Key "Timing" -DefaultValue ([ordered]@{})

    $serverExecutable = Resolve-BenchmarkPath -RootPath $RepositoryRoot -PathText ([string](Get-BenchmarkMapValue -Map $serverSpec -Key "Executable"))
    $serverConfigTemplate = Resolve-BenchmarkPath -RootPath $RepositoryRoot -PathText ([string](Get-BenchmarkMapValue -Map $serverSpec -Key "ConfigTemplate"))
    $clientExecutable = Resolve-BenchmarkPath -RootPath $RepositoryRoot -PathText ([string](Get-BenchmarkMapValue -Map $clientSpec -Key "Executable"))
    $clientConfigTemplate = Resolve-BenchmarkPath -RootPath $RepositoryRoot -PathText ([string](Get-BenchmarkMapValue -Map $clientSpec -Key "ConfigTemplate"))

    $warmupSeconds = ConvertTo-BenchmarkInt -Value (Get-BenchmarkMapValue -Map $timingSpec -Key "WarmupSeconds" -DefaultValue 0) -DefaultValue 0
    $measureSeconds = ConvertTo-BenchmarkInt -Value (Get-BenchmarkMapValue -Map $timingSpec -Key "MeasureSeconds" -DefaultValue 30) -DefaultValue 30
    $cooldownSeconds = ConvertTo-BenchmarkInt -Value (Get-BenchmarkMapValue -Map $timingSpec -Key "CooldownSeconds" -DefaultValue 0) -DefaultValue 0
    $startupTimeoutSeconds = ConvertTo-BenchmarkInt -Value (Get-BenchmarkMapValue -Map $timingSpec -Key "StartupTimeoutSeconds" -DefaultValue 10) -DefaultValue 10
    $runTimeoutSeconds = ConvertTo-BenchmarkInt -Value (Get-BenchmarkMapValue -Map $timingSpec -Key "RunTimeoutSeconds" -DefaultValue ($warmupSeconds + $measureSeconds + $cooldownSeconds + 30)) -DefaultValue ($warmupSeconds + $measureSeconds + $cooldownSeconds + 30)
    $totalRunSeconds = [Math]::Max(1, $warmupSeconds + $measureSeconds + $cooldownSeconds)

    $serverStdOutPath = Join-Path $RunDirectory "server.stdout.log"
    $serverStdErrPath = Join-Path $RunDirectory "server.stderr.log"
    $clientStdOutPath = Join-Path $RunDirectory "client.stdout.log"
    $clientStdErrPath = Join-Path $RunDirectory "client.stderr.log"
    $serverLogDirectory = Join-Path $RunDirectory "server_logs"
    $rttCsvPath = Join-Path $RunDirectory "rtt.csv"
    $serverConfigPath = Join-Path $RunDirectory "effective.server.yaml"
    $clientConfigPath = Join-Path $RunDirectory "effective.client.yaml"

    $serverOverrides = Copy-BenchmarkObject -Value (Get-BenchmarkMapValue -Map $serverSpec -Key "Overrides" -DefaultValue ([ordered]@{}))
    $clientOverrides = Copy-BenchmarkObject -Value (Get-BenchmarkMapValue -Map $clientSpec -Key "Overrides" -DefaultValue ([ordered]@{}))

    if (-not $serverOverrides.Contains("ChattingServer.LogOutputDirectory"))
    {
        $serverOverrides["ChattingServer.LogOutputDirectory"] = $serverLogDirectory
    }

    if (-not $serverOverrides.Contains("Debug.Headless"))
    {
        $serverOverrides["Debug.Headless"] = $true
    }

    if (-not $clientOverrides.Contains("ChattingDummy.RttCsvPath"))
    {
        $clientOverrides["ChattingDummy.RttCsvPath"] = $rttCsvPath
    }

    if (-not $clientOverrides.Contains("ChattingDummy.RunSeconds"))
    {
        $clientOverrides["ChattingDummy.RunSeconds"] = $totalRunSeconds
    }

    $serverTemplateContent = Get-Content -Path $serverConfigTemplate -Raw -Encoding utf8
    $clientTemplateContent = Get-Content -Path $clientConfigTemplate -Raw -Encoding utf8
    $effectiveServerYaml = Set-BenchmarkYamlOverrides -TemplateContent $serverTemplateContent -Overrides $serverOverrides
    $effectiveClientYaml = Set-BenchmarkYamlOverrides -TemplateContent $clientTemplateContent -Overrides $clientOverrides

    Write-BenchmarkUtf8File -Path $serverConfigPath -Content $effectiveServerYaml
    Write-BenchmarkUtf8File -Path $clientConfigPath -Content $effectiveClientYaml

    $startedAt = Get-Date
    $serverProcess = $null
    $clientProcess = $null
    $serverReady = $false
    $serverExitedEarly = $false
    $clientTimedOut = $false
    $clientExitCode = -1
    $failureReason = ""

    try
    {
        $serverProcess = Start-BenchmarkProcess `
            -ExecutablePath $serverExecutable `
            -Arguments @("--config", $serverConfigPath, "--headless") `
            -WorkingDirectory (Split-Path -Parent $serverExecutable) `
            -StdOutPath $serverStdOutPath `
            -StdErrPath $serverStdErrPath

        $serverReady = Wait-BenchmarkServerReady -Process $serverProcess -TimeoutSeconds $startupTimeoutSeconds
        if (-not $serverReady)
        {
            $failureReason = "Server failed to become ready."
            throw $failureReason
        }

        $clientProcess = Start-BenchmarkProcess `
            -ExecutablePath $clientExecutable `
            -Arguments @("--config", $clientConfigPath) `
            -WorkingDirectory (Split-Path -Parent $clientExecutable) `
            -StdOutPath $clientStdOutPath `
            -StdErrPath $clientStdErrPath

        $deadline = (Get-Date).AddSeconds([Math]::Max(1, $runTimeoutSeconds))
        while ((Get-Date) -lt $deadline)
        {
            $clientProcess.Refresh()
            if ($clientProcess.HasExited)
            {
                break
            }

            $serverProcess.Refresh()
            if ($serverProcess.HasExited)
            {
                $serverExitedEarly = $true
                $failureReason = "Server exited before client completed."
                break
            }

            Start-Sleep -Milliseconds 250
        }

        $clientProcess.Refresh()
        if (-not $clientProcess.HasExited)
        {
            $clientTimedOut = $true
            $failureReason = "Client timed out."
            Stop-BenchmarkProcess -Process $clientProcess
        }

        $clientProcess.Refresh()
        if ($clientProcess.HasExited)
        {
            $clientProcess.WaitForExit()
            $clientExitCode = $clientProcess.ExitCode
        }
    }
    catch
    {
        if ([string]::IsNullOrWhiteSpace($failureReason))
        {
            $failureReason = $_.Exception.Message
        }
    }
    finally
    {
        Stop-BenchmarkProcess -Process $clientProcess
        Stop-BenchmarkProcess -Process $serverProcess
    }

    $clientSummaryLine = Get-BenchmarkLastLine -Path $clientStdOutPath -Pattern '^chatting dummy finished\.'
    $clientSummary = if ($clientSummaryLine) { ConvertFrom-BenchmarkMetricLine -Line $clientSummaryLine } else { $null }
    $serverStatSamples = @(Get-BenchmarkMetricMaps -Path $serverStdOutPath -Pattern '^\[ChattingStats\]')
    $serverStats = if ($serverStatSamples.Count -gt 0) { $serverStatSamples[-1] } else { $null }
    if ($null -ne $serverStats)
    {
        $serverStatAggregates = Get-BenchmarkMetricAggregateMap `
            -MetricMaps $serverStatSamples `
            -AverageKeys @("sendTPS", "recvTPS", "cpuPercent") `
            -MaxKeys @("sendTPS", "recvTPS") `
            -FilterKey "sessions" `
            -MinimumFilterValue 1
        foreach ($aggregateKey in $serverStatAggregates.Keys)
        {
            $serverStats[$aggregateKey] = $serverStatAggregates[$aggregateKey]
        }
    }

    $contentStats = Get-BenchmarkLastMetricMap -Path $serverStdOutPath -Pattern '^\[ContentStats\]'

    $hasClientExitCode = $null -ne $clientExitCode -and $clientExitCode -ne -1
    $succeeded =
        $serverReady -and
        (-not $serverExitedEarly) -and
        (-not $clientTimedOut) -and
        $hasClientExitCode -and
        $clientExitCode -eq 0

    if ($null -eq $clientSummary)
    {
        $succeeded = $false
        if ([string]::IsNullOrWhiteSpace($failureReason))
        {
            $failureReason = "Client summary was not found."
        }
    }

    $clientFailureMetricKeys = @(
        "permanentFailure",
        "timeout",
        "unexpectedDisconnect",
        "sessionError",
        "selfBroadcast",
        "invalidRoomBroadcast",
        "payloadValidationFailure"
    )
    if ($null -ne $clientSummary)
    {
        foreach ($metricKey in $clientFailureMetricKeys)
        {
            if (-not $clientSummary.Contains($metricKey))
            {
                $succeeded = $false
                if ([string]::IsNullOrWhiteSpace($failureReason))
                {
                    $failureReason = "Required client summary metric was not found: $metricKey"
                }
                break
            }

            $metricValue = ConvertTo-BenchmarkInt -Value $clientSummary[$metricKey] -DefaultValue 1
            if ($metricValue -ne 0)
            {
                $succeeded = $false
                if ([string]::IsNullOrWhiteSpace($failureReason))
                {
                    $failureReason = "Client summary reported $metricKey=$metricValue."
                }
                break
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($failureReason) -and -not $hasClientExitCode)
    {
        $failureReason = "Client exit code was unavailable."
    }
    elseif ([string]::IsNullOrWhiteSpace($failureReason) -and $clientExitCode -ne 0)
    {
        $failureReason = "Client exited with code $clientExitCode."
    }

    if ([string]::IsNullOrWhiteSpace($failureReason) -and -not $succeeded)
    {
        $failureReason = "Chatting scenario failed."
    }

    if ($succeeded)
    {
        Remove-BenchmarkFileIfExists -Path $clientStdOutPath
        Remove-BenchmarkFileIfExists -Path $clientStdErrPath
    }

    $result = [ordered]@{
        Scenario = "Chatting"
        RunName = [string](Get-BenchmarkMapValue -Map $RunDefinition -Key "Name" -DefaultValue "UnnamedRun")
        StartedAt = $startedAt.ToString("o")
        FinishedAt = (Get-Date).ToString("o")
        Succeeded = $succeeded
        FailureReason = $failureReason
        ServerReady = $serverReady
        ServerExitedEarly = $serverExitedEarly
        ClientTimedOut = $clientTimedOut
        ClientExitCode = $clientExitCode
        OutputDirectory = $RunDirectory
        EffectiveServerConfigPath = $serverConfigPath
        EffectiveClientConfigPath = $clientConfigPath
        ClientSummary = $clientSummary
        ServerStats = $serverStats
        ContentStats = $contentStats
    }

    Write-BenchmarkUtf8File -Path (Join-Path $RunDirectory "run-summary.json") -Content ($result | ConvertTo-Json -Depth 8)
    return $result
}

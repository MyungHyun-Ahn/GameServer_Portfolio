param(
    [int]$VirtualUserCount = 100,
    [int]$ConnectsPerSecond = 20,
    [int]$RunSeconds = 60,
    [int]$SearchWeight = 50,
    [int]$RegisterWeight = 20,
    [int]$BidWeight = 30,
    [uint64]$InitialGoldAmount = 1000000,
    [int]$OutbidRefundPercent = 50,
    [int]$ExpireListingsAfterSeconds = 0,
    [int]$ExpireListingCount = 20,
    [uint32]$ListingBuyoutMarkupMinimum = 100,
    [uint32]$ListingBuyoutMarkupMaximum = 1000,
    [int]$ResponseTimeoutMs = 5000,
    [int]$Port = 19106,
    [int]$CachePort = 19103,
    [uint64]$UserIdBase = 800000,
    [switch]$KeepTestData,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

if ($VirtualUserCount -le 0 -or $ConnectsPerSecond -le 0 -or $RunSeconds -le 0 -or
    $SearchWeight -le 0 -or $RegisterWeight -lt 0 -or $BidWeight -lt 0 -or $InitialGoldAmount -eq 0 -or
    $OutbidRefundPercent -lt 0 -or $OutbidRefundPercent -gt 100 -or $ExpireListingsAfterSeconds -lt 0 -or
    $ExpireListingCount -le 0 -or $ListingBuyoutMarkupMinimum -eq 0 -or
    $ListingBuyoutMarkupMaximum -lt $ListingBuyoutMarkupMinimum -or $ResponseTimeoutMs -le 0 -or
    $Port -le 0 -or $Port -gt 65535 -or $CachePort -le 0 -or $CachePort -gt 65535 -or $Port -eq $CachePort)
{
    throw "Load-test counts, duration and behavior weights are invalid."
}
$lastUserId = $UserIdBase + [uint64]$VirtualUserCount - 1
if ($lastUserId -gt [uint32]::MaxValue)
{
    throw "Load-test user IDs must fit in uint32."
}

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $scriptDirectory)
$envFile = Join-Path $repositoryRoot ".env"
$serverProject = Join-Path $repositoryRoot "Auction\AuctionHouseServer\AuctionHouseServer.vcxproj"
$clientProject = Join-Path $repositoryRoot "Auction\AuctionDummyClient\AuctionDummyClient.vcxproj"
$cacheProject = Join-Path $repositoryRoot "Cache\CacheServer\CacheServer.vcxproj"
$serverExecutable = Join-Path $repositoryRoot "Out\AuctionHouseServer\Release\AuctionHouseServer.exe"
$clientExecutable = Join-Path $repositoryRoot "Out\AuctionDummyClient\Release\AuctionDummyClient.exe"
$cacheExecutable = Join-Path $repositoryRoot "Out\CacheServer\Release\CacheServer.exe"
$cacheConfigTemplate = Join-Path $repositoryRoot "Config\Server\CacheServer.yaml"
$serverConfigTemplate = Join-Path $repositoryRoot "Config\Server\AuctionHouseServer.yaml"
$redisContainer = "gameserverportfolio-chat-redis"
. (Join-Path $repositoryRoot "scripts\common\ServerConfig.ps1")

function Get-DotEnvValue([string]$Name)
{
    $line = Get-Content -LiteralPath $envFile |
        Where-Object { $_ -match "^$([regex]::Escape($Name))=" } |
        Select-Object -Last 1
    if ([string]::IsNullOrWhiteSpace($line))
    {
        throw "Missing $Name in $envFile"
    }
    $value = $line.Substring($line.IndexOf('=') + 1).Trim()
    if ($value.Length -ge 2 -and
        (($value.StartsWith('"') -and $value.EndsWith('"')) -or
         ($value.StartsWith("'") -and $value.EndsWith("'"))))
    {
        $value = $value.Substring(1, $value.Length - 2)
    }
    return $value
}

function Invoke-RedisPipe([string]$Container, [string]$Commands)
{
    $commandFile = Join-Path ([System.IO.Path]::GetTempPath()) ("auction-redis-" + [guid]::NewGuid().ToString("N") + ".txt")
    try
    {
        [System.IO.File]::WriteAllText($commandFile, $Commands, [System.Text.UTF8Encoding]::new($false))
        $nativeCommand = "docker exec -i $Container redis-cli --pipe < `"$commandFile`""
        $output = & $env:ComSpec /d /s /c $nativeCommand 2>&1
        if ($LASTEXITCODE -ne 0 -or ($output | Out-String) -match "errors: [1-9]")
        {
            throw "Redis pipe failed. output=$output"
        }
    }
    finally
    {
        if (Test-Path -LiteralPath $commandFile)
        {
            Remove-Item -LiteralPath $commandFile -Force
        }
    }
}

function Invoke-RootSql([string]$Container, [string]$Sql, [string]$RootPassword)
{
    docker exec -e "MYSQL_PWD=$RootPassword" $Container mysql -uroot -e $Sql
    if ($LASTEXITCODE -ne 0)
    {
        throw "SQL cleanup failed in $Container"
    }
}

function Wait-ListeningPort(
    [int]$ListenPort,
    [System.Diagnostics.Process]$Process,
    [string]$ProcessName,
    [string]$StandardErrorPath)
{
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline)
    {
        if ($Process.HasExited)
        {
            $processError = Get-Content -LiteralPath $StandardErrorPath -Raw -ErrorAction SilentlyContinue
            throw "$ProcessName exited before listening on port $ListenPort. error=$processError"
        }

        $listener = Get-NetTCPConnection -LocalPort $ListenPort -State Listen -ErrorAction SilentlyContinue |
            Where-Object { $_.OwningProcess -eq $Process.Id } |
            Select-Object -First 1
        if ($null -ne $listener)
        {
            return
        }
        Start-Sleep -Milliseconds 100
    }

    throw "$ProcessName did not listen on port $ListenPort within 15 seconds."
}

function Wait-EstablishedConnection(
    [int]$RemotePort,
    [System.Diagnostics.Process]$Process,
    [string]$ProcessName)
{
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline)
    {
        if ($Process.HasExited)
        {
            throw "$ProcessName exited before connecting to port $RemotePort."
        }
        $connection = Get-NetTCPConnection -OwningProcess $Process.Id -RemotePort $RemotePort -State Established -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $connection)
        {
            Start-Sleep -Milliseconds 250
            return
        }
        Start-Sleep -Milliseconds 100
    }

	throw "$ProcessName did not connect to port $RemotePort within 15 seconds."
}

function Stop-LoadTestProcess(
    [AllowNull()]
    [System.Diagnostics.Process]$Process,
    [string]$ProcessName,
    [int]$NaturalExitWaitMilliseconds)
{
    if ($null -eq $Process)
    {
        return
    }

    $Process.Refresh()
    if ($Process.HasExited)
    {
        $Process.WaitForExit()
        $Process.Refresh()
        return
    }

    if ($NaturalExitWaitMilliseconds -gt 0 -and $Process.WaitForExit($NaturalExitWaitMilliseconds))
    {
        $Process.WaitForExit()
        $Process.Refresh()
        return
    }

    Write-Warning "$ProcessName did not exit within the graceful shutdown window. Forcing termination."
    try
    {
        Stop-Process -Id $Process.Id -Force -ErrorAction Stop
    }
    catch
    {
        $Process.Refresh()
        if (-not $Process.HasExited)
        {
            throw
        }
    }

    if (-not $Process.HasExited)
    {
        if (-not $Process.WaitForExit(5000))
        {
            throw "$ProcessName did not exit after forced termination. pid=$($Process.Id)"
        }
    }
    $Process.WaitForExit()
    $Process.Refresh()
}

function Invoke-LoadTestLogicValidation(
    [uint64]$FirstUserId,
    [uint64]$LastUserId,
    [string]$RootPassword,
    [bool]$ExpectExpiration,
    [bool]$ExpectRefund)
{
    $auctionSql = "USE auctiondb; SELECT COALESCE(SUM(l.state IN (1,3,4,5,6)),0), COALESCE(SUM(l.state=7),0), (SELECT COUNT(*) FROM auction_bids b JOIN auction_listings bl ON bl.listing_id=b.listing_id WHERE bl.seller_user_id BETWEEN $FirstUserId AND $LastUserId AND b.state IN (1,4)), (SELECT COUNT(*) FROM auction_bids b JOIN auction_listings bl ON bl.listing_id=b.listing_id WHERE bl.seller_user_id BETWEEN $FirstUserId AND $LastUserId AND b.state=5), (SELECT COUNT(*) FROM auction_bids b JOIN auction_listings bl ON bl.listing_id=b.listing_id WHERE bl.seller_user_id BETWEEN $FirstUserId AND $LastUserId AND b.state=6), (SELECT COUNT(*) FROM auction_listings ml JOIN auction_bids mb ON mb.bid_id=ml.highest_bid_id WHERE ml.seller_user_id BETWEEN $FirstUserId AND $LastUserId AND ((ml.state=2 AND mb.state<>2) OR (ml.state=7 AND mb.state<>6))) FROM auction_listings l WHERE l.seller_user_id BETWEEN $FirstUserId AND $LastUserId;"
    $auctionResult = docker exec -e "MYSQL_PWD=$RootPassword" gameserverportfolio-auction-db-primary mysql -uroot -N -B -e $auctionSql
    if ($LASTEXITCODE -ne 0)
    {
        throw "AuctionDB logic validation query failed."
    }
    $auctionValues = ($auctionResult -split "`t")
    if ($auctionValues.Count -ne 6)
    {
        throw "AuctionDB logic validation returned an unexpected result: $auctionResult"
    }
    $pendingListings = [uint64]$auctionValues[0]
    $soldListings = [uint64]$auctionValues[1]
    $pendingBids = [uint64]$auctionValues[2]
    $refundedBids = [uint64]$auctionValues[3]
    $wonBids = [uint64]$auctionValues[4]
    $highestBidMismatches = [uint64]$auctionValues[5]

    $gameSql = "USE gamedb; SELECT COUNT(*) FROM mail_attachments a JOIN mails m ON m.mail_id=a.mail_id WHERE m.receiver_user_id BETWEEN $FirstUserId AND $LastUserId AND a.attachment_type=1 AND a.state=2;"
    $claimedItems = [uint64](docker exec -e "MYSQL_PWD=$RootPassword" gameserverportfolio-game-db-primary mysql -uroot -N -B -e $gameSql)
    if ($LASTEXITCODE -ne 0)
    {
        throw "GameDB logic validation query failed."
    }

    Write-Host "AUCTION_LOAD_TEST_LOGIC_VALIDATION pendingListings=$pendingListings pendingBids=$pendingBids highestBidMismatches=$highestBidMismatches refundedBids=$refundedBids soldListings=$soldListings wonBids=$wonBids claimedItems=$claimedItems"
    if ($pendingListings -ne 0 -or $pendingBids -ne 0 -or $highestBidMismatches -ne 0)
    {
        throw "Auction load-test logic validation found an invalid intermediate state."
    }
    if ($ExpectRefund -and $refundedBids -eq 0)
    {
        throw "Auction load-test logic validation did not observe a completed refund."
    }
    if ($ExpectExpiration -and ($soldListings -eq 0 -or $wonBids -eq 0 -or $claimedItems -lt $wonBids))
    {
        throw "Auction load-test logic validation did not complete the expected settlement and mail claim flow."
    }
}

function Reset-LoadTestData([uint64]$FirstUserId, [uint64]$LastUserId, [string]$RootPassword)
{
	$auctionSql = "USE auctiondb; DELETE b FROM auction_bids b JOIN auction_listings l ON l.listing_id=b.listing_id WHERE l.seller_user_id BETWEEN $FirstUserId AND $LastUserId; DELETE FROM auction_listings WHERE seller_user_id BETWEEN $FirstUserId AND $LastUserId;"
	$gameSql = "USE gamedb; DELETE a FROM mail_attachments a JOIN mails m ON m.mail_id=a.mail_id WHERE m.receiver_user_id BETWEEN $FirstUserId AND $LastUserId; DELETE FROM mails WHERE receiver_user_id BETWEEN $FirstUserId AND $LastUserId; DELETE FROM inventory_items WHERE owner_user_id BETWEEN $FirstUserId AND $LastUserId; DELETE FROM player_currencies WHERE user_id BETWEEN $FirstUserId AND $LastUserId;"
    Invoke-RootSql "gameserverportfolio-auction-db-primary" $auctionSql $RootPassword
    Invoke-RootSql "gameserverportfolio-game-db-primary" $gameSql $RootPassword
}

if (-not (Test-Path -LiteralPath $envFile))
{
    throw "Create $envFile from .env.example first."
}

& (Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1")
$loginCompose = Join-Path $repositoryRoot "Infra\docker-compose.login-platform.yaml"
docker compose --env-file $envFile -f $loginCompose up -d chat-redis
if ($LASTEXITCODE -ne 0)
{
    throw "Failed to start Redis for AuctionAuth."
}

if (-not $SkipBuild)
{
    $buildTemp = Join-Path $repositoryRoot "Out\Temp"
    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    New-Item -ItemType Directory -Path $buildTemp -Force | Out-Null
    try
    {
        $env:TEMP = $buildTemp
        $env:TMP = $buildTemp
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        $visualStudioPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        $msbuild = Join-Path $visualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
        foreach ($project in @($cacheProject, $serverProject, $clientProject))
        {
            & $msbuild $project /t:Build /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
            if ($LASTEXITCODE -ne 0)
            {
                throw "Build failed: $project"
            }
        }
    }
    finally
    {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
    }
}

$testDirectory = Join-Path $repositoryRoot ("Out\AuctionSearchLoadTest\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$ticketPath = Join-Path $testDirectory "Tickets.txt"
$configPath = Join-Path $testDirectory "AuctionDummyClient.yaml"
$serverStdout = Join-Path $testDirectory "server.stdout.log"
$serverStderr = Join-Path $testDirectory "server.stderr.log"
$cacheStdout = Join-Path $testDirectory "cache.stdout.log"
$cacheStderr = Join-Path $testDirectory "cache.stderr.log"
$clientStdout = Join-Path $testDirectory "client.stdout.log"
$clientStderr = Join-Path $testDirectory "client.stderr.log"
$tickets = [System.Collections.Generic.List[string]]::new()
$redisSeed = [System.Text.StringBuilder]::new()
$redisCleanup = [System.Text.StringBuilder]::new()
$ticketTtlSeconds = $RunSeconds + [math]::Ceiling($VirtualUserCount / $ConnectsPerSecond) + 120
$runId = [guid]::NewGuid().ToString("N")

for ($index = 0; $index -lt $VirtualUserCount; ++$index)
{
    $userId = $UserIdBase + [uint64]$index
    $ticket = "auction-load-$runId-$userId"
    $loginId = "auction_load_$userId"
    $loginIdHex = [System.BitConverter]::ToString([System.Text.Encoding]::UTF8.GetBytes($loginId)).Replace("-", "")
    $tickets.Add($ticket)
    [void]$redisSeed.AppendLine("SET chat:active-login:$userId 1")
    [void]$redisSeed.AppendLine("SETEX auction:ticket:$ticket $ticketTtlSeconds $userId`:1`:$loginIdHex")
    [void]$redisCleanup.AppendLine("DEL chat:active-login:$userId")
    [void]$redisCleanup.AppendLine("DEL auction:ticket:$ticket")
}

$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllLines($ticketPath, $tickets, $utf8WithoutBom)
$configText = @"
AuctionDummy:
  ServerIp: 127.0.0.1
  Port: $Port
  PacketKey: 55
  WorkerThreadCount: 4
  VirtualUserCount: $VirtualUserCount
  ConnectsPerSecond: $ConnectsPerSecond
  RunSeconds: $RunSeconds
  SearchIntervalMinMs: 500
  SearchIntervalMaxMs: 1500
  ResponseTimeoutMs: $ResponseTimeoutMs
  ConsoleSummaryIntervalSeconds: 5
  EventPollMaxCount: 512
  RandomStatMaximum: 20
  SearchWeight: $SearchWeight
  RegisterWeight: $RegisterWeight
  BidWeight: $BidWeight
  InitialGoldAmount: $InitialGoldAmount
  BidIncrementMinimum: 10
  BidIncrementMaximum: 100
  BidHotspotPercent: 80
  OutbidRefundPercent: $OutbidRefundPercent
  InventoryListLimit: 20
  CheatItemDataIds: 1001,1002,1003,1004,2001,2002,3001,3002
  ListingStartPriceMinimum: 100
  ListingStartPriceMaximum: 1000
  ListingBuyoutMarkupMinimum: $ListingBuyoutMarkupMinimum
  ListingBuyoutMarkupMaximum: $ListingBuyoutMarkupMaximum
  RandomSeed: 20260817
  TicketFilePath: Tickets.txt
"@
[System.IO.File]::WriteAllText($configPath, $configText, $utf8WithoutBom)

$serverProcess = $null
$cacheProcess = $null
$expirationJob = $null
$loadTestCompleted = $false
$rootPassword = Get-DotEnvValue "MYSQL_ROOT_PASSWORD"
try
{
    Reset-LoadTestData $UserIdBase $lastUserId $rootPassword
    Invoke-RedisPipe $redisContainer ($redisSeed.ToString())

    $appPassword = Get-DotEnvValue "MYSQL_PASSWORD"
    $env:MYSQL_PASSWORD = $appPassword
    $serverRunSeconds = $RunSeconds + [math]::Ceiling($VirtualUserCount / $ConnectsPerSecond) + 30
    $cacheServerConfigPath = Join-Path $testDirectory "CacheServer.yaml"
    New-ServerConfigFile -TemplatePath $cacheConfigTemplate -DestinationPath $cacheServerConfigPath -Overrides @{
        "CacheServer.Backend" = "Iocp"
        "CacheServer.Port" = $CachePort
        "CacheServer.PlayerCacheShardCount" = 4
        "CacheServer.DatabaseEnabled" = $true
        "GameDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
        "CacheServer.LogOutputDirectory" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "cache-logs"))
        "Debug.RunSeconds" = ($serverRunSeconds + 15)
    } | Out-Null
    $auctionServerConfigPath = Join-Path $testDirectory "AuctionHouseServer.yaml"
    New-ServerConfigFile -TemplatePath $serverConfigTemplate -DestinationPath $auctionServerConfigPath -Overrides @{
        "AuctionHouseServer.Port" = $Port
        "AuctionHouseServer.RunSeconds" = $serverRunSeconds
        "Logging.OutputDirectory" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "auction-logs"))
        "Diagnostics.TimingCsvPath" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "auction_timing.csv"))
        "Authentication.Enabled" = $true
        "CacheRpc.Port" = $CachePort
        "CacheRpc.ReconnectMilliseconds" = 100
        "AuctionDatabase.Enabled" = $true
        "AuctionDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
    } | Out-Null
    $cacheProcess = Start-Process -FilePath $cacheExecutable `
        -ArgumentList @("--config", $cacheServerConfigPath) `
        -WorkingDirectory $testDirectory `
        -RedirectStandardOutput $cacheStdout `
        -RedirectStandardError $cacheStderr `
        -WindowStyle Hidden -PassThru
    Wait-ListeningPort -ListenPort $CachePort -Process $cacheProcess -ProcessName "CacheServer" -StandardErrorPath $cacheStderr

    $serverProcess = Start-Process -FilePath $serverExecutable `
        -ArgumentList @("--config", $auctionServerConfigPath) `
        -WorkingDirectory (Split-Path -Parent $serverExecutable) `
        -RedirectStandardOutput $serverStdout `
        -RedirectStandardError $serverStderr `
        -WindowStyle Hidden -PassThru
    Wait-ListeningPort -ListenPort $Port -Process $serverProcess -ProcessName "AuctionHouseServer" -StandardErrorPath $serverStderr
    Wait-EstablishedConnection -RemotePort $CachePort -Process $serverProcess -ProcessName "AuctionHouseServer"

    if ($ExpireListingsAfterSeconds -gt 0)
    {
        $expirationJob = Start-Job -ArgumentList @($ExpireListingsAfterSeconds, $ExpireListingCount, $UserIdBase, $lastUserId, $rootPassword) -ScriptBlock {
            param($DelaySeconds, $ListingCount, $FirstUserId, $LastUserId, $Password)
            Start-Sleep -Seconds $DelaySeconds
            docker exec -e "MYSQL_PWD=$Password" gameserverportfolio-auction-db-primary mysql -uroot -e `
                "USE auctiondb; UPDATE auction_listings SET expires_at=DATE_ADD(UTC_TIMESTAMP(6), INTERVAL 1 SECOND) WHERE seller_user_id BETWEEN $FirstUserId AND $LastUserId AND state=2 AND highest_bid_id IS NOT NULL ORDER BY listing_id LIMIT $ListingCount;"
            if ($LASTEXITCODE -ne 0)
            {
                throw "Failed to accelerate load-test listing expiration."
            }
        }
    }

    $clientProcess = Start-Process -FilePath $clientExecutable `
        -ArgumentList @("--load-test", "--config", $configPath) `
        -WorkingDirectory (Split-Path -Parent $clientExecutable) `
        -RedirectStandardOutput $clientStdout `
        -RedirectStandardError $clientStderr `
        -WindowStyle Hidden -Wait -PassThru
    $clientExitCode = $clientProcess.ExitCode
    $clientOutput = Get-Content -LiteralPath $clientStdout -Raw
    if ($clientExitCode -ne 0 -or $clientOutput -notmatch "AUCTION_LOAD_TEST_SUCCESS")
    {
        $clientError = Get-Content -LiteralPath $clientStderr -Raw -ErrorAction SilentlyContinue
        $serverError = Get-Content -LiteralPath $serverStderr -Raw -ErrorAction SilentlyContinue
        $cacheError = Get-Content -LiteralPath $cacheStderr -Raw -ErrorAction SilentlyContinue
        throw "Search load test failed. client=$clientError server=$serverError cache=$cacheError"
    }

    Write-Host $clientOutput.Trim()
    Invoke-LoadTestLogicValidation $UserIdBase $lastUserId $rootPassword ($ExpireListingsAfterSeconds -gt 0) ($OutbidRefundPercent -gt 0)
    $integrityReportPath = Join-Path $testDirectory "integrity-report.json"
    & (Join-Path $scriptDirectory "Test-AuctionDataIntegrity.ps1") `
        -FirstUserId $UserIdBase `
        -LastUserId $lastUserId `
        -OutputPath $integrityReportPath
    Write-Host "Logs: $testDirectory"
    $loadTestCompleted = $true
}
finally
{
    if ($expirationJob -ne $null)
    {
        if ($expirationJob.State -eq "Running")
        {
            Stop-Job -Job $expirationJob
        }
        Receive-Job -Job $expirationJob -ErrorAction SilentlyContinue | Write-Host
        Remove-Job -Job $expirationJob -Force
    }
    $serverNaturalExitWaitMilliseconds = 3000
    $cacheNaturalExitWaitMilliseconds = 3000
    if ($loadTestCompleted)
    {
        $serverNaturalExitWaitMilliseconds = 45000
        $cacheNaturalExitWaitMilliseconds = 30000
    }
    Stop-LoadTestProcess $serverProcess "AuctionHouseServer" $serverNaturalExitWaitMilliseconds
    Stop-LoadTestProcess $cacheProcess "CacheServer" $cacheNaturalExitWaitMilliseconds
    try
    {
        Invoke-RedisPipe $redisContainer ($redisCleanup.ToString())
    }
    catch
    {
        Write-Warning "Redis cleanup failed: $_"
    }
    if (-not $KeepTestData)
    {
        try
        {
            Reset-LoadTestData $UserIdBase $lastUserId $rootPassword
        }
        catch
        {
            Write-Warning "DB cleanup failed: $_"
        }
    }
}

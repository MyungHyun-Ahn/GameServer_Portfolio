[CmdletBinding()]
param(
    [int]$AuctionPort = 19112,
    [int]$CachePort = 19113,
    [switch]$SkipBuild,
    [switch]$SkipDatabaseStart
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $scriptDirectory)
$envFile = Join-Path $repositoryRoot ".env"
$databaseStartScript = Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1"
$loginCompose = Join-Path $repositoryRoot "Infra\docker-compose.login-platform.yaml"
$cacheProject = Join-Path $repositoryRoot "Cache\CacheServer\CacheServer.vcxproj"
$auctionProject = Join-Path $repositoryRoot "Auction\AuctionHouseServer\AuctionHouseServer.vcxproj"
$clientProject = Join-Path $repositoryRoot "Auction\AuctionDummyClient\AuctionDummyClient.vcxproj"
$cacheExecutable = Join-Path $repositoryRoot "Out\CacheServer\Debug\CacheServer.exe"
$auctionExecutable = Join-Path $repositoryRoot "Out\AuctionHouseServer\Debug\AuctionHouseServer.exe"
$clientExecutable = Join-Path $repositoryRoot "Out\AuctionDummyClient\Debug\AuctionDummyClient.exe"
$cacheConfigTemplate = Join-Path $repositoryRoot "Config\Server\CacheServer.yaml"
$auctionConfigTemplate = Join-Path $repositoryRoot "Config\Server\AuctionHouseServer.yaml"
. (Join-Path $repositoryRoot "scripts\common\ServerConfig.ps1")

$testUserId = [uint64]940030001
$sellerUserId = [uint64]940030002
$highestBidderUserId = [uint64]940030003
$listingId = [uint64]99300001
$bidId = [uint64]77300001
$highestBidId = [uint64]77300002
$refundAmount = [uint64]1500

function Get-DotEnvValue
{
    param([Parameter(Mandatory = $true)][string]$Name)

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

function Invoke-RootSql
{
    param(
        [Parameter(Mandatory = $true)][string]$Container,
        [Parameter(Mandatory = $true)][string]$Sql,
        [Parameter(Mandatory = $true)][string]$RootPassword
    )

    $Sql | docker exec -i -e "MYSQL_PWD=$RootPassword" $Container mysql -uroot
    if ($LASTEXITCODE -ne 0)
    {
        throw "SQL execution failed in $Container"
    }
}

function Get-RootSqlScalar
{
    param(
        [Parameter(Mandatory = $true)][string]$Container,
        [Parameter(Mandatory = $true)][string]$Sql,
        [Parameter(Mandatory = $true)][string]$RootPassword
    )

    $result = docker exec -e "MYSQL_PWD=$RootPassword" $Container mysql -uroot -Nse $Sql 2>$null
    if ($LASTEXITCODE -ne 0)
    {
        throw "SQL scalar query failed in $Container"
    }
    return ($result | Out-String).Trim()
}

function Wait-RootSqlScalar
{
    param(
        [Parameter(Mandatory = $true)][string]$Container,
        [Parameter(Mandatory = $true)][string]$Sql,
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string]$RootPassword,
        [int]$TimeoutMilliseconds = 8000
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do
    {
        $actual = docker exec -e "MYSQL_PWD=$RootPassword" $Container mysql -uroot -Nse $Sql 2>$null
        if ($LASTEXITCODE -eq 0 -and ($actual | Out-String).Trim() -eq $Expected)
        {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    while ([DateTime]::UtcNow -lt $deadline)

    throw "Expected SQL value '$Expected' was not observed in $Container. actual=$(($actual | Out-String).Trim())"
}

function Wait-ListeningPort
{
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$StandardErrorPath
    )

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline -and -not $Process.HasExited)
    {
        $listener = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue |
            Where-Object { $_.OwningProcess -eq $Process.Id } |
            Select-Object -First 1
        if ($null -ne $listener)
        {
            return
        }
        Start-Sleep -Milliseconds 100
    }

    $errorOutput = Get-Content -LiteralPath $StandardErrorPath -Raw -ErrorAction SilentlyContinue
    throw "$Name did not enter Listen state on port $Port. error=$errorOutput"
}

function Wait-PortClosed
{
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [int]$TimeoutMilliseconds = 5000
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do
    {
        if ($null -eq (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue))
        {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    while ([DateTime]::UtcNow -lt $deadline)

    throw "Port $Port remained in Listen state."
}

function Wait-EstablishedConnection
{
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][int]$RemotePort,
        [int]$TimeoutMilliseconds = 10000
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline -and -not $Process.HasExited)
    {
        $connection = Get-NetTCPConnection -State Established -OwningProcess $Process.Id -ErrorAction SilentlyContinue |
            Where-Object { $_.RemotePort -eq $RemotePort } |
            Select-Object -First 1
        if ($null -ne $connection)
        {
            Start-Sleep -Milliseconds 250
            return
        }
        Start-Sleep -Milliseconds 100
    }

    throw "Process $($Process.Id) did not establish a connection to port $RemotePort."
}

function Wait-FilePattern
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [int]$TimeoutMilliseconds = 12000
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do
    {
        $text = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
        if ($text -match $Pattern)
        {
            return
        }
        if ($Process.HasExited)
        {
            throw "Process $($Process.Id) exited before marker '$Pattern'. log=$Path"
        }
        Start-Sleep -Milliseconds 50
    }
    while ([DateTime]::UtcNow -lt $deadline)

    throw "Timed out waiting for marker '$Pattern'. log=$Path"
}

function Stop-CapturedProcess
{
    param([AllowNull()][System.Diagnostics.Process]$Process)

    if ($null -ne $Process -and -not $Process.HasExited)
    {
        Stop-Process -Id $Process.Id -Force
        if (-not $Process.WaitForExit(5000))
        {
            throw "Process $($Process.Id) did not exit after Stop-Process."
        }
    }
}

function Wait-NaturalExit
{
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$Name,
        [int]$TimeoutMilliseconds = 20000
    )

    if (-not $Process.WaitForExit($TimeoutMilliseconds))
    {
        throw "$Name did not exit within the timeout. pid=$($Process.Id)"
    }
    # Complete redirected stream draining. Some Windows PowerShell hosts do not
    # retain ExitCode for an asynchronously started process, so downstream
    # protocol/log assertions are the authoritative success criteria.
    $Process.WaitForExit()
    $Process.Refresh()
}

function Read-LogText
{
    param([Parameter(Mandatory = $true)][string]$Path)

    $text = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
    if ($null -eq $text)
    {
        return ""
    }
    return $text
}

function Start-CacheServer
{
    param(
        [Parameter(Mandatory = $true)][string]$CaseDirectory,
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath,
        [hashtable]$AdditionalOverrides = @{},
        [int]$RunSeconds = 15
    )

    $configPath = Join-Path $CaseDirectory (([System.IO.Path]::GetFileNameWithoutExtension($StdoutPath)) + ".yaml")
    $overrides = @{
        "CacheServer.Port" = $CachePort
        "CacheServer.PlayerCacheShardCount" = 4
        "CacheServer.DatabaseEnabled" = $true
        "GameDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
        "CacheServer.LogOutputDirectory" = (ConvertTo-ServerConfigYamlString (Join-Path $CaseDirectory "cache-logs"))
        "Debug.RunSeconds" = $RunSeconds
    }
    foreach ($entry in $AdditionalOverrides.GetEnumerator())
    {
        $overrides[$entry.Key] = $entry.Value
    }
    New-ServerConfigFile -TemplatePath $cacheConfigTemplate -DestinationPath $configPath -Overrides $overrides | Out-Null
    return Start-Process -FilePath $cacheExecutable `
        -ArgumentList @("--config", $configPath) `
        -WorkingDirectory $CaseDirectory `
        -RedirectStandardOutput $StdoutPath `
        -RedirectStandardError $StderrPath `
        -WindowStyle Hidden `
        -PassThru
}

function Start-AuctionServer
{
    param(
        [Parameter(Mandatory = $true)][string]$CaseDirectory,
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath,
        [Parameter(Mandatory = $true)][int]$RpcTimeoutMilliseconds,
        [hashtable]$AdditionalOverrides = @{},
        [int]$RunSeconds = 12
    )

    $configPath = Join-Path $CaseDirectory (([System.IO.Path]::GetFileNameWithoutExtension($StdoutPath)) + ".yaml")
    $overrides = @{
        "AuctionHouseServer.Port" = $AuctionPort
        "AuctionHouseServer.RunSeconds" = $RunSeconds
        "Logging.OutputDirectory" = (ConvertTo-ServerConfigYamlString (Join-Path $CaseDirectory "auction-logs"))
        "Diagnostics.TimingCsvPath" = (ConvertTo-ServerConfigYamlString (Join-Path $CaseDirectory "auction_timing.csv"))
        "Authentication.Enabled" = $true
        "CacheRpc.Port" = $CachePort
        "CacheRpc.HandshakeTimeoutMilliseconds" = $RpcTimeoutMilliseconds
        "CacheRpc.ReconnectMilliseconds" = 100
        "AuctionDatabase.Enabled" = $true
        "AuctionDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
    }
    foreach ($entry in $AdditionalOverrides.GetEnumerator())
    {
        $overrides[$entry.Key] = $entry.Value
    }
    New-ServerConfigFile -TemplatePath $auctionConfigTemplate -DestinationPath $configPath -Overrides $overrides | Out-Null
    return Start-Process -FilePath $auctionExecutable `
        -ArgumentList @("--config", $configPath) `
        -WorkingDirectory $CaseDirectory `
        -RedirectStandardOutput $StdoutPath `
        -RedirectStandardError $StderrPath `
        -WindowStyle Hidden `
        -PassThru
}

function Remove-FaultTestData
{
    param([Parameter(Mandatory = $true)][string]$RootPassword)

    Invoke-RootSql -Container "gameserverportfolio-auction-db-primary" -RootPassword $RootPassword -Sql @"
USE auctiondb;
DELETE FROM auction_bids WHERE listing_id=$listingId OR bid_id IN ($bidId,$highestBidId);
DELETE FROM auction_listings WHERE listing_id=$listingId;
"@
    Invoke-RootSql -Container "gameserverportfolio-game-db-primary" -RootPassword $RootPassword -Sql @"
USE gamedb;
DELETE FROM player_currencies WHERE user_id=$testUserId AND currency_id=1;
"@
}

function Initialize-FaultTestData
{
    param([Parameter(Mandatory = $true)][string]$RootPassword)

    Remove-FaultTestData -RootPassword $RootPassword
    Invoke-RootSql -Container "gameserverportfolio-game-db-primary" -RootPassword $RootPassword -Sql @"
USE gamedb;
INSERT INTO player_currencies(user_id,currency_id,amount,version)
VALUES($testUserId,1,5000,1);
"@
    Invoke-RootSql -Container "gameserverportfolio-auction-db-primary" -RootPassword $RootPassword -Sql @"
USE auctiondb;
INSERT INTO auction_listings
(listing_id,seller_user_id,seller_login_id,item_instance_id,item_data_id,item_category,quantity,item_data,search_name,
 search_grade,search_enhancement_level,search_str,search_dex,search_int,search_luk,
 currency_id,start_price,current_bid_price,buyout_price,
 highest_bid_id,highest_bidder_user_id,state,expires_at,version)
VALUES
($listingId,$sellerUserId,'rpc-fault-seller',99300001001,1001,1,1,JSON_OBJECT('str',10),'RPC Fault Sword',1,0,10,0,0,0,
 1,1000,2000,5000,$highestBidId,$highestBidderUserId,2,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 1 DAY),1);
INSERT INTO auction_bids
(bid_id,listing_id,bidder_user_id,currency_id,bid_amount,state,version)
VALUES
($bidId,$listingId,$testUserId,1,$refundAmount,3,1),
($highestBidId,$listingId,$highestBidderUserId,1,2000,2,1);
"@

    Wait-RootSqlScalar -Container "gameserverportfolio-auction-db-replica" `
        -Sql "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=$bidId" `
        -Expected "3:1" `
        -RootPassword $RootPassword
    Wait-RootSqlScalar -Container "gameserverportfolio-auction-db-replica-2" `
        -Sql "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=$bidId" `
        -Expected "3:1" `
        -RootPassword $RootPassword
}

function New-AuctionTicket
{
    param([Parameter(Mandatory = $true)][string]$CaseName)

    $ticket = "rpc-fault-$CaseName-" + [guid]::NewGuid().ToString("N")
    docker exec gameserverportfolio-chat-redis redis-cli SET "chat:active-login:$testUserId" "1" | Out-Null
    docker exec gameserverportfolio-chat-redis redis-cli SETEX "auction:ticket:$ticket" 120 `
        "${testUserId}:1:7270632d6661756c742d75736572" | Out-Null
    if ($LASTEXITCODE -ne 0)
    {
        throw "Failed to seed AuctionAuth ticket for case $CaseName."
    }
    return $ticket
}

function Remove-AuctionTicket
{
    param([AllowEmptyString()][string]$Ticket)

    if ([string]::IsNullOrWhiteSpace($Ticket))
    {
        return
    }
    docker exec gameserverportfolio-chat-redis redis-cli DEL `
        "auction:ticket:$Ticket" "chat:active-login:$testUserId" 2>$null | Out-Null
}

function Assert-CaseLogs
{
    param(
        [Parameter(Mandatory = $true)]$Case,
        [Parameter(Mandatory = $true)][string]$AuctionOutput,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$CacheOutput
    )

    $remainingState = if ($Case.ExpectedResultCode -eq 3) { "OUTBID_CLAIMABLE" } else { "REFUND_PENDING" }
    $structuredPattern =
        "BidRefund completed\..*userId=$testUserId.*listingId=$listingId.*bidId=$bidId.*result=$($Case.ExpectedResultCode).*remainingState=$remainingState"
    if ($AuctionOutput -notmatch $structuredPattern)
    {
        throw "BidRefund structured result log was not found. pattern=$structuredPattern"
    }
    if (-not [string]::IsNullOrWhiteSpace($Case.AuctionLogPattern) -and $AuctionOutput -notmatch $Case.AuctionLogPattern)
    {
        throw "Auction fault-policy log was not found. pattern=$($Case.AuctionLogPattern)"
    }
    if (-not [string]::IsNullOrWhiteSpace($Case.CacheLogPattern) -and $CacheOutput -notmatch $Case.CacheLogPattern)
    {
        throw "Cache fault-injection log was not found. pattern=$($Case.CacheLogPattern)"
    }
}

function Invoke-FaultCase
{
    param(
        [Parameter(Mandatory = $true)]$Case,
        [Parameter(Mandatory = $true)][string]$RunDirectory,
        [Parameter(Mandatory = $true)][string]$RootPassword
    )

    $caseDirectory = Join-Path $RunDirectory $Case.Name
    New-Item -ItemType Directory -Path $caseDirectory -Force | Out-Null
    $cacheStdout = Join-Path $caseDirectory "cache.stdout.log"
    $cacheStderr = Join-Path $caseDirectory "cache.stderr.log"
    $cacheRestartStdout = Join-Path $caseDirectory "cache.restart.stdout.log"
    $cacheRestartStderr = Join-Path $caseDirectory "cache.restart.stderr.log"
    $auctionStdout = Join-Path $caseDirectory "auction.stdout.log"
    $auctionStderr = Join-Path $caseDirectory "auction.stderr.log"
    $clientStdout = Join-Path $caseDirectory "client.stdout.log"
    $clientStderr = Join-Path $caseDirectory "client.stderr.log"

    $cacheProcess = $null
    $cacheRestartProcess = $null
    $auctionProcess = $null
    $clientProcess = $null
    $ticket = ""

    try
    {
        Wait-PortClosed -Port $AuctionPort
        Wait-PortClosed -Port $CachePort
        Initialize-FaultTestData -RootPassword $RootPassword
        $ticket = New-AuctionTicket -CaseName $Case.Name

        if ($Case.StartCache)
        {
            $cacheProcess = Start-CacheServer `
                -CaseDirectory $caseDirectory `
                -StdoutPath $cacheStdout `
                -StderrPath $cacheStderr `
                -AdditionalOverrides $Case.CacheOverrides `
                -RunSeconds 15
            Wait-ListeningPort -Port $CachePort -Process $cacheProcess -Name "CacheServer" -StandardErrorPath $cacheStderr
        }

        $auctionRunSeconds = if ($Case.KillCacheProcess) { 12 } else { 8 }
        $auctionProcess = Start-AuctionServer `
            -CaseDirectory $caseDirectory `
            -StdoutPath $auctionStdout `
            -StderrPath $auctionStderr `
            -RpcTimeoutMilliseconds $Case.RpcTimeoutMilliseconds `
            -AdditionalOverrides $Case.AuctionOverrides `
            -RunSeconds $auctionRunSeconds
        Wait-ListeningPort -Port $AuctionPort -Process $auctionProcess -Name "AuctionHouseServer" -StandardErrorPath $auctionStderr
        if ($Case.StartCache)
        {
            Wait-EstablishedConnection -Process $auctionProcess -RemotePort $CachePort
        }
        else
        {
            Start-Sleep -Milliseconds 300
        }

        $clientProcess = Start-Process -FilePath $clientExecutable `
            -ArgumentList @("--port", "$AuctionPort", "--bid-refund-fault-test", "--ticket", $ticket) `
            -WorkingDirectory (Split-Path -Parent $clientExecutable) `
            -RedirectStandardOutput $clientStdout `
            -RedirectStandardError $clientStderr `
            -WindowStyle Hidden `
            -PassThru

        if ($Case.KillCacheProcess)
        {
            Wait-FilePattern `
                -Path $cacheStdout `
                -Pattern "operation=CreditCurrency.*stage=BeforeGameDB\.Transaction.*action=Delay.*delayMs=5000" `
                -Process $cacheProcess
            $capturedCachePid = $cacheProcess.Id
            Stop-CapturedProcess -Process $cacheProcess
            Wait-PortClosed -Port $CachePort

            if (-not $clientProcess.WaitForExit(12000))
            {
                throw "AuctionDummyClient did not finish after CacheServer PID $capturedCachePid was terminated."
            }

            $cacheRestartProcess = Start-CacheServer `
                -CaseDirectory $caseDirectory `
                -StdoutPath $cacheRestartStdout `
                -StderrPath $cacheRestartStderr `
                -RunSeconds 15
            Wait-ListeningPort `
                -Port $CachePort `
                -Process $cacheRestartProcess `
                -Name "restarted CacheServer" `
                -StandardErrorPath $cacheRestartStderr
            Wait-EstablishedConnection -Process $auctionProcess -RemotePort $CachePort
        }
        elseif (-not $clientProcess.WaitForExit(12000))
        {
            throw "AuctionDummyClient did not finish for case $($Case.Name)."
        }

        $clientProcess.WaitForExit()
        $clientProcess.Refresh()

        $clientOutput = Read-LogText -Path $clientStdout
        $clientError = Read-LogText -Path $clientStderr
        if ($clientOutput -notmatch "BID_REFUND_FAULT_RESULT resultCode=$($Case.ExpectedResultCode) bidId=$bidId")
        {
            throw "Unexpected BidRefund client result. stdout=$clientOutput stderr=$clientError"
        }

        if ($Case.WaitForLateResponse)
        {
            Start-Sleep -Milliseconds 2000
        }

        Wait-RootSqlScalar `
            -Container "gameserverportfolio-auction-db-primary" `
            -Sql "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=$bidId" `
            -Expected $Case.ExpectedBidState `
            -RootPassword $RootPassword
        Wait-RootSqlScalar `
            -Container "gameserverportfolio-game-db-primary" `
            -Sql "SELECT CONCAT(amount,':',version) FROM gamedb.player_currencies WHERE user_id=$testUserId AND currency_id=1" `
            -Expected $Case.ExpectedCurrencyState `
            -RootPassword $RootPassword

        Wait-NaturalExit -Process $auctionProcess -Name "AuctionHouseServer"
        if ($Case.StartCache -and -not $Case.KillCacheProcess)
        {
            Wait-NaturalExit -Process $cacheProcess -Name "CacheServer"
        }

        $auctionOutput = Read-LogText -Path $auctionStdout
        $cacheOutput = Read-LogText -Path $cacheStdout
        Assert-CaseLogs -Case $Case -AuctionOutput $auctionOutput -CacheOutput $cacheOutput

        if ($Case.WaitForLateResponse -and
            $auctionOutput -notmatch "cache RPC response was not completed\..*result=1")
        {
            throw "Late Cache response was not rejected as ERpcCompletionResult::NotFound."
        }
        if ($Case.KillCacheProcess)
        {
            if ($auctionOutput -notmatch "pending cache RPC calls failed after disconnect\..*count=1")
            {
                throw "Pending call disconnect cleanup log was not found."
            }
            $readyCount = ([regex]::Matches($auctionOutput, "cache RPC session ready\.")).Count
            if ($readyCount -lt 2)
            {
                throw "Cache reconnect was not confirmed after restart. readyCount=$readyCount"
            }
        }

        return [pscustomobject]@{
            Name = $Case.Name
            ResultCode = $Case.ExpectedResultCode
            BidState = $Case.ExpectedBidState
            CurrencyState = $Case.ExpectedCurrencyState
            Logs = $caseDirectory
        }
    }
    finally
    {
        Stop-CapturedProcess -Process $clientProcess
        Stop-CapturedProcess -Process $auctionProcess
        Stop-CapturedProcess -Process $cacheRestartProcess
        Stop-CapturedProcess -Process $cacheProcess
        Wait-PortClosed -Port $AuctionPort
        Wait-PortClosed -Port $CachePort
        Remove-AuctionTicket -Ticket $ticket
        Remove-FaultTestData -RootPassword $RootPassword
    }
}

if ($AuctionPort -le 0 -or $AuctionPort -gt 65535 -or
    $CachePort -le 0 -or $CachePort -gt 65535 -or
    $AuctionPort -eq $CachePort)
{
    throw "AuctionPort and CachePort must be different values in range 1..65535."
}
if (-not (Test-Path -LiteralPath $envFile))
{
    throw "Create $envFile from .env.example before running the fault-injection smoke test."
}

$rootPassword = Get-DotEnvValue -Name "MYSQL_ROOT_PASSWORD"
$appPassword = Get-DotEnvValue -Name "MYSQL_PASSWORD"
$previousMySqlPassword = $env:MYSQL_PASSWORD

try
{
    if (-not $SkipDatabaseStart)
    {
        & $databaseStartScript
        if ($LASTEXITCODE -ne 0)
        {
            throw "Auction database environment startup failed."
        }
    }

    docker compose --env-file $envFile -f $loginCompose up -d chat-redis
    if ($LASTEXITCODE -ne 0)
    {
        throw "Failed to start Redis for AuctionAuth."
    }
    for ($attempt = 0; $attempt -lt 60; ++$attempt)
    {
        $redisState = docker inspect --format "{{.State.Health.Status}}" gameserverportfolio-chat-redis 2>$null
        if (($redisState | Out-String).Trim() -eq "healthy")
        {
            break
        }
        Start-Sleep -Seconds 1
    }
    if (($redisState | Out-String).Trim() -ne "healthy")
    {
        throw "Redis did not become healthy."
    }

    if (-not $SkipBuild)
    {
        $msbuildCommand = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
        $msbuild = if ($null -ne $msbuildCommand) { $msbuildCommand.Source } else { $null }
        if ($null -eq $msbuild -or -not (Test-Path -LiteralPath $msbuild))
        {
            $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
            if (Test-Path -LiteralPath $vswhere)
            {
                $visualStudioPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
                if (-not [string]::IsNullOrWhiteSpace($visualStudioPath))
                {
                    $msbuild = Join-Path $visualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
                }
            }
        }
        if ($null -eq $msbuild -or -not (Test-Path -LiteralPath $msbuild))
        {
            throw "MSBuild.exe was not found."
        }

        foreach ($project in @($cacheProject, $auctionProject, $clientProject))
        {
            & $msbuild $project /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
            if ($LASTEXITCODE -ne 0)
            {
                throw "Build failed: $project"
            }
        }
    }

    foreach ($executable in @($cacheExecutable, $auctionExecutable, $clientExecutable))
    {
        if (-not (Test-Path -LiteralPath $executable))
        {
            throw "Executable is missing: $executable"
        }
    }

    $env:MYSQL_PASSWORD = $appPassword
    $runId = "{0}_{1}_{2}" -f (Get-Date -Format "yyyyMMdd_HHmmss_fff"), $PID, ([guid]::NewGuid().ToString("N").Substring(0, 8))
    $runDirectory = Join-Path $repositoryRoot ("Out\RpcFaultInjectionSmoke\" + $runId)
    New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

    $cases = @(
        [pscustomobject]@{
            Name = "01_cache_not_started"
            StartCache = $false
            KillCacheProcess = $false
            WaitForLateResponse = $false
            RpcTimeoutMilliseconds = 3000
            CacheOverrides = @{}
            AuctionOverrides = @{}
            ExpectedResultCode = 3
            ExpectedBidState = "3:3"
            ExpectedCurrencyState = "5000:1"
            AuctionLogPattern = "Cache refund RPC was not started; REFUND_PENDING was reverted\."
            CacheLogPattern = ""
        },
        [pscustomobject]@{
            Name = "02_before_game_db_transaction"
            StartCache = $true
            KillCacheProcess = $false
            WaitForLateResponse = $false
            RpcTimeoutMilliseconds = 3000
            CacheOverrides = @{ "FaultInjection.CreditBeforeDatabaseTransaction" = $true }
            AuctionOverrides = @{}
            ExpectedResultCode = 3
            ExpectedBidState = "3:3"
            ExpectedCurrencyState = "5000:1"
            AuctionLogPattern = "Cache refund credit was rejected; REFUND_PENDING was reverted\."
            CacheLogPattern = "operation=CreditCurrency.*stage=BeforeGameDB\.Transaction.*action=Fail.*faultInjected=true"
        },
        [pscustomobject]@{
            Name = "03_after_game_db_commit_disconnect"
            StartCache = $true
            KillCacheProcess = $false
            WaitForLateResponse = $false
            RpcTimeoutMilliseconds = 3000
            CacheOverrides = @{ "FaultInjection.CreditAfterCommitDisconnect" = $true }
            AuctionOverrides = @{}
            ExpectedResultCode = 6
            ExpectedBidState = "4:2"
            ExpectedCurrencyState = "6500:2"
            AuctionLogPattern = "Cache refund RPC outcome is unknown; REFUND_PENDING was preserved\. error=7"
            CacheLogPattern = "operation=CreditCurrency.*stage=AfterGameDB\.Commit\.BeforeRpcResponse.*action=Disconnect.*faultInjected=true"
        },
        [pscustomobject]@{
            Name = "04_before_auction_db_complete"
            StartCache = $true
            KillCacheProcess = $false
            WaitForLateResponse = $false
            RpcTimeoutMilliseconds = 3000
            CacheOverrides = @{}
            AuctionOverrides = @{ "Debug.FaultInjectBidRefundBeforeComplete" = $true }
            ExpectedResultCode = 6
            ExpectedBidState = "4:2"
            ExpectedCurrencyState = "6500:2"
            AuctionLogPattern = "stage=BeforeAuctionDB\.CompleteBidRefund.*faultInjected=true"
            CacheLogPattern = "Cache mutation completed\. operation=CreditCurrency.*result=0.*balance=6500.*version=2"
        },
        [pscustomobject]@{
            Name = "05a_rpc_timeout_late_response"
            StartCache = $true
            KillCacheProcess = $false
            WaitForLateResponse = $true
            RpcTimeoutMilliseconds = 300
            CacheOverrides = @{ "FaultInjection.CreditAfterCommitDelayMilliseconds" = 1500 }
            AuctionOverrides = @{}
            ExpectedResultCode = 6
            ExpectedBidState = "4:2"
            ExpectedCurrencyState = "6500:2"
            AuctionLogPattern = "Cache refund RPC outcome is unknown; REFUND_PENDING was preserved\. error=6"
            CacheLogPattern = "operation=CreditCurrency.*stage=AfterGameDB\.Commit\.BeforeRpcResponse.*action=Delay.*faultInjected=true.*delayMs=1500"
        },
        [pscustomobject]@{
            Name = "05b_cache_process_terminated"
            StartCache = $true
            KillCacheProcess = $true
            WaitForLateResponse = $false
            RpcTimeoutMilliseconds = 3000
            CacheOverrides = @{ "FaultInjection.CreditBeforeDatabaseDelayMilliseconds" = 5000 }
            AuctionOverrides = @{}
            ExpectedResultCode = 6
            ExpectedBidState = "4:2"
            ExpectedCurrencyState = "5000:1"
            AuctionLogPattern = "Cache refund RPC outcome is unknown; REFUND_PENDING was preserved\. error=7"
            CacheLogPattern = "operation=CreditCurrency.*stage=BeforeGameDB\.Transaction.*action=Delay.*faultInjected=true.*delayMs=5000"
        }
    )

    $results = [System.Collections.Generic.List[object]]::new()
    $failures = [System.Collections.Generic.List[string]]::new()
    foreach ($case in $cases)
    {
        try
        {
            Write-Host "[RUN ] $($case.Name)"
            $results.Add((Invoke-FaultCase -Case $case -RunDirectory $runDirectory -RootPassword $rootPassword))
            Write-Host "[PASS] $($case.Name)"
        }
        catch
        {
            $failures.Add("$($case.Name): $($_.Exception.Message)")
            Write-Host "[FAIL] $($case.Name): $($_.Exception.Message)"
        }
    }

    Write-Host ""
    Write-Host "RPC_FAULT_INJECTION_SUMMARY passed=$($results.Count) failed=$($failures.Count) total=$($cases.Count)"
    foreach ($result in $results)
    {
        Write-Host "  PASS $($result.Name) result=$($result.ResultCode) bid=$($result.BidState) currency=$($result.CurrencyState)"
    }
    foreach ($failure in $failures)
    {
        Write-Host "  FAIL $failure"
    }
    Write-Host "Logs: $runDirectory"

    if ($failures.Count -ne 0)
    {
        throw "RPC fault-injection smoke failed in $($failures.Count) case(s)."
    }
    Write-Host "RPC_FAULT_INJECTION_TEST_SUCCESS cases=$($results.Count)"
}
finally
{
    $env:MYSQL_PASSWORD = $previousMySqlPassword
}

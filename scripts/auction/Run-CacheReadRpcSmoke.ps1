param(
    [int]$AuctionPort = 19112,
    [int]$CachePort = 19113,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$WriteRpc,
    [switch]$SkipBuild,
    [switch]$SkipDatabaseStart
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$envFile = Join-Path $repositoryRoot ".env"
$databaseStartScript = Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1"
$loginCompose = Join-Path $repositoryRoot "Infra\docker-compose.login-platform.yaml"
$cacheProject = Join-Path $repositoryRoot "Cache\CacheServer\CacheServer.vcxproj"
$auctionProject = Join-Path $repositoryRoot "Auction\AuctionHouseServer\AuctionHouseServer.vcxproj"
$clientProject = Join-Path $repositoryRoot "Auction\AuctionDummyClient\AuctionDummyClient.vcxproj"
$cacheExecutable = Join-Path $repositoryRoot "Out\CacheServer\$Configuration\CacheServer.exe"
$auctionExecutable = Join-Path $repositoryRoot "Out\AuctionHouseServer\$Configuration\AuctionHouseServer.exe"
$clientExecutable = Join-Path $repositoryRoot "Out\AuctionDummyClient\$Configuration\AuctionDummyClient.exe"
$cacheConfigTemplate = Join-Path $repositoryRoot "Config\Server\CacheServer.yaml"
$auctionConfigTemplate = Join-Path $repositoryRoot "Config\Server\AuctionHouseServer.yaml"
. (Join-Path $repositoryRoot "scripts\common\ServerConfig.ps1")
$userId = [uint64]930010001
$mailId = [uint64]930010000001
$attachmentId = [uint64]930010000001
$insufficientBidListingId = [uint64]993010000001

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

function Wait-ReplicaValue
{
    param(
        [Parameter(Mandatory = $true)][string]$Container,
        [Parameter(Mandatory = $true)][string]$Sql,
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string]$RootPassword
    )

    for ($attempt = 0; $attempt -lt 80; ++$attempt)
    {
        $actual = docker exec -e "MYSQL_PWD=$RootPassword" $Container mysql -uroot -Nse $Sql 2>$null
        if ($LASTEXITCODE -eq 0 -and ($actual | Out-String).Trim() -eq $Expected)
        {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Replica did not reach expected value '$Expected': $Container"
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
        if ($null -ne (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue))
        {
            return
        }
        Start-Sleep -Milliseconds 100
    }

    $errorOutput = Get-Content -LiteralPath $StandardErrorPath -Raw -ErrorAction SilentlyContinue
    throw "$Name did not enter Listen state. error=$errorOutput"
}

if ($AuctionPort -le 0 -or $AuctionPort -gt 65535 -or $CachePort -le 0 -or $CachePort -gt 65535)
{
    throw "AuctionPort and CachePort must be in range 1..65535."
}
if ($AuctionPort -eq $CachePort)
{
    throw "AuctionPort and CachePort must be different."
}
if (-not (Test-Path -LiteralPath $envFile))
{
    throw "Create $envFile from .env.example before running the smoke test."
}

$rootPassword = Get-DotEnvValue -Name "MYSQL_ROOT_PASSWORD"
$appPassword = Get-DotEnvValue -Name "MYSQL_PASSWORD"
$previousMySqlPassword = $env:MYSQL_PASSWORD
$cacheProcess = $null
$auctionProcess = $null
$seeded = $false
$ticket = "cache-read-rpc-smoke-" + [guid]::NewGuid().ToString("N")

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
            & $msbuild $project /t:Build /p:Configuration=$Configuration /p:Platform=x64 /m /nologo /v:minimal
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

    $writeSeedSql = ""
    if ($WriteRpc)
    {
        $writeSeedSql = @"
DELETE FROM player_currencies WHERE user_id = $userId;
INSERT INTO player_currencies(user_id,currency_id,amount,version) VALUES($userId,1,1000,7);
"@
    }

    $seedSql = @"
USE gamedb;
DELETE attachment
  FROM mail_attachments AS attachment
  JOIN mails AS mail ON mail.mail_id = attachment.mail_id
 WHERE mail.receiver_user_id = $userId;
DELETE FROM mail_attachments WHERE attachment_id = $attachmentId OR mail_id = $mailId;
DELETE FROM mails WHERE receiver_user_id = $userId OR mail_id = $mailId;
DELETE FROM inventory_items WHERE owner_user_id = $userId OR item_instance_id BETWEEN 930010000001 AND 930010000003;
$writeSeedSql
INSERT INTO inventory_items
    (item_instance_id,owner_user_id,item_data_id,quantity,item_data,is_equipped,is_tradable,version)
VALUES
    (930010000001,$userId,1001,1,JSON_OBJECT('str',12,'dex',3,'int',0,'luk',1),1,1,1),
    (930010000002,$userId,2001,20,JSON_OBJECT(),0,1,2),
    (930010000003,$userId,3001,30,JSON_OBJECT(),0,0,3);
INSERT INTO mails(mail_id,receiver_user_id,mail_type,subject,body,state,expires_at,created_at)
VALUES($mailId,$userId,2,'Cache RPC smoke','GameDB reads are served by CacheServer RPC.',1,
       DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 1 DAY),UTC_TIMESTAMP(6));
INSERT INTO mail_attachments
    (attachment_id,mail_id,attachment_type,currency_id,currency_amount,state)
VALUES($attachmentId,$mailId,2,1,777,1);
"@
    Invoke-RootSql -Container "gameserverportfolio-game-db-primary" -Sql $seedSql -RootPassword $rootPassword
    if ($WriteRpc)
    {
        Invoke-RootSql -Container "gameserverportfolio-auction-db-primary" `
            -Sql @"
USE auctiondb;
DELETE FROM auction_bids WHERE listing_id IN ($insufficientBidListingId) OR listing_id IN
    (SELECT listing_id FROM auction_listings WHERE seller_user_id=$userId);
DELETE FROM auction_listings WHERE seller_user_id=$userId OR listing_id=$insufficientBidListingId;
INSERT INTO auction_listings
    (listing_id,seller_user_id,seller_login_id,item_instance_id,item_data_id,item_category,quantity,item_data,search_name,
     search_grade,search_enhancement_level,search_str,search_dex,search_int,search_luk,
     currency_id,start_price,current_bid_price,buyout_price,state,expires_at,version)
VALUES
    ($insufficientBidListingId,930010002,'cache-rpc-seller',993010000001,1001,1,1,JSON_OBJECT('str',10),
     'Insufficient Bid Sword',1,0,10,0,0,0,1,1000,0,10000,2,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 1 DAY),1);
"@ `
            -RootPassword $rootPassword
    }
    $seeded = $true

    $replicaSql = "SELECT CONCAT((SELECT COUNT(*) FROM gamedb.inventory_items WHERE owner_user_id=$userId),':',(SELECT COUNT(*) FROM gamedb.mails WHERE receiver_user_id=$userId),':',(SELECT COUNT(*) FROM gamedb.mail_attachments WHERE mail_id=$mailId))"
    foreach ($replica in @("gameserverportfolio-game-db-replica", "gameserverportfolio-game-db-replica-2"))
    {
        Wait-ReplicaValue -Container $replica -Sql $replicaSql -Expected "3:1:1" -RootPassword $rootPassword
    }

    docker exec gameserverportfolio-chat-redis redis-cli SET "chat:active-login:$userId" "1" | Out-Null
    docker exec gameserverportfolio-chat-redis redis-cli SETEX "auction:ticket:$ticket" 120 "${userId}:1:63616368652d7270632d736d6f6b65" | Out-Null
    if ($LASTEXITCODE -ne 0)
    {
        throw "Failed to seed the AuctionAuth ticket."
    }

    $testRunId = "{0}_{1}_{2}" -f (Get-Date -Format "yyyyMMdd_HHmmss_fff"), $PID, ([guid]::NewGuid().ToString("N").Substring(0, 8))
    $testDirectory = Join-Path $repositoryRoot ("Out\CacheReadRpcSmoke\" + $testRunId)
    New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
    $cacheStandardOutput = Join-Path $testDirectory "cache.stdout.log"
    $cacheStandardError = Join-Path $testDirectory "cache.stderr.log"
    $auctionStandardOutput = Join-Path $testDirectory "auction.stdout.log"
    $auctionStandardError = Join-Path $testDirectory "auction.stderr.log"
    $clientStandardOutput = Join-Path $testDirectory "client.stdout.log"
    $clientStandardError = Join-Path $testDirectory "client.stderr.log"
    $cacheConfig = Join-Path $testDirectory "CacheServer.yaml"
    New-ServerConfigFile -TemplatePath $cacheConfigTemplate -DestinationPath $cacheConfig -Overrides @{
        "CacheServer.Port" = $CachePort
        "CacheServer.PlayerCacheShardCount" = 4
        "CacheServer.DatabaseEnabled" = $true
        "GameDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
        "CacheServer.LogOutputDirectory" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "cache-logs"))
        "Debug.RunSeconds" = 20
    } | Out-Null
    $auctionConfig = Join-Path $testDirectory "AuctionHouseServer.yaml"
    New-ServerConfigFile -TemplatePath $auctionConfigTemplate -DestinationPath $auctionConfig -Overrides @{
        "AuctionHouseServer.Port" = $AuctionPort
        "AuctionHouseServer.RunSeconds" = 15
        "Logging.OutputDirectory" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "auction-logs"))
        "Diagnostics.TimingCsvPath" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "auction_timing.csv"))
        "Authentication.Enabled" = $true
        "CacheRpc.Port" = $CachePort
        "CacheRpc.ReconnectMilliseconds" = 100
        "AuctionDatabase.Enabled" = $true
        "AuctionDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
    } | Out-Null

    $env:MYSQL_PASSWORD = $appPassword
    $cacheProcess = Start-Process `
        -FilePath $cacheExecutable `
        -ArgumentList @("--config", $cacheConfig) `
        -WorkingDirectory $testDirectory `
        -RedirectStandardOutput $cacheStandardOutput `
        -RedirectStandardError $cacheStandardError `
        -WindowStyle Hidden `
        -PassThru
    Wait-ListeningPort -Port $CachePort -Process $cacheProcess -Name "CacheServer" -StandardErrorPath $cacheStandardError

    $auctionProcess = Start-Process `
        -FilePath $auctionExecutable `
        -ArgumentList @("--config", $auctionConfig) `
        -WorkingDirectory $testDirectory `
        -RedirectStandardOutput $auctionStandardOutput `
        -RedirectStandardError $auctionStandardError `
        -WindowStyle Hidden `
        -PassThru
    Wait-ListeningPort -Port $AuctionPort -Process $auctionProcess -Name "AuctionHouseServer" -StandardErrorPath $auctionStandardError

    # stdout is block-buffered while redirected, so readiness is confirmed from the
    # flushed server log after a clean shutdown. The short delay only gives the local
    # Hello exchange time to finish before the one-time authentication ticket is used.
    Start-Sleep -Seconds 1

    $clientMode = if ($WriteRpc) { "--cache-write-rpc-test" } else { "--cache-read-rpc-test" }
    $clientProcess = Start-Process `
        -FilePath $clientExecutable `
        -ArgumentList @("--port", "$AuctionPort", $clientMode, "--ticket", $ticket) `
        -WorkingDirectory (Split-Path -Parent $clientExecutable) `
        -RedirectStandardOutput $clientStandardOutput `
        -RedirectStandardError $clientStandardError `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    $clientOutput = Get-Content -LiteralPath $clientStandardOutput -Raw -ErrorAction SilentlyContinue
    $successMarker = if ($WriteRpc) { "CACHE_WRITE_RPC_TEST_SUCCESS" } else { "CACHE_READ_RPC_TEST_SUCCESS" }
    if ($clientProcess.ExitCode -ne 0 -or $clientOutput -notmatch $successMarker)
    {
        $clientError = Get-Content -LiteralPath $clientStandardError -Raw -ErrorAction SilentlyContinue
        $auctionError = Get-Content -LiteralPath $auctionStandardError -Raw -ErrorAction SilentlyContinue
        $cacheError = Get-Content -LiteralPath $cacheStandardError -Raw -ErrorAction SilentlyContinue
        throw "Cache read RPC client validation failed. client=$clientError auction=$auctionError cache=$cacheError"
    }

    if (-not $auctionProcess.WaitForExit(20000))
    {
        throw "AuctionHouseServer did not exit cleanly within the timeout."
    }
    if (-not $cacheProcess.WaitForExit(25000))
    {
        throw "CacheServer did not exit cleanly within the timeout."
    }

    $sourceCount = 0
    $auctionOutput = Get-Content -LiteralPath $auctionStandardOutput -Raw -ErrorAction SilentlyContinue
    $sourceCount = ([regex]::Matches($auctionOutput, "source=cache-rpc")).Count

    $expectedSourceCount = if ($WriteRpc) { 9 } else { 3 }
    if ($auctionOutput -notmatch "cache RPC session ready\." -or
        $auctionOutput -notmatch "InventoryList completed\..*source=cache-rpc" -or
        $auctionOutput -notmatch "MailList completed\..*source=cache-rpc" -or
        $auctionOutput -notmatch "MailDetail completed\..*source=cache-rpc" -or
        $sourceCount -ne $expectedSourceCount)
    {
        throw "Auction Cache RPC source log validation failed. sourceCount=$sourceCount log=$auctionStandardOutput"
    }

    if ($WriteRpc)
    {
        $gameStateSql = "SELECT CONCAT((SELECT CONCAT(amount,':',version) FROM gamedb.player_currencies WHERE user_id=$userId AND currency_id=1),':',(SELECT COUNT(*) FROM gamedb.inventory_items WHERE owner_user_id=$userId),':',(SELECT state FROM gamedb.mails WHERE mail_id=$mailId),':',(SELECT state FROM gamedb.mail_attachments WHERE attachment_id=$attachmentId))"
        $listingStateSql = "SELECT CONCAT(item_data_id,':',state,':',version,':',start_price,':',buyout_price,':',search_str,':',search_dex,':',search_int,':',search_luk) FROM auctiondb.auction_listings WHERE seller_user_id=$userId ORDER BY listing_id DESC LIMIT 1"
        $revertedBidStateSql = "SELECT CONCAT(state,':',version,':',current_bid_price,':',COALESCE(highest_bid_id,0),':',(SELECT COUNT(*) FROM auctiondb.auction_bids WHERE listing_id=$insufficientBidListingId)) FROM auctiondb.auction_listings WHERE listing_id=$insufficientBidListingId"
        $gameState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary mysql -uroot -Nse $gameStateSql
        $listingState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot -Nse $listingStateSql
        $revertedBidState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot -Nse $revertedBidStateSql
        if (($gameState | Out-String).Trim() -ne "2277:9:3:3:2" -or
            ($listingState | Out-String).Trim() -ne "1002:2:2:1000:2000:4:5:6:7" -or
            ($revertedBidState | Out-String).Trim() -ne "2:3:0:0:0")
        {
            throw "Cache write RPC primary validation failed. game=$gameState listing=$listingState revertedBid=$revertedBidState"
        }

        foreach ($replica in @("gameserverportfolio-game-db-replica", "gameserverportfolio-game-db-replica-2"))
        {
            Wait-ReplicaValue -Container $replica -Sql $gameStateSql -Expected "2277:9:3:3:2" -RootPassword $rootPassword
        }
        foreach ($replica in @("gameserverportfolio-auction-db-replica", "gameserverportfolio-auction-db-replica-2"))
        {
            Wait-ReplicaValue -Container $replica -Sql $listingStateSql -Expected "1002:2:2:1000:2000:4:5:6:7" -RootPassword $rootPassword
            Wait-ReplicaValue -Container $replica -Sql $revertedBidStateSql -Expected "2:3:0:0:0" -RootPassword $rootPassword
        }

        $cacheOutput = Get-Content -LiteralPath $cacheStandardOutput -Raw -ErrorAction SilentlyContinue
        foreach ($operation in @("CreditCurrency", "GrantInventoryItem", "ClaimMailAttachment", "ConsumeInventoryItemForListing"))
        {
            if ($cacheOutput -notmatch "operation=$operation .*result=0")
            {
                throw "Cache mutation success log was not found for $operation. log=$cacheStandardOutput"
            }
        }
        if ($cacheOutput -notmatch "operation=DebitCurrency .*INSUFFICIENT_CURRENCY")
        {
            throw "Cache DebitCurrency insufficient-balance log was not found. log=$cacheStandardOutput"
        }
        if ($auctionOutput -notmatch "DebugCheat completed\..*cheatType=1.*source=cache-rpc" -or
            $auctionOutput -notmatch "DebugCheat completed\..*cheatType=2.*source=cache-rpc" -or
            $auctionOutput -notmatch "MailClaim completed\..*result=0.*source=cache-rpc" -or
            $auctionOutput -notmatch "ListingRegister completed\..*gameData=cache-rpc")
        {
            throw "Auction write RPC success log validation failed. log=$auctionStandardOutput"
        }
    }

    Write-Host $clientOutput.Trim()
    Write-Host $(if ($WriteRpc) {
        "[PASS] CreditCurrency -> GrantInventoryItem -> ClaimMailAttachment -> ListingRegister -> DebitCurrency failure/revert"
    } else {
        "[PASS] AuctionAuth -> InventoryList -> MailList -> MailDetail(found/not-found)"
    })
    Write-Host "[PASS] Cache RPC ready=1 source=cache-rpc count=$sourceCount"
    Write-Host "Logs: $testDirectory"
}
finally
{
    if ($auctionProcess -ne $null -and -not $auctionProcess.HasExited)
    {
        Stop-Process -Id $auctionProcess.Id -Force
    }
    if ($cacheProcess -ne $null -and -not $cacheProcess.HasExited)
    {
        Stop-Process -Id $cacheProcess.Id -Force
    }

    docker exec gameserverportfolio-chat-redis redis-cli DEL "auction:ticket:$ticket" "chat:active-login:$userId" 2>$null | Out-Null

    if ($seeded)
    {
        $cleanupSql = @"
USE gamedb;
DELETE FROM mail_attachments WHERE attachment_id=$attachmentId OR mail_id=$mailId;
DELETE FROM mails WHERE mail_id=$mailId;
DELETE FROM inventory_items WHERE owner_user_id=$userId OR item_instance_id BETWEEN 930010000001 AND 930010000003;
DELETE FROM player_currencies WHERE user_id=$userId;
"@
        $cleanupSql | docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary mysql -uroot 2>$null
        if ($WriteRpc)
        {
            "USE auctiondb; DELETE FROM auction_bids WHERE listing_id=$insufficientBidListingId OR listing_id IN (SELECT listing_id FROM auction_listings WHERE seller_user_id=$userId); DELETE FROM auction_listings WHERE seller_user_id=$userId OR listing_id=$insufficientBidListingId;" |
                docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot 2>$null
        }
    }

    $env:MYSQL_PASSWORD = $previousMySqlPassword
}

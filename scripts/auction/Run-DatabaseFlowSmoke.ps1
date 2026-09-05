param(
    [int]$Port = 19102,
    [int]$CachePort = 19103,
    [switch]$SkipBuild,
    [switch]$InjectListingRegisterAfterAuctionCommit
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $scriptDirectory)
$envFile = Join-Path $repositoryRoot ".env"
$cacheProject = Join-Path $repositoryRoot "Cache\CacheServer\CacheServer.vcxproj"
$serverProject = Join-Path $repositoryRoot "Auction\AuctionHouseServer\AuctionHouseServer.vcxproj"
$clientProject = Join-Path $repositoryRoot "Auction\AuctionDummyClient\AuctionDummyClient.vcxproj"
$cacheExecutable = Join-Path $repositoryRoot "Out\CacheServer\Debug\CacheServer.exe"
$serverExecutable = Join-Path $repositoryRoot "Out\AuctionHouseServer\Debug\AuctionHouseServer.exe"
$clientExecutable = Join-Path $repositoryRoot "Out\AuctionDummyClient\Debug\AuctionDummyClient.exe"
$cacheConfigTemplate = Join-Path $repositoryRoot "Config\Server\CacheServer.yaml"
$serverConfigTemplate = Join-Path $repositoryRoot "Config\Server\AuctionHouseServer.yaml"
$inventoryPolicyPath = Join-Path $repositoryRoot "Generated\GameData\Data\Server\InventoryPolicy.yaml"
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

function Invoke-RootSql([string]$Container, [string]$Sql, [string]$RootPassword)
{
    $Sql | docker exec -i -e "MYSQL_PWD=$RootPassword" $Container mysql -uroot
    if ($LASTEXITCODE -ne 0)
    {
        throw "SQL execution failed in $Container"
    }
}

function Wait-ReplicaValue([string]$Container, [string]$Sql, [string]$Expected, [string]$RootPassword)
{
    for ($attempt = 0; $attempt -lt 40; ++$attempt)
    {
        $actual = docker exec -e "MYSQL_PWD=$RootPassword" $Container mysql -uroot -Nse $Sql
        if ($LASTEXITCODE -eq 0 -and ($actual | Out-String).Trim() -eq $Expected)
        {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Replica did not reach expected value '$Expected': $Container"
}

function Wait-ListeningPort(
    [int]$ListenPort,
    [System.Diagnostics.Process]$Process,
    [string]$Name,
    [string]$StandardErrorPath)
{
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline -and -not $Process.HasExited)
    {
        if ($null -ne (Get-NetTCPConnection -State Listen -LocalPort $ListenPort -ErrorAction SilentlyContinue))
        {
            return
        }
        Start-Sleep -Milliseconds 100
    }

    $errorOutput = Get-Content -LiteralPath $StandardErrorPath -Raw -ErrorAction SilentlyContinue
    throw "$Name did not enter Listen state. error=$errorOutput"
}

if ($Port -le 0 -or $Port -gt 65535 -or $CachePort -le 0 -or $CachePort -gt 65535 -or $Port -eq $CachePort)
{
    throw "Port and CachePort must be different values in range 1..65535."
}

if (-not (Test-Path -LiteralPath $envFile))
{
    throw "Create $envFile from .env.example first."
}

$inventoryPolicyLine = Get-Content -LiteralPath $inventoryPolicyPath |
    Where-Object { $_ -match '^\s*MaxInventorySlots\s*:\s*(\d+)\s*$' } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($inventoryPolicyLine) -or
    $inventoryPolicyLine -notmatch '^\s*MaxInventorySlots\s*:\s*(\d+)\s*$')
{
    throw "MaxInventorySlots was not found in $inventoryPolicyPath"
}
$maxInventorySlots = [uint32]$Matches[1]

$rootPassword = Get-DotEnvValue "MYSQL_ROOT_PASSWORD"
$appPassword = Get-DotEnvValue "MYSQL_PASSWORD"
$env:MYSQL_PASSWORD = $appPassword

& (Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1")

$loginCompose = Join-Path $repositoryRoot "Infra\docker-compose.login-platform.yaml"
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

$auctionTicket = "auction-smoke-" + [guid]::NewGuid().ToString("N")
$outbidTicket = "auction-outbid-smoke-" + [guid]::NewGuid().ToString("N")
$sellerTicket = "auction-seller-smoke-" + [guid]::NewGuid().ToString("N")
# This range is reserved for this smoke flow so cleanup never targets normal
# developer accounts. Fixed listing/bid IDs below are reserved by the same test.
$buyerUserId = [uint64]940020001
$outbidUserId = [uint64]940020002
$competingBidderUserId = [uint64]940020004
$sellerUserId = [uint64]940020005
$testUserIdsSql = "$buyerUserId,$outbidUserId,$competingBidderUserId,$sellerUserId"
$testListingIdsSql = "99000002,99000003,99000004,99000005,99000006,99000007,99000008"
$testBidIdsSql = "77000001,77000002,77000003,77000004,77000005,77000010,77000011,77000012"
# Reuse the exact same ownership predicates before seeding and in finally so an
# interrupted run cannot leave terminal listings whose generated mails are later removed.
$gameCleanupStatements = @"
DELETE a FROM mail_attachments AS a
INNER JOIN mails AS m ON m.mail_id=a.mail_id
WHERE m.receiver_user_id IN ($testUserIdsSql);
DELETE FROM mails WHERE receiver_user_id IN ($testUserIdsSql);
DELETE FROM inventory_items WHERE owner_user_id IN ($testUserIdsSql);
DELETE FROM player_currencies WHERE user_id IN ($testUserIdsSql) AND currency_id=1;
"@
$auctionCleanupStatements = @"
DELETE b FROM auction_bids AS b
INNER JOIN auction_listings AS l ON l.listing_id=b.listing_id
WHERE l.seller_user_id IN ($buyerUserId,$sellerUserId);
DELETE FROM auction_bids WHERE bid_id IN ($testBidIdsSql) OR listing_id IN ($testListingIdsSql);
DELETE FROM auction_listings
WHERE seller_user_id IN ($buyerUserId,$sellerUserId) OR listing_id IN ($testListingIdsSql);
"@
$databaseFlowDataInitialized = $false

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
            & $msbuild $project /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
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

$gameSeed = @"
USE gamedb;
$gameCleanupStatements
INSERT INTO player_currencies(user_id,currency_id,amount,version) VALUES($buyerUserId,1,5000,1);
CALL sp_gd_c_inventory_item($buyerUserId,1001,1,1,$maxInventorySlots,JSON_OBJECT('str',12,'dex',0,'int',0,'luk',0),1);
CALL sp_gd_c_inventory_item($buyerUserId,2001,20,99,$maxInventorySlots,JSON_OBJECT(),1);
CALL sp_gd_c_inventory_item($buyerUserId,3001,50,999,$maxInventorySlots,JSON_OBJECT(),1);
"@
$auctionSeed = @"
USE auctiondb;
$auctionCleanupStatements
INSERT INTO auction_listings
(listing_id,seller_user_id,seller_login_id,item_instance_id,item_data_id,item_category,quantity,item_data,search_name,
 search_grade,search_enhancement_level,search_str,search_dex,search_int,search_luk,
 currency_id,start_price,current_bid_price,buyout_price,
 highest_bid_id,highest_bidder_user_id,state,expires_at,version)
VALUES
(99000002,$sellerUserId,'auction-smoke-seller',994020000001,1001,1,1,JSON_OBJECT(),'Warrior Sword',1,0,12,0,0,0,1,1000,2000,5000,
 77000002,$outbidUserId,2,DATE_ADD(NOW(6),INTERVAL 1 DAY),1),
(99000003,$sellerUserId,'auction-smoke-seller',994020000002,1002,1,1,JSON_OBJECT(),'Ranger Bow',1,0,0,12,0,0,1,1000,2000,5000,
 77000003,$outbidUserId,2,DATE_ADD(NOW(6),INTERVAL 1 DAY),1),
(99000004,$sellerUserId,'auction-smoke-seller',994020000003,1002,1,1,JSON_OBJECT(),'Ranger Bow',1,0,0,12,0,0,1,1000,2200,5000,
 77000012,$competingBidderUserId,2,DATE_ADD(NOW(6),INTERVAL 1 DAY),1),
(99000005,$sellerUserId,'auction-smoke-seller',994020000004,2001,2,10,JSON_OBJECT(),'Health Potion',1,0,0,0,0,0,1,100,500,1000,
 77000005,$outbidUserId,2,DATE_ADD(NOW(6),INTERVAL 1 DAY),1),
(99000006,$buyerUserId,'auction-smoke-buyer',994020000005,3002,3,20,JSON_OBJECT(),'Magic Cloth',1,0,0,0,0,0,1,100,1000,2000,
 77000010,$competingBidderUserId,2,DATE_ADD(NOW(6),INTERVAL 1 DAY),1),
(99000007,$sellerUserId,'auction-smoke-seller',994020000006,1002,1,1,JSON_OBJECT('str',0,'dex',12,'int',0,'luk',0),'Expired Ranger Bow',1,0,0,12,0,0,1,1000,2500,5000,
 77000011,$outbidUserId,2,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 12 SECOND),1),
(99000008,$sellerUserId,'auction-smoke-seller',994020000007,3002,3,25,JSON_OBJECT(),'Expired Magic Cloth',1,0,0,0,0,0,1,100,0,1000,
 NULL,NULL,2,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 12 SECOND),1);
INSERT INTO auction_bids
(bid_id,listing_id,bidder_user_id,currency_id,bid_amount,state,version)
VALUES
(77000001,99000002,$buyerUserId,1,1500,3,1),
(77000002,99000002,$outbidUserId,1,2000,2,1),
(77000003,99000003,$outbidUserId,1,2000,2,1),
(77000004,99000004,$outbidUserId,1,1800,3,1),
(77000012,99000004,$competingBidderUserId,1,2200,2,1),
(77000005,99000005,$outbidUserId,1,500,2,1),
(77000010,99000006,$competingBidderUserId,1,1000,2,1),
(77000011,99000007,$outbidUserId,1,2500,2,1);
"@
$cacheProcess = $null
$serverProcess = $null
$databaseFlowDataInitialized = $true

try
{
    Invoke-RootSql "gameserverportfolio-game-db-primary" $gameSeed $rootPassword
    Invoke-RootSql "gameserverportfolio-auction-db-primary" $auctionSeed $rootPassword
    Wait-ReplicaValue "gameserverportfolio-auction-db-replica" `
    "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=77000001" `
    "3:1" $rootPassword
    Wait-ReplicaValue "gameserverportfolio-game-db-replica" `
    "SELECT GROUP_CONCAT(CONCAT(item_data_id,':',quantity) ORDER BY item_instance_id DESC SEPARATOR ',') FROM gamedb.inventory_items WHERE owner_user_id=$buyerUserId" `
    "3001:50,2001:20,1001:1" $rootPassword

# Seed short-lived authentication tickets only after builds and database setup have
# completed. Debug builds can exceed the ticket TTL on a cold machine.
docker exec gameserverportfolio-chat-redis redis-cli SET "chat:active-login:$buyerUserId" "1" | Out-Null
docker exec gameserverportfolio-chat-redis redis-cli SETEX "auction:ticket:$auctionTicket" 120 "${buyerUserId}:1:736d6f6b652d75736572" | Out-Null
docker exec gameserverportfolio-chat-redis redis-cli SET "chat:active-login:$outbidUserId" "1" | Out-Null
docker exec gameserverportfolio-chat-redis redis-cli SETEX "auction:ticket:$outbidTicket" 120 "${outbidUserId}:1:6f75746269642d75736572" | Out-Null
docker exec gameserverportfolio-chat-redis redis-cli SET "chat:active-login:$sellerUserId" "1" | Out-Null
docker exec gameserverportfolio-chat-redis redis-cli SETEX "auction:ticket:$sellerTicket" 120 "${sellerUserId}:1:61756374696f6e2d736d6f6b652d73656c6c6572" | Out-Null
if ($LASTEXITCODE -ne 0)
{
    throw "Failed to seed the one-time AuctionAuth ticket."
}

$testDirectory = Join-Path $repositoryRoot ("Out\AuctionDatabaseFlowTest\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$cacheStdout = Join-Path $testDirectory "cache.stdout.log"
$cacheStderr = Join-Path $testDirectory "cache.stderr.log"
$serverStdout = Join-Path $testDirectory "server.stdout.log"
$serverStderr = Join-Path $testDirectory "server.stderr.log"
$clientStdout = Join-Path $testDirectory "client.stdout.log"
$clientStderr = Join-Path $testDirectory "client.stderr.log"
	$cacheConfig = Join-Path $testDirectory "CacheServer.yaml"
	New-ServerConfigFile -TemplatePath $cacheConfigTemplate -DestinationPath $cacheConfig -Overrides @{
		"CacheServer.Port" = $CachePort
		"CacheServer.PlayerCacheShardCount" = 4
		"CacheServer.DatabaseEnabled" = $true
		"GameDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
		"CacheServer.LogOutputDirectory" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "cache-logs"))
		"Debug.RunSeconds" = 40
	} | Out-Null

	$serverConfig = Join-Path $testDirectory "AuctionHouseServer.yaml"
	New-ServerConfigFile -TemplatePath $serverConfigTemplate -DestinationPath $serverConfig -Overrides @{
		"AuctionHouseServer.Port" = $Port
		"AuctionHouseServer.RunSeconds" = 30
		"Logging.OutputDirectory" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "auction-logs"))
		"Diagnostics.TimingCsvPath" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "auction_timing.csv"))
		"Authentication.Enabled" = $true
		"CacheRpc.Host" = (ConvertTo-ServerConfigYamlString "127.0.0.1")
		"CacheRpc.Port" = $CachePort
		"CacheRpc.ReconnectMilliseconds" = 100
		"AuctionDatabase.Enabled" = $true
		"AuctionDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
		"Debug.FaultInjectListingRegisterAfterAuctionCommit" = [bool]$InjectListingRegisterAfterAuctionCommit
	} | Out-Null

	$cacheProcess = Start-Process -FilePath $cacheExecutable `
		-ArgumentList @("--config", $cacheConfig) `
		-WorkingDirectory $testDirectory `
		-RedirectStandardOutput $cacheStdout `
		-RedirectStandardError $cacheStderr `
		-WindowStyle Hidden -PassThru
	Wait-ListeningPort -ListenPort $CachePort -Process $cacheProcess -Name "CacheServer" -StandardErrorPath $cacheStderr

    $serverProcess = Start-Process -FilePath $serverExecutable `
		-ArgumentList @("--config", $serverConfig) `
        -WorkingDirectory $testDirectory `
        -RedirectStandardOutput $serverStdout `
        -RedirectStandardError $serverStderr `
        -WindowStyle Hidden -PassThru
    Wait-ListeningPort -ListenPort $Port -Process $serverProcess -Name "AuctionHouseServer" -StandardErrorPath $serverStderr
    Start-Sleep -Milliseconds 500

    $clientProcess = Start-Process -FilePath $clientExecutable `
        -ArgumentList @("--port", "$Port", "--database-flow-test", "--ticket", $auctionTicket,
            "--outbid-ticket", $outbidTicket, "--seller-ticket", $sellerTicket) `
        -WorkingDirectory (Split-Path -Parent $clientExecutable) `
        -RedirectStandardOutput $clientStdout `
        -RedirectStandardError $clientStderr `
        -WindowStyle Hidden -Wait -PassThru
    $clientExitCode = $clientProcess.ExitCode
    $clientOutput = Get-Content -LiteralPath $clientStdout -Raw
	if ($InjectListingRegisterAfterAuctionCommit)
	{
		$clientError = Get-Content -LiteralPath $clientStderr -Raw -ErrorAction SilentlyContinue
		Start-Sleep -Milliseconds 500
		$serverOutput = Get-Content -LiteralPath $serverStdout -Raw
		$pendingState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
			mysql -uroot -Nse "SELECT CONCAT(listing_id,':',state,':',version) FROM auctiondb.auction_listings WHERE seller_user_id=$buyerUserId AND item_data_id=1001 ORDER BY listing_id DESC LIMIT 1"
		$inventoryState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
			mysql -uroot -Nse "SELECT CONCAT(item_data_id,':',quantity,':',version) FROM gamedb.inventory_items WHERE owner_user_id=$buyerUserId AND item_data_id=1001"

		if ($clientExitCode -eq 0 -or
			$clientError -notmatch "ListingRegister validation failed\. result=6 listingId=[1-9][0-9]*" -or
			($pendingState | Out-String).Trim() -notmatch "^[1-9][0-9]*:1:1$" -or
			($inventoryState | Out-String).Trim() -ne "1001:1:1" -or
			$serverOutput -notmatch "operation=ListingRegister.*result=PARTIAL_COMMIT\(6\).*failedStep=BeforeCacheRpc\.ConsumeInventoryItem.*auctionDbCommit=SUCCEEDED.*cacheRpc=NOT_ATTEMPTED.*activationCommit=NOT_ATTEMPTED.*remainingState=REGISTER_PENDING.*faultInjected=true")
		{
			throw "Fault injection validation failed. client=$clientError pending=$pendingState inventory=$inventoryState logs=$testDirectory"
		}

		Write-Host "FAULT_INJECTION_TEST_SUCCESS"
		Write-Host "PENDING_STATE listing=$($pendingState.Trim()) inventory=$($inventoryState.Trim())"
		Write-Host "STRUCTURED_FAILURE_LOG verified=1 faultInjected=1"
		Write-Host "Logs: $testDirectory"

		return
	}
    if ($clientExitCode -ne 0 -or
        $clientOutput -notmatch "DATABASE_FLOW_TEST_SUCCESS" -or
        $clientOutput -notmatch "AUTH_REPLAY_REJECTED")
    {
        $clientError = Get-Content -LiteralPath $clientStderr -Raw -ErrorAction SilentlyContinue
        $serverError = Get-Content -LiteralPath $serverStderr -Raw -ErrorAction SilentlyContinue
        throw "Database flow failed. client=$clientError server=$serverError"
    }

    Wait-ReplicaValue "gameserverportfolio-auction-db-primary" `
        "SELECT CONCAT(state,':',COALESCE(final_buyer_user_id,0),':',final_price,':',COALESCE(sale_type,0),':',version) FROM auctiondb.auction_listings WHERE listing_id=99000007" `
        "7:${outbidUserId}:2500:1:3" $rootPassword
    Wait-ReplicaValue "gameserverportfolio-auction-db-primary" `
        "SELECT CONCAT(state,':',version) FROM auctiondb.auction_listings WHERE listing_id=99000008" `
        "9:3" $rootPassword

    $gameState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(amount,':',version) FROM gamedb.player_currencies WHERE user_id=$buyerUserId AND currency_id=1"
    $auctionState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=77000001"
    $itemSearchState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(item_category,':',search_str,':',search_dex,':',search_int,':',search_luk) FROM auctiondb.auction_listings WHERE listing_id=99000002"
    $inventoryState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
        mysql -uroot -Nse "SELECT GROUP_CONCAT(CONCAT(item_data_id,':',quantity,':',version) ORDER BY item_data_id,quantity SEPARATOR ',') FROM gamedb.inventory_items WHERE owner_user_id=$buyerUserId"
    $registeredListingState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(item_data_id,':',quantity,':',state,':',version,':',search_str) FROM auctiondb.auction_listings WHERE seller_user_id=$buyerUserId AND item_data_id=1001"
    $bidListingState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(current_bid_price,':',COALESCE(highest_bidder_user_id,0),':',state,':',version) FROM auctiondb.auction_listings WHERE listing_id=99000003"
    $oldHighestState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=77000003"
    $otherBuyoutState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(state,':',final_buyer_user_id,':',final_price,':',sale_type,':',version) FROM auctiondb.auction_listings WHERE listing_id=99000005"
    $buyoutBidState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=77000005"
    $mailState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
        mysql -uroot -Nse "SELECT CONCAT((SELECT COUNT(*) FROM gamedb.mails WHERE receiver_user_id IN ($testUserIdsSql)),':',(SELECT COUNT(*) FROM gamedb.mail_attachments AS a INNER JOIN gamedb.mails AS m ON m.mail_id=a.mail_id WHERE m.receiver_user_id IN ($testUserIdsSql)),':',(SELECT COUNT(*) FROM gamedb.mails WHERE receiver_user_id=$buyerUserId),':',(SELECT COUNT(*) FROM gamedb.mails WHERE receiver_user_id=$sellerUserId))"
    $mailClaimState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
        mysql -uroot -Nse "SELECT CONCAT((SELECT COUNT(*) FROM gamedb.mails WHERE receiver_user_id IN ($testUserIdsSql) AND state=3),':',(SELECT COUNT(*) FROM gamedb.mail_attachments AS a INNER JOIN gamedb.mails AS m ON m.mail_id=a.mail_id WHERE m.receiver_user_id IN ($testUserIdsSql) AND a.state=2))"
    $sellerCurrencyState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(amount,':',version) FROM gamedb.player_currencies WHERE user_id=$sellerUserId AND currency_id=1"
    $expiredSoldState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(state,':',COALESCE(final_buyer_user_id,0),':',final_price,':',COALESCE(sale_type,0),':',version) FROM auctiondb.auction_listings WHERE listing_id=99000007"
    $expiredBidState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=77000011"
    $expiredUnsoldState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(state,':',version) FROM auctiondb.auction_listings WHERE listing_id=99000008"
    if (($gameState | Out-String).Trim() -ne "500:6" -or
        ($auctionState | Out-String).Trim() -ne "5:3" -or
        ($itemSearchState | Out-String).Trim() -ne "1:12:0:0:0" -or
        ($inventoryState | Out-String).Trim() -ne "1001:1:1,1002:1:1,2001:10:1,2001:20:1,3001:50:1" -or
        ($registeredListingState | Out-String).Trim() -ne "1001:1:8:4:12" -or
        ($bidListingState | Out-String).Trim() -ne "0:0:7:7" -or
        ($oldHighestState | Out-String).Trim() -ne "3:2" -or
        ($otherBuyoutState | Out-String).Trim() -ne "7:${buyerUserId}:1000:2:3" -or
        ($buyoutBidState | Out-String).Trim() -ne "3:2" -or
        ($mailState | Out-String).Trim() -ne "8:8:3:4" -or
        ($mailClaimState | Out-String).Trim() -ne "5:5" -or
        ($sellerCurrencyState | Out-String).Trim() -ne "6000:2" -or
        ($expiredSoldState | Out-String).Trim() -ne "7:${outbidUserId}:2500:1:3" -or
        ($expiredBidState | Out-String).Trim() -ne "6:2" -or
        ($expiredUnsoldState | Out-String).Trim() -ne "9:3")
    {
        throw "Final primary DB validation failed. game=$gameState auction=$auctionState itemSearch=$itemSearchState inventory=$inventoryState listing=$registeredListingState bidListing=$bidListingState oldHighest=$oldHighestState otherBuyout=$otherBuyoutState buyoutBid=$buyoutBidState mails=$mailState claims=$mailClaimState sellerCurrency=$sellerCurrencyState expiredSold=$expiredSoldState expiredBid=$expiredBidState expiredUnsold=$expiredUnsoldState"
    }

    Wait-ReplicaValue "gameserverportfolio-auction-db-replica" `
        "SELECT CONCAT(item_data_id,':',quantity,':',state,':',version,':',search_str) FROM auctiondb.auction_listings WHERE seller_user_id=$buyerUserId AND item_data_id=1001" `
        "1001:1:8:4:12" $rootPassword

	if (-not $serverProcess.HasExited)
	{
		Wait-Process -Id $serverProcess.Id -Timeout 35
	}
	Start-Sleep -Milliseconds 500
    $serverOutput = Get-Content -LiteralPath $serverStdout -Raw
    if ($serverOutput -notmatch "item data loaded\. count=8" -or
        $serverOutput -notmatch "InventoryList completed\. userId=$buyerUserId shardIndex=1 itemCount=3 source=cache-rpc" -or
        $serverOutput -notmatch "ListingRegister completed\. userId=$buyerUserId itemInstanceId=.* listingId=.* shardIndex=1" -or
        $serverOutput -notmatch "ListingSearch completed\. userId=$buyerUserId itemCategory=1 itemDataIdCount=1 sortType=1 count=2 source=replica" -or
        $serverOutput -notmatch "ListingDetail completed\. userId=$buyerUserId listingId=.* source=replica" -or
        $serverOutput -notmatch "MyBidList completed\. userId=$buyerUserId shardIndex=1" -or
        $serverOutput -notmatch "BidRefund completed\. userId=$buyerUserId listingId=99000002 shardIndex=1" -or
        $serverOutput -notmatch "outbid catch-up completed\. userId=$outbidUserId count=1" -or
        $serverOutput -notmatch "Bid completed\. userId=$buyerUserId listingId=99000003" -or
        $serverOutput -notmatch "Buyout completed\. userId=$buyerUserId listingId=99000003" -or
        $serverOutput -notmatch "Buyout completed\. userId=$buyerUserId listingId=99000005" -or
        $serverOutput -notmatch "ListingCancel completed\. userId=$buyerUserId listingId=" -or
        $serverOutput -notmatch "expiration settled\. listingId=99000007 winnerUserId=$outbidUserId finalPrice=2500" -or
        $serverOutput -notmatch "expiration settled\. listingId=99000008 winnerUserId=0 finalPrice=0")
    {
        throw "Unified userId routing validation failed. See $serverStdout"
    }

    Write-Host $clientOutput.Trim()
    Write-Host "PRIMARY_DB_STATE gameCurrency=500:6 auctionBid=5:3"
    Write-Host "ITEM_SEARCH_STATE category=1 str=12 dex=0 int=0 luk=0"
    Write-Host "INVENTORY_STATE claimedItems=1002:1,2001:10 returnedItem=1001:1 retainedItems=3001:50,2001:20"
    Write-Host "LISTING_REGISTER_STATE itemData=1001 quantity=1 state=CANCELLED version=4 str=12 replicaVerified=1"
    Write-Host "UNIFIED_ROUTING_STATE userId=$buyerUserId shardIndex=1 listingIdModulo=2"
    Write-Host "BID_STATE listing=99000003 state=SOLD version=7 buyerBid=WON oldHighest=OUTBID_CLAIMABLE"
    Write-Host "OUTBID_NOTIFICATION_STATE loginCatchUp=1 onlineImmediate=1"
    Write-Host "BUYOUT_STATE ownHeldAmountReused=1 otherHighestOutbid=1 buyerItemMails=2 sellerCurrencyMails=2"
    Write-Host "MAIL_STATE list=1 detail=1 itemClaims=3 currencyClaims=2 duplicateRejected=1 ownershipChecked=1"
    Write-Host "CANCEL_STATE success=1 returnMailClaimed=1 staleRejected=1 highestBidRejected=1 duplicateRejected=1"
    Write-Host "EXPIRATION_STATE pollSeconds=5 soldWithWinner=1 expiredWithoutBid=1 winnerOnlineNoti=1 winnerItemMail=1 sellerCurrencyMail=1 unsoldReturnMail=1"
    Write-Host "Logs: $testDirectory"
}
finally
{
    if ($serverProcess -ne $null -and -not $serverProcess.HasExited)
    {
        Stop-Process -Id $serverProcess.Id -Force
    }
    if ($cacheProcess -ne $null -and -not $cacheProcess.HasExited)
    {
        Stop-Process -Id $cacheProcess.Id -Force
    }
    docker exec gameserverportfolio-chat-redis redis-cli DEL `
        "auction:ticket:$auctionTicket" "auction:ticket:$outbidTicket" "auction:ticket:$sellerTicket" `
        "chat:active-login:$buyerUserId" "chat:active-login:$outbidUserId" "chat:active-login:$sellerUserId" 2>$null | Out-Null

    if ($databaseFlowDataInitialized)
    {
        Invoke-RootSql "gameserverportfolio-auction-db-primary" "USE auctiondb; $auctionCleanupStatements" $rootPassword
        Invoke-RootSql "gameserverportfolio-game-db-primary" "USE gamedb; $gameCleanupStatements" $rootPassword
        Write-Host "TEST_DATA_CLEANUP users=$testUserIdsSql listings=$testListingIdsSql"
    }
}

param(
    [int]$Port = 19102,
    [switch]$SkipBuild,
    [switch]$InjectListingRegisterAfterAuctionCommit
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $scriptDirectory)
$envFile = Join-Path $repositoryRoot ".env"
$serverProject = Join-Path $repositoryRoot "Auction\AuctionHouseServer\AuctionHouseServer.vcxproj"
$clientProject = Join-Path $repositoryRoot "Auction\AuctionDummyClient\AuctionDummyClient.vcxproj"
$serverExecutable = Join-Path $repositoryRoot "Out\AuctionHouseServer\Debug\AuctionHouseServer.exe"
$clientExecutable = Join-Path $repositoryRoot "Out\AuctionDummyClient\Debug\AuctionDummyClient.exe"

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

if (-not (Test-Path -LiteralPath $envFile))
{
    throw "Create $envFile from .env.example first."
}

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
    $redisState = docker inspect --format "{{.State.Health.Status}}" refactoringserver-chat-redis 2>$null
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
docker exec refactoringserver-chat-redis redis-cli SET "chat:active-login:3001" "1" | Out-Null
docker exec refactoringserver-chat-redis redis-cli SETEX "auction:ticket:$auctionTicket" 60 "3001:1:736d6f6b652d75736572" | Out-Null
docker exec refactoringserver-chat-redis redis-cli SET "chat:active-login:3002" "1" | Out-Null
docker exec refactoringserver-chat-redis redis-cli SETEX "auction:ticket:$outbidTicket" 60 "3002:1:6f75746269642d75736572" | Out-Null
docker exec refactoringserver-chat-redis redis-cli SET "chat:active-login:2001" "1" | Out-Null
docker exec refactoringserver-chat-redis redis-cli SETEX "auction:ticket:$sellerTicket" 60 "2001:1:73656c6c657232303031" | Out-Null
if ($LASTEXITCODE -ne 0)
{
    throw "Failed to seed the one-time AuctionAuth ticket."
}

if (-not $SkipBuild)
{
    $env:TEMP = "E:\Procademy\build-temp"
    $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Path $env:TEMP -Force | Out-Null
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $visualStudioPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    $msbuild = Join-Path $visualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
    foreach ($project in @($serverProject, $clientProject))
    {
        & $msbuild $project /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
        if ($LASTEXITCODE -ne 0)
        {
            throw "Build failed: $project"
        }
    }
}

$gameSeed = @"
USE gamedb;
DELETE FROM inventory_items WHERE owner_user_id=3001;
DELETE FROM player_currencies WHERE user_id=3001 AND currency_id=1;
DELETE FROM player_currencies WHERE user_id=2001 AND currency_id=1;
DELETE FROM mail_attachments;
DELETE FROM mails;
INSERT INTO player_currencies(user_id,currency_id,amount,version) VALUES(3001,1,5000,1);
CALL sp_gd_c_inventory_item(3001,1001,1,1,JSON_OBJECT('str',12,'dex',0,'int',0,'luk',0),1);
CALL sp_gd_c_inventory_item(3001,2001,20,99,JSON_OBJECT(),1);
CALL sp_gd_c_inventory_item(3001,3001,50,999,JSON_OBJECT(),1);
"@
$auctionSeed = @"
USE auctiondb;
DELETE FROM auction_bids WHERE listing_id BETWEEN 99100001 AND 99299999;
DELETE FROM auction_listings WHERE listing_id BETWEEN 99100001 AND 99299999;
DELETE FROM auction_bids WHERE bid_id IN (77000001,77000002,77000003,77000004,77000005,77000010,77000011,77000012) OR listing_id IN (99000001,99000002,99000003,99000004,99000005,99000006,99000007,99000008);
DELETE FROM auction_listings WHERE seller_user_id=3001;
DELETE FROM auction_listings WHERE listing_id IN (99000001,99000002,99000003,99000004,99000005,99000006,99000007,99000008);
INSERT INTO auction_listings
(listing_id,seller_user_id,seller_login_id,item_instance_id,item_data_id,item_category,quantity,item_data,search_name,
 search_grade,search_enhancement_level,search_str,search_dex,search_int,search_luk,
 currency_id,start_price,current_bid_price,buyout_price,
 highest_bid_id,highest_bidder_user_id,state,expires_at,version)
VALUES
(99000002,2001,'seller2001',88000001,1001,1,1,JSON_OBJECT(),'Warrior Sword',1,0,12,0,0,0,1,1000,2000,5000,
 77000002,3002,2,DATE_ADD(NOW(6),INTERVAL 1 DAY),1),
(99000003,2001,'seller2001',88000003,1002,1,1,JSON_OBJECT(),'Ranger Bow',1,0,0,12,0,0,1,1000,2000,5000,
 77000003,3002,2,DATE_ADD(NOW(6),INTERVAL 1 DAY),1),
(99000004,2001,'seller2001',88000004,1002,1,1,JSON_OBJECT(),'Ranger Bow',1,0,0,12,0,0,1,1000,2200,5000,
 77000012,3004,2,DATE_ADD(NOW(6),INTERVAL 1 DAY),1),
(99000005,2001,'seller2001',88000005,2001,2,10,JSON_OBJECT(),'Health Potion',1,0,0,0,0,0,1,100,500,1000,
 77000005,3002,2,DATE_ADD(NOW(6),INTERVAL 1 DAY),1),
(99000006,3001,'smoke-user',9999000006,3002,3,20,JSON_OBJECT(),'Magic Cloth',1,0,0,0,0,0,1,100,1000,2000,
 77000010,3004,2,DATE_ADD(NOW(6),INTERVAL 1 DAY),1),
(99000007,2001,'seller2001',9999000007,1002,1,1,JSON_OBJECT('str',0,'dex',12,'int',0,'luk',0),'Expired Ranger Bow',1,0,0,12,0,0,1,1000,2500,5000,
 77000011,3002,2,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 12 SECOND),1),
(99000008,2001,'seller2001',9999000008,3002,3,25,JSON_OBJECT(),'Expired Magic Cloth',1,0,0,0,0,0,1,100,0,1000,
 NULL,NULL,2,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 12 SECOND),1);
INSERT INTO auction_bids
(bid_id,listing_id,bidder_user_id,currency_id,bid_amount,state,version)
VALUES
(77000001,99000002,3001,1,1500,3,1),
(77000002,99000002,3002,1,2000,2,1),
(77000003,99000003,3002,1,2000,2,1),
(77000004,99000004,3002,1,1800,3,1),
(77000012,99000004,3004,1,2200,2,1),
(77000005,99000005,3002,1,500,2,1),
(77000010,99000006,3004,1,1000,2,1),
(77000011,99000007,3002,1,2500,2,1);
"@
Invoke-RootSql "gameserverportfolio-game-db-primary" $gameSeed $rootPassword
Invoke-RootSql "gameserverportfolio-auction-db-primary" $auctionSeed $rootPassword
Wait-ReplicaValue "gameserverportfolio-auction-db-replica" `
    "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=77000001" `
    "3:1" $rootPassword
Wait-ReplicaValue "gameserverportfolio-game-db-replica" `
    "SELECT GROUP_CONCAT(CONCAT(item_data_id,':',quantity) ORDER BY item_instance_id DESC SEPARATOR ',') FROM gamedb.inventory_items WHERE owner_user_id=3001" `
    "3001:50,2001:20,1001:1" $rootPassword

$testDirectory = Join-Path $repositoryRoot ("Out\AuctionDatabaseFlowTest\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$serverStdout = Join-Path $testDirectory "server.stdout.log"
$serverStderr = Join-Path $testDirectory "server.stderr.log"
$clientStdout = Join-Path $testDirectory "client.stdout.log"
$clientStderr = Join-Path $testDirectory "client.stderr.log"
$serverProcess = $null

try
{
	$serverArguments = @("--port", "$Port", "--run-seconds", "30", "--database-enabled", "--redis-auth-enabled")
	if ($InjectListingRegisterAfterAuctionCommit)
	{
		$serverArguments += "--fault-inject-listing-register-after-auction-commit"
	}
    $serverProcess = Start-Process -FilePath $serverExecutable `
		-ArgumentList $serverArguments `
        -WorkingDirectory $testDirectory `
        -RedirectStandardOutput $serverStdout `
        -RedirectStandardError $serverStderr `
        -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1

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
			mysql -uroot -Nse "SELECT CONCAT(listing_id,':',state,':',version) FROM auctiondb.auction_listings WHERE seller_user_id=3001 AND item_data_id=1001 ORDER BY listing_id DESC LIMIT 1"
		$inventoryState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
			mysql -uroot -Nse "SELECT CONCAT(item_data_id,':',quantity,':',version) FROM gamedb.inventory_items WHERE owner_user_id=3001 AND item_data_id=1001"

		if ($clientExitCode -eq 0 -or
			$clientError -notmatch "ListingRegister validation failed\. result=6 listingId=[1-9][0-9]*" -or
			($pendingState | Out-String).Trim() -notmatch "^[1-9][0-9]*:1:1$" -or
			($inventoryState | Out-String).Trim() -ne "1001:1:1" -or
			$serverOutput -notmatch "operation=ListingRegister.*result=PARTIAL_COMMIT\(6\).*failedStep=BeforeGameDB\.Commit.*auctionDbCommit=SUCCEEDED.*gameDbCommit=NOT_ATTEMPTED.*remainingState=REGISTER_PENDING.*faultInjected=true")
		{
			throw "Fault injection validation failed. client=$clientError pending=$pendingState inventory=$inventoryState logs=$testDirectory"
		}

		Write-Host "FAULT_INJECTION_TEST_SUCCESS"
		Write-Host "PENDING_STATE listing=$($pendingState.Trim()) inventory=$($inventoryState.Trim())"
		Write-Host "STRUCTURED_FAILURE_LOG verified=1 faultInjected=1"
		Write-Host "Logs: $testDirectory"

		Invoke-RootSql "gameserverportfolio-auction-db-primary" `
			"USE auctiondb; DELETE FROM auction_listings WHERE seller_user_id=3001 AND item_data_id=1001 AND state=1;" `
			$rootPassword
		Write-Host "TEST_DATA_CLEANUP pendingListingDeleted=1 inventoryPreserved=1"
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
        "7:3002:2500:1:3" $rootPassword
    Wait-ReplicaValue "gameserverportfolio-auction-db-primary" `
        "SELECT CONCAT(state,':',version) FROM auctiondb.auction_listings WHERE listing_id=99000008" `
        "9:3" $rootPassword

    $gameState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(amount,':',version) FROM gamedb.player_currencies WHERE user_id=3001 AND currency_id=1"
    $auctionState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=77000001"
    $itemSearchState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(item_category,':',search_str,':',search_dex,':',search_int,':',search_luk) FROM auctiondb.auction_listings WHERE listing_id=99000002"
    $inventoryState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
        mysql -uroot -Nse "SELECT GROUP_CONCAT(CONCAT(item_data_id,':',quantity,':',version) ORDER BY item_data_id,quantity SEPARATOR ',') FROM gamedb.inventory_items WHERE owner_user_id=3001"
    $registeredListingState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(item_data_id,':',quantity,':',state,':',version,':',search_str) FROM auctiondb.auction_listings WHERE seller_user_id=3001 AND item_data_id=1001"
    $bidListingState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(current_bid_price,':',COALESCE(highest_bidder_user_id,0),':',state,':',version) FROM auctiondb.auction_listings WHERE listing_id=99000003"
    $oldHighestState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=77000003"
    $otherBuyoutState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(state,':',final_buyer_user_id,':',final_price,':',sale_type,':',version) FROM auctiondb.auction_listings WHERE listing_id=99000005"
    $buyoutBidState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(state,':',version) FROM auctiondb.auction_bids WHERE bid_id=77000005"
    $mailState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
        mysql -uroot -Nse "SELECT CONCAT((SELECT COUNT(*) FROM gamedb.mails),':',(SELECT COUNT(*) FROM gamedb.mail_attachments),':',(SELECT COUNT(*) FROM gamedb.mails WHERE receiver_user_id=3001),':',(SELECT COUNT(*) FROM gamedb.mails WHERE receiver_user_id=2001))"
    $mailClaimState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
        mysql -uroot -Nse "SELECT CONCAT((SELECT COUNT(*) FROM gamedb.mails WHERE state=3),':',(SELECT COUNT(*) FROM gamedb.mail_attachments WHERE state=2))"
    $sellerCurrencyState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary `
        mysql -uroot -Nse "SELECT CONCAT(amount,':',version) FROM gamedb.player_currencies WHERE user_id=2001 AND currency_id=1"
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
        ($otherBuyoutState | Out-String).Trim() -ne "7:3001:1000:2:3" -or
        ($buyoutBidState | Out-String).Trim() -ne "3:2" -or
        ($mailState | Out-String).Trim() -ne "8:8:3:4" -or
        ($mailClaimState | Out-String).Trim() -ne "5:5" -or
        ($sellerCurrencyState | Out-String).Trim() -ne "6000:2" -or
        ($expiredSoldState | Out-String).Trim() -ne "7:3002:2500:1:3" -or
        ($expiredBidState | Out-String).Trim() -ne "6:2" -or
        ($expiredUnsoldState | Out-String).Trim() -ne "9:3")
    {
        throw "Final primary DB validation failed. game=$gameState auction=$auctionState itemSearch=$itemSearchState inventory=$inventoryState listing=$registeredListingState bidListing=$bidListingState oldHighest=$oldHighestState otherBuyout=$otherBuyoutState buyoutBid=$buyoutBidState mails=$mailState claims=$mailClaimState sellerCurrency=$sellerCurrencyState expiredSold=$expiredSoldState expiredBid=$expiredBidState expiredUnsold=$expiredUnsoldState"
    }

    Wait-ReplicaValue "gameserverportfolio-auction-db-replica" `
        "SELECT CONCAT(item_data_id,':',quantity,':',state,':',version,':',search_str) FROM auctiondb.auction_listings WHERE seller_user_id=3001 AND item_data_id=1001" `
        "1001:1:8:4:12" $rootPassword

	if (-not $serverProcess.HasExited)
	{
		Wait-Process -Id $serverProcess.Id -Timeout 35
	}
	Start-Sleep -Milliseconds 500
    $serverOutput = Get-Content -LiteralPath $serverStdout -Raw
    if ($serverOutput -notmatch "item data loaded\. count=8" -or
        $serverOutput -notmatch "InventoryList completed\. userId=3001 shardIndex=1 itemCount=3" -or
        $serverOutput -notmatch "ListingRegister completed\. userId=3001 itemInstanceId=.* listingId=.* shardIndex=1" -or
        $serverOutput -notmatch "ListingSearch completed\. userId=3001 itemCategory=1 itemDataIdCount=1 sortType=1 count=2 source=replica" -or
        $serverOutput -notmatch "ListingDetail completed\. userId=3001 listingId=.* source=replica" -or
        $serverOutput -notmatch "MyBidList completed\. userId=3001 shardIndex=1" -or
        $serverOutput -notmatch "BidRefund completed\. userId=3001 listingId=99000002 shardIndex=1" -or
        $serverOutput -notmatch "outbid catch-up completed\. userId=3002 count=1" -or
        $serverOutput -notmatch "Bid completed\. userId=3001 listingId=99000003" -or
        $serverOutput -notmatch "Buyout completed\. userId=3001 listingId=99000003" -or
        $serverOutput -notmatch "Buyout completed\. userId=3001 listingId=99000005" -or
        $serverOutput -notmatch "ListingCancel completed\. userId=3001 listingId=" -or
        $serverOutput -notmatch "expiration settled\. listingId=99000007 winnerUserId=3002 finalPrice=2500" -or
        $serverOutput -notmatch "expiration settled\. listingId=99000008 winnerUserId=0 finalPrice=0")
    {
        throw "Unified userId routing validation failed. See $serverStdout"
    }

    Write-Host $clientOutput.Trim()
    Write-Host "PRIMARY_DB_STATE gameCurrency=500:6 auctionBid=5:3"
    Write-Host "ITEM_SEARCH_STATE category=1 str=12 dex=0 int=0 luk=0"
    Write-Host "INVENTORY_STATE claimedItems=1002:1,2001:10 returnedItem=1001:1 retainedItems=3001:50,2001:20"
    Write-Host "LISTING_REGISTER_STATE itemData=1001 quantity=1 state=CANCELLED version=4 str=12 replicaVerified=1"
    Write-Host "UNIFIED_ROUTING_STATE userId=3001 shardIndex=1 listingIdModulo=2"
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
}

param(
    [int]$Port = 19103,
    [switch]$SkipBuild
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
        Where-Object { $_ -match "^$([regex]::Escape($Name))=" } | Select-Object -Last 1
    if ([string]::IsNullOrWhiteSpace($line)) { throw "Missing $Name in $envFile" }
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
    if ($LASTEXITCODE -ne 0) { throw "SQL execution failed in $Container" }
}

if (-not (Test-Path -LiteralPath $envFile)) { throw "Create $envFile from .env.example first." }
$rootPassword = Get-DotEnvValue "MYSQL_ROOT_PASSWORD"
$env:MYSQL_PASSWORD = Get-DotEnvValue "MYSQL_PASSWORD"

& (Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1")
$loginCompose = Join-Path $repositoryRoot "Infra\docker-compose.login-platform.yaml"
docker compose --env-file $envFile -f $loginCompose up -d chat-redis
if ($LASTEXITCODE -ne 0) { throw "Failed to start Redis." }
for ($attempt = 0; $attempt -lt 60; ++$attempt)
{
    $redisState = docker inspect --format "{{.State.Health.Status}}" refactoringserver-chat-redis 2>$null
    if (($redisState | Out-String).Trim() -eq "healthy") { break }
    Start-Sleep -Seconds 1
}
if (($redisState | Out-String).Trim() -ne "healthy") { throw "Redis did not become healthy." }

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
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $project" }
    }
}

$gameSeed = @"
USE gamedb;
DELETE FROM mail_attachments;
DELETE FROM mails;
DELETE FROM player_currencies WHERE user_id IN (4101,4102,4103,4104,4200);
INSERT INTO player_currencies(user_id,currency_id,amount,version) VALUES
(4101,1,20000,1),(4102,1,20000,1),(4103,1,20000,1),(4104,1,20000,1),(4200,1,0,1);
"@
$expirationRaceUnixMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() + 8000
$expirationRaceUtc = [DateTimeOffset]::FromUnixTimeMilliseconds($expirationRaceUnixMs).UtcDateTime.ToString(
    "yyyy-MM-dd HH:mm:ss.fff", [System.Globalization.CultureInfo]::InvariantCulture)
$auctionSeed = @"
USE auctiondb;
DELETE FROM auction_bids WHERE listing_id IN (99100001,99100002,99100003,99100004,99100005,99100006);
DELETE FROM auction_listings WHERE listing_id IN (99100001,99100002,99100003,99100004,99100005,99100006);
INSERT INTO auction_listings
(listing_id,seller_user_id,seller_login_id,item_instance_id,item_data_id,item_category,quantity,item_data,search_name,
 search_grade,search_enhancement_level,search_str,search_dex,search_int,search_luk,
 currency_id,start_price,current_bid_price,buyout_price,state,expires_at,version)
VALUES
(99100001,4200,'concurrency-seller',9999100001,1001,1,1,JSON_OBJECT('str',10),'Concurrency Sword',1,0,10,0,0,0,1,1000,0,10000,2,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 1 DAY),1),
(99100002,4200,'concurrency-seller',9999100002,1002,1,1,JSON_OBJECT('dex',10),'Bid Buyout Bow',1,0,0,10,0,0,1,1000,0,5000,2,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 1 DAY),1),
(99100003,4200,'concurrency-seller',9999100003,3002,3,10,JSON_OBJECT(),'Buyout Cancel Cloth',1,0,0,0,0,0,1,1000,0,5000,2,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 1 DAY),1),
(99100004,4200,'concurrency-seller',9999100004,1001,1,1,JSON_OBJECT('str',11),'Expire Bid Sword',1,0,11,0,0,0,1,1000,0,5000,2,'$expirationRaceUtc',1),
(99100005,4200,'concurrency-seller',9999100005,1002,1,1,JSON_OBJECT('dex',11),'Expire Buyout Bow',1,0,0,11,0,0,1,1000,0,5000,2,'$expirationRaceUtc',1),
(99100006,4200,'concurrency-seller',9999100006,3002,3,10,JSON_OBJECT(),'Expire Cancel Cloth',1,0,0,0,0,0,1,1000,0,5000,2,'$expirationRaceUtc',1);
"@
Invoke-RootSql "gameserverportfolio-game-db-primary" $gameSeed $rootPassword
Invoke-RootSql "gameserverportfolio-auction-db-primary" $auctionSeed $rootPassword

$userIds = @(4101,4102,4103,4104,4200)
$tickets = @()
foreach ($userId in $userIds)
{
    $ticket = "auction-concurrency-$userId-" + [guid]::NewGuid().ToString("N")
    $tickets += $ticket
    docker exec refactoringserver-chat-redis redis-cli SET "chat:active-login:$userId" "1" | Out-Null
    docker exec refactoringserver-chat-redis redis-cli SETEX "auction:ticket:$ticket" 60 "$userId`:1" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Failed to seed AuctionAuth ticket for $userId." }
}

$testDirectory = Join-Path $repositoryRoot ("Out\AuctionConcurrencyTest\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$serverStdout = Join-Path $testDirectory "server.stdout.log"
$serverStderr = Join-Path $testDirectory "server.stderr.log"
$clientStdout = Join-Path $testDirectory "client.stdout.log"
$clientStderr = Join-Path $testDirectory "client.stderr.log"
$serverProcess = $null

try
{
    $serverProcess = Start-Process -FilePath $serverExecutable `
        -ArgumentList @("--port", "$Port", "--run-seconds", "25", "--database-enabled", "--redis-auth-enabled", "--expiration-poll-ms", "10") `
        -WorkingDirectory $testDirectory -RedirectStandardOutput $serverStdout `
        -RedirectStandardError $serverStderr -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1

    $clientArguments = @("--port", "$Port", "--concurrency-test", "--expiration-race-at-unix-ms", "$expirationRaceUnixMs")
    foreach ($ticket in $tickets) { $clientArguments += @("--concurrency-ticket", $ticket) }
    & $clientExecutable @clientArguments 1> $clientStdout 2> $clientStderr
    $clientExitCode = $LASTEXITCODE
    $clientOutput = Get-Content -LiteralPath $clientStdout -Raw
    if ($clientExitCode -ne 0 -or $clientOutput -notmatch "AUCTION_CONCURRENCY_CLIENT_SUCCESS")
    {
        $clientError = Get-Content -LiteralPath $clientStderr -Raw -ErrorAction SilentlyContinue
        throw "Concurrency client failed: $clientError"
    }

    for ($attempt = 0; $attempt -lt 100; ++$attempt)
    {
        $terminalCount = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot -Nse `
            "SELECT COUNT(*) FROM auctiondb.auction_listings WHERE listing_id IN (99100004,99100005,99100006) AND state IN (7,8,9)"
        if (($terminalCount | Out-String).Trim() -eq "3") { break }
        Start-Sleep -Milliseconds 50
    }
    if (($terminalCount | Out-String).Trim() -ne "3") { throw "Expiration races did not reach terminal states." }

    $bidRaceState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot -Nse `
        "SELECT CONCAT(l.state,':',l.version,':',COUNT(b.bid_id),':',SUM(b.state=2),':',SUM(b.state=3),':',SUM(b.bid_id=l.highest_bid_id),':',l.current_bid_price) FROM auctiondb.auction_listings l JOIN auctiondb.auction_bids b ON b.listing_id=l.listing_id WHERE l.listing_id=99100001 GROUP BY l.listing_id"
    $bidBuyoutState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot -Nse `
        "SELECT CONCAT(l.state,':',COUNT(b.bid_id),':',COALESCE(SUM(b.state=2),0),':',COALESCE(SUM(b.bid_id=l.highest_bid_id),0),':',l.version) FROM auctiondb.auction_listings l LEFT JOIN auctiondb.auction_bids b ON b.listing_id=l.listing_id WHERE l.listing_id=99100002 GROUP BY l.listing_id"
    $buyoutCancelState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot -Nse `
        "SELECT CONCAT(state,':',version) FROM auctiondb.auction_listings WHERE listing_id=99100003"
    $expirationRaceState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot -Nse `
        "SELECT GROUP_CONCAT(CONCAT(listing_id,':',state,':',version,':',COALESCE(sale_type,0),':',COALESCE(final_buyer_user_id,0),':',COALESCE(final_price,0)) ORDER BY listing_id) FROM auctiondb.auction_listings WHERE listing_id IN (99100004,99100005,99100006)"
    $expirationItemState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary mysql -uroot -Nse `
        "SELECT GROUP_CONCAT(CONCAT(item_instance_id,':',row_count) ORDER BY item_instance_id) FROM (SELECT item_instance_id,COUNT(*) row_count FROM gamedb.mail_attachments WHERE item_instance_id IN (9999100004,9999100005,9999100006) GROUP BY item_instance_id) x"
    $expirationBidState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot -Nse `
        "SELECT CONCAT((SELECT state FROM auctiondb.auction_listings WHERE listing_id=99100004),':',(SELECT COUNT(*) FROM auctiondb.auction_bids WHERE listing_id=99100004),':',(SELECT COALESCE(SUM(state=6),0) FROM auctiondb.auction_bids WHERE listing_id=99100004))"
    $conservationState = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary mysql -uroot -Nse `
        "SELECT (SELECT SUM(amount) FROM gamedb.player_currencies WHERE user_id IN (4101,4102,4103,4104,4200))+(SELECT COALESCE(SUM(currency_amount),0) FROM gamedb.mail_attachments)"
    $heldBidTotal = docker exec -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot -Nse `
        "SELECT COALESCE(SUM(bid_amount),0) FROM auctiondb.auction_bids WHERE listing_id IN (99100001,99100002,99100003) AND state IN (2,3)"
    $conservedTotal = [uint64](($conservationState | Out-String).Trim()) + [uint64](($heldBidTotal | Out-String).Trim())

    $bidRaceValue = ($bidRaceState | Out-String).Trim()
    $bidBuyoutValue = ($bidBuyoutState | Out-String).Trim()
    $buyoutCancelValue = ($buyoutCancelState | Out-String).Trim()
    $expirationRaceValue = ($expirationRaceState | Out-String).Trim()
    $expirationItemValue = ($expirationItemState | Out-String).Trim()
    $expirationBidValue = ($expirationBidState | Out-String).Trim()
    $expirationStatesValid = $expirationRaceValue -match '^99100004:(7:5:1:4103:1500|9:3:0:0:0),99100005:(7:3:2:4104:5000|9:3:0:0:0),99100006:(8:3:0:0:0|9:3:0:0:0)$'
    $expirationBidValid = $expirationBidValue -in @("7:1:1","9:0:0")
    if ($bidRaceValue -ne "2:5:2:1:1:1:2000" -or
        $bidBuyoutValue -notin @("2:1:1:1:3","7:0:0:0:3") -or
        $buyoutCancelValue -notin @("7:3","8:3") -or
        -not $expirationStatesValid -or -not $expirationBidValid -or
        $expirationItemValue -ne "9999100004:1,9999100005:1,9999100006:1" -or
        $conservedTotal -ne 80000)
    {
        throw "Concurrency DB invariant failed. bidRace=$bidRaceValue bidBuyout=$bidBuyoutValue buyoutCancel=$buyoutCancelValue expiration=$expirationRaceValue expirationBid=$expirationBidValue expirationItems=$expirationItemValue conserved=$conservedTotal"
    }

    Write-Host $clientOutput.Trim()
    Write-Host "BID_RACE_STATE listing=ACTIVE version=5 bids=2 highest=1 outbidClaimable=1 highestReferenceMatches=1 currentBid=2000"
    Write-Host "BID_BUYOUT_RACE_STATE value=$bidBuyoutValue exactlyOneWinner=1"
    Write-Host "BUYOUT_CANCEL_RACE_STATE value=$buyoutCancelValue exactlyOneWinner=1"
    Write-Host "EXPIRATION_BOUNDARY_RACE_STATE value=$expirationRaceValue bid=$expirationBidValue uniqueItemMails=$expirationItemValue"
    Write-Host "CURRENCY_CONSERVATION_STATE initial=80000 walletsPlusMailPlusHeldBids=$conservedTotal"
    Write-Host "Logs: $testDirectory"
}
finally
{
    if ($serverProcess -ne $null -and -not $serverProcess.HasExited)
    {
        Stop-Process -Id $serverProcess.Id -Force
    }
    try
    {
        "USE auctiondb; DELETE FROM auction_bids WHERE listing_id BETWEEN 99100001 AND 99100006; DELETE FROM auction_listings WHERE listing_id BETWEEN 99100001 AND 99100006;" |
            docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot
        "USE gamedb; DELETE FROM player_currencies WHERE user_id IN (4101,4102,4103,4104,4200);" |
            docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary mysql -uroot
    }
    catch
    {
        Write-Warning "Concurrency test data cleanup failed: $_"
    }
}

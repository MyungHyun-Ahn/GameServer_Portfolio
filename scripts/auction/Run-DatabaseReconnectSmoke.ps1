param(
    [int]$Port = 19118,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $scriptDirectory)
$envFile = Join-Path $repositoryRoot ".env"
$serverExecutable = Join-Path $repositoryRoot "Out\AuctionHouseServer\Debug\AuctionHouseServer.exe"
$clientExecutable = Join-Path $repositoryRoot "Out\AuctionDummyClient\Debug\AuctionDummyClient.exe"
$primaryContainer = "gameserverportfolio-auction-db-primary"
$replicaContainers = @(
    "gameserverportfolio-auction-db-replica",
    "gameserverportfolio-auction-db-replica-2"
)

function Get-DotEnvValue([string]$Name)
{
    $line = Get-Content -LiteralPath $envFile |
        Where-Object { $_ -match "^$([regex]::Escape($Name))=" } | Select-Object -Last 1
    if ([string]::IsNullOrWhiteSpace($line)) { throw "Missing $Name in $envFile" }
    return $line.Substring($line.IndexOf('=') + 1).Trim().Trim('"').Trim("'")
}

function Wait-ContainerHealthy([string]$Container)
{
    for ($attempt = 0; $attempt -lt 90; ++$attempt)
    {
        $state = docker inspect --format "{{.State.Health.Status}}" $Container 2>$null
        if (($state | Out-String).Trim() -eq "healthy") { return }
        Start-Sleep -Seconds 1
    }
    throw "$Container did not become healthy."
}

function Wait-OutputMarker([string]$Path, [string]$Marker, $Process, [int]$TimeoutSeconds = 15)
{
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline)
    {
        $text = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
        if ($text -match [regex]::Escape($Marker)) { return }
        if ($Process.HasExited) { throw "Client exited before marker '$Marker'." }
        Start-Sleep -Milliseconds 50
    }
    throw "Timed out waiting for marker '$Marker'."
}

$rootPassword = Get-DotEnvValue "MYSQL_ROOT_PASSWORD"
$env:MYSQL_PASSWORD = Get-DotEnvValue "MYSQL_PASSWORD"
& (Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1")
$loginCompose = Join-Path $repositoryRoot "Infra\docker-compose.login-platform.yaml"
docker compose --env-file $envFile -f $loginCompose up -d chat-redis
if ($LASTEXITCODE -ne 0) { throw "Failed to start Redis." }
Wait-ContainerHealthy "refactoringserver-chat-redis"

if (-not $SkipBuild)
{
    $env:TEMP = "E:\Procademy\build-temp"
    $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Path $env:TEMP -Force | Out-Null
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $visualStudioPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    $msbuild = Join-Path $visualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
    foreach ($project in @(
        (Join-Path $repositoryRoot "Auction\AuctionHouseServer\AuctionHouseServer.vcxproj"),
        (Join-Path $repositoryRoot "Auction\AuctionDummyClient\AuctionDummyClient.vcxproj")))
    {
        & $msbuild $project /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $project" }
    }
}

$gameSeed = @"
USE gamedb;
DELETE FROM player_currencies WHERE user_id=5101 AND currency_id=1;
INSERT INTO player_currencies(user_id,currency_id,amount,version) VALUES(5101,1,10000,1);
"@
$auctionSeed = @"
USE auctiondb;
DELETE FROM auction_bids WHERE listing_id=99200001;
DELETE FROM auction_listings WHERE listing_id=99200001;
INSERT INTO auction_listings
 (listing_id,seller_user_id,seller_login_id,item_instance_id,item_data_id,item_category,quantity,item_data,search_name,
 search_grade,search_enhancement_level,search_str,search_dex,search_int,search_luk,
 currency_id,start_price,current_bid_price,buyout_price,state,expires_at,version)
VALUES(99200001,5200,'reconnect-seller',9999200001,1001,1,1,JSON_OBJECT('str',10),'Reconnect Sword',1,0,10,0,0,0,
       1,1000,0,5000,2,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 1 DAY),1);
"@
$gameSeed | docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary mysql -uroot
$auctionSeed | docker exec -i -e "MYSQL_PWD=$rootPassword" $primaryContainer mysql -uroot
if ($LASTEXITCODE -ne 0) { throw "Reconnect test seed failed." }
foreach ($replicaContainer in $replicaContainers)
{
    for ($attempt = 0; $attempt -lt 50; ++$attempt)
    {
        $replicaListing = docker exec -e "MYSQL_PWD=$rootPassword" $replicaContainer mysql -uroot -Nse `
            "SELECT COUNT(*) FROM auctiondb.auction_listings WHERE listing_id=99200001"
        if (($replicaListing | Out-String).Trim() -eq "1") { break }
        Start-Sleep -Milliseconds 100
    }
    if (($replicaListing | Out-String).Trim() -ne "1") { throw "Reconnect seed did not replicate to $replicaContainer." }
}

$ticket = "auction-reconnect-" + [guid]::NewGuid().ToString("N")
docker exec refactoringserver-chat-redis redis-cli SET "chat:active-login:5101" "1" | Out-Null
docker exec refactoringserver-chat-redis redis-cli SETEX "auction:ticket:$ticket" 60 "5101:1" | Out-Null

$testDirectory = Join-Path $repositoryRoot ("Out\AuctionDatabaseReconnectTest\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$serverStdout = Join-Path $testDirectory "server.stdout.log"
$serverStderr = Join-Path $testDirectory "server.stderr.log"
$clientStdout = Join-Path $testDirectory "client.stdout.log"
$clientStderr = Join-Path $testDirectory "client.stderr.log"
$serverProcess = $null
$clientProcess = $null

try
{
    $serverProcess = Start-Process -FilePath $serverExecutable `
        -ArgumentList @("--port", "$Port", "--run-seconds", "70", "--database-enabled", "--redis-auth-enabled", "--replica-reconnect-cooldown-ms", "100") `
        -WorkingDirectory $testDirectory -RedirectStandardOutput $serverStdout `
        -RedirectStandardError $serverStderr -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1
    $clientProcess = Start-Process -FilePath $clientExecutable `
        -ArgumentList @("--port", "$Port", "--reconnect-test", "--ticket", $ticket) `
        -WorkingDirectory $testDirectory -RedirectStandardOutput $clientStdout `
        -RedirectStandardError $clientStderr -WindowStyle Hidden -PassThru

    Wait-OutputMarker $clientStdout "RECONNECT_FIRST_REPLICA_STOP_READY" $clientProcess
    docker stop --timeout 0 $replicaContainers[0] | Out-Null
    Wait-OutputMarker $clientStdout "RECONNECT_SECOND_REPLICA_STOP_READY" $clientProcess
    docker stop --timeout 0 $replicaContainers[1] | Out-Null
    Wait-OutputMarker $clientStdout "RECONNECT_ALL_REPLICAS_FALLBACK_SUCCESS" $clientProcess
    foreach ($replicaContainer in $replicaContainers)
    {
        docker start $replicaContainer | Out-Null
        Wait-ContainerHealthy $replicaContainer
    }

    Wait-OutputMarker $clientStdout "RECONNECT_MASTER_STOP_READY" $clientProcess
    docker stop --time 0 $primaryContainer | Out-Null
    Wait-OutputMarker $clientStdout "RECONNECT_MASTER_FAILURE_OBSERVED" $clientProcess
    docker start $primaryContainer | Out-Null
    Wait-ContainerHealthy $primaryContainer

    if (-not $clientProcess.WaitForExit(15000)) { throw "Reconnect client did not finish." }
    $clientProcess.WaitForExit()
    $clientProcess.Refresh()
    $clientOutput = Get-Content -LiteralPath $clientStdout -Raw
    if ($clientOutput -notmatch "AUCTION_DB_RECONNECT_TEST_SUCCESS")
    {
        $clientError = Get-Content -LiteralPath $clientStderr -Raw -ErrorAction SilentlyContinue
        throw "Reconnect client failed: $clientError"
    }
    $serverOutput = Get-Content -LiteralPath $serverStdout -Raw
    $detailSources = [regex]::Matches($serverOutput,"ListingDetail completed\.[^\r\n]*source=(replica|primary)") |
        ForEach-Object { $_.Groups[1].Value }
    if (($detailSources -join ',') -ne "replica,replica,primary,replica")
    {
        throw "Replica fallback/reconnect source sequence invalid: $($detailSources -join ',')"
    }
    $bidState = docker exec -e "MYSQL_PWD=$rootPassword" $primaryContainer mysql -uroot -Nse `
        "SELECT CONCAT(l.state,':',l.version,':',b.state,':',b.bid_amount) FROM auctiondb.auction_listings l JOIN auctiondb.auction_bids b ON b.bid_id=l.highest_bid_id WHERE l.listing_id=99200001"
    if (($bidState | Out-String).Trim() -ne "2:3:2:1500") { throw "Recovered master write state invalid: $bidState" }

    Write-Host $clientOutput.Trim()
    Write-Host "REPLICA_RECOVERY_STATE sources=replica,replica,primary,replica cooldownMs=100"
    Write-Host "MASTER_RECOVERY_STATE failedRequest=DATABASE_UNAVAILABLE nextRequest=SUCCESS bidState=2:3:2:1500"
    Write-Host "Logs: $testDirectory"
}
finally
{
    foreach ($container in @($primaryContainer) + $replicaContainers)
    {
        $running = docker inspect --format "{{.State.Running}}" $container 2>$null
        if (($running | Out-String).Trim() -ne "true") { docker start $container | Out-Null }
    }
    try
    {
        Wait-ContainerHealthy $primaryContainer
        "USE auctiondb; DELETE FROM auction_bids WHERE listing_id=99200001; DELETE FROM auction_listings WHERE listing_id=99200001;" |
            docker exec -i -e "MYSQL_PWD=$rootPassword" $primaryContainer mysql -uroot
        "USE gamedb; DELETE FROM player_currencies WHERE user_id=5101 AND currency_id=1;" |
            docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary mysql -uroot
    }
    catch
    {
        Write-Warning "Reconnect test data cleanup failed: $_"
    }
    if ($clientProcess -ne $null -and -not $clientProcess.HasExited) { Stop-Process -Id $clientProcess.Id -Force }
    if ($serverProcess -ne $null -and -not $serverProcess.HasExited) { Stop-Process -Id $serverProcess.Id -Force }
}

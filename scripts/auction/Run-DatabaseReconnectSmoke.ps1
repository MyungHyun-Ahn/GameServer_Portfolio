param(
    [int]$Port = 19118,
    [int]$CachePort = 19119,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $scriptDirectory)
$envFile = Join-Path $repositoryRoot ".env"
$serverExecutable = Join-Path $repositoryRoot "Out\AuctionHouseServer\Debug\AuctionHouseServer.exe"
$cacheExecutable = Join-Path $repositoryRoot "Out\CacheServer\Debug\CacheServer.exe"
$clientExecutable = Join-Path $repositoryRoot "Out\AuctionDummyClient\Debug\AuctionDummyClient.exe"
$cacheConfigTemplate = Join-Path $repositoryRoot "Config\Server\CacheServer.yaml"
$serverConfigTemplate = Join-Path $repositoryRoot "Config\Server\AuctionHouseServer.yaml"
. (Join-Path $repositoryRoot "scripts\common\ServerConfig.ps1")
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

function Wait-ListeningPort(
    [int]$ListenPort,
    $Process,
    [string]$Name,
    [string]$StandardErrorPath,
    [int]$TimeoutSeconds = 15)
{
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline)
    {
        if ($Process.HasExited)
        {
            $errorOutput = Get-Content -LiteralPath $StandardErrorPath -Raw -ErrorAction SilentlyContinue
            throw "$Name exited before entering Listen state. error=$errorOutput"
        }
        if ($null -ne (Get-NetTCPConnection -State Listen -LocalPort $ListenPort -ErrorAction SilentlyContinue))
        {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "$Name did not enter Listen state on port $ListenPort."
}

function Wait-OutboundConnection(
    [int]$RemotePort,
    $Process,
    [string]$Name,
    [string]$StandardErrorPath,
    [int]$TimeoutSeconds = 15)
{
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline)
    {
        if ($Process.HasExited)
        {
            $errorOutput = Get-Content -LiteralPath $StandardErrorPath -Raw -ErrorAction SilentlyContinue
            throw "$Name exited before connecting to CacheServer. error=$errorOutput"
        }
        $connection = Get-NetTCPConnection -State Established -RemotePort $RemotePort -ErrorAction SilentlyContinue |
            Where-Object { $_.OwningProcess -eq $Process.Id } |
            Select-Object -First 1
        if ($null -ne $connection)
        {
            # Give the RPC Hello exchange a short interval after TCP establishment.
            Start-Sleep -Milliseconds 500
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "$Name did not establish its CacheServer connection on port $RemotePort."
}

if ($Port -le 0 -or $Port -gt 65535 -or $CachePort -le 0 -or $CachePort -gt 65535)
{
    throw "Port and CachePort must be in range 1..65535."
}
if ($Port -eq $CachePort)
{
    throw "Port and CachePort must be different."
}

$rootPassword = Get-DotEnvValue "MYSQL_ROOT_PASSWORD"
$appPassword = Get-DotEnvValue "MYSQL_PASSWORD"
$env:MYSQL_PASSWORD = $appPassword
& (Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1")
$loginCompose = Join-Path $repositoryRoot "Infra\docker-compose.login-platform.yaml"
docker compose --env-file $envFile -f $loginCompose up -d chat-redis
if ($LASTEXITCODE -ne 0) { throw "Failed to start Redis." }
Wait-ContainerHealthy "gameserverportfolio-chat-redis"

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
        foreach ($project in @(
            (Join-Path $repositoryRoot "Cache\CacheServer\CacheServer.vcxproj"),
            (Join-Path $repositoryRoot "Auction\AuctionHouseServer\AuctionHouseServer.vcxproj"),
            (Join-Path $repositoryRoot "Auction\AuctionDummyClient\AuctionDummyClient.vcxproj")))
        {
            & $msbuild $project /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
            if ($LASTEXITCODE -ne 0) { throw "Build failed: $project" }
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
docker exec gameserverportfolio-chat-redis redis-cli SET "chat:active-login:5101" "1" | Out-Null
docker exec gameserverportfolio-chat-redis redis-cli SETEX "auction:ticket:$ticket" 60 "5101:1" | Out-Null

$testDirectory = Join-Path $repositoryRoot ("Out\AuctionDatabaseReconnectTest\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$serverStdout = Join-Path $testDirectory "server.stdout.log"
$serverStderr = Join-Path $testDirectory "server.stderr.log"
$cacheStdout = Join-Path $testDirectory "cache.stdout.log"
$cacheStderr = Join-Path $testDirectory "cache.stderr.log"
$clientStdout = Join-Path $testDirectory "client.stdout.log"
$clientStderr = Join-Path $testDirectory "client.stderr.log"
$cacheConfig = Join-Path $testDirectory "CacheServer.yaml"
New-ServerConfigFile -TemplatePath $cacheConfigTemplate -DestinationPath $cacheConfig -Overrides @{
    "CacheServer.Port" = $CachePort
    "CacheServer.PlayerCacheShardCount" = 4
    "CacheServer.DatabaseEnabled" = $true
    "GameDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
    "CacheServer.ReplicaReconnectCooldownMilliseconds" = 100
    "CacheServer.LogOutputDirectory" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "cache-logs"))
    "Debug.RunSeconds" = 80
} | Out-Null
$serverConfig = Join-Path $testDirectory "AuctionHouseServer.yaml"
New-ServerConfigFile -TemplatePath $serverConfigTemplate -DestinationPath $serverConfig -Overrides @{
    "AuctionHouseServer.Port" = $Port
    "AuctionHouseServer.RunSeconds" = 70
    "Logging.OutputDirectory" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "auction-logs"))
    "Diagnostics.TimingCsvPath" = (ConvertTo-ServerConfigYamlString (Join-Path $testDirectory "auction_timing.csv"))
    "Authentication.Enabled" = $true
    "CacheRpc.Port" = $CachePort
    "CacheRpc.ReconnectMilliseconds" = 100
    "AuctionDatabase.Enabled" = $true
    "AuctionDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
    "AuctionDatabase.ReplicaReconnectCooldownMilliseconds" = 100
} | Out-Null
$serverProcess = $null
$cacheProcess = $null
$clientProcess = $null

try
{
    foreach ($executable in @($cacheExecutable, $serverExecutable, $clientExecutable))
    {
        if (-not (Test-Path -LiteralPath $executable))
        {
            throw "Executable is missing: $executable"
        }
    }

    $cacheProcess = Start-Process -FilePath $cacheExecutable `
        -ArgumentList @("--config", $cacheConfig) `
        -WorkingDirectory $testDirectory -RedirectStandardOutput $cacheStdout `
        -RedirectStandardError $cacheStderr -WindowStyle Hidden -PassThru
    Wait-ListeningPort -ListenPort $CachePort -Process $cacheProcess -Name "CacheServer" -StandardErrorPath $cacheStderr

    $serverProcess = Start-Process -FilePath $serverExecutable `
        -ArgumentList @("--config", $serverConfig) `
        -WorkingDirectory $testDirectory -RedirectStandardOutput $serverStdout `
        -RedirectStandardError $serverStderr -WindowStyle Hidden -PassThru
    Wait-ListeningPort -ListenPort $Port -Process $serverProcess -Name "AuctionHouseServer" -StandardErrorPath $serverStderr
    Wait-OutboundConnection -RemotePort $CachePort -Process $serverProcess -Name "AuctionHouseServer" -StandardErrorPath $serverStderr

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
    Write-Host "CACHE_RPC_RECOVERY_STATE ready=1 cachePort=$CachePort"
    Write-Host "REPLICA_RECOVERY_STATE sources=replica,replica,primary,replica cooldownMs=100"
    Write-Host "MASTER_RECOVERY_STATE failedRequest=DATABASE_UNAVAILABLE nextRequest=SUCCESS bidState=2:3:2:1500"
    Write-Host "Logs: $testDirectory"
}
finally
{
    if ($clientProcess -ne $null -and -not $clientProcess.HasExited) { Stop-Process -Id $clientProcess.Id -Force }
    if ($serverProcess -ne $null -and -not $serverProcess.HasExited) { Stop-Process -Id $serverProcess.Id -Force }
    if ($cacheProcess -ne $null -and -not $cacheProcess.HasExited) { Stop-Process -Id $cacheProcess.Id -Force }

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
}

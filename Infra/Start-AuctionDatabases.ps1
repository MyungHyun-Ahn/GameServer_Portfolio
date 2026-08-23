param(
    [switch]$Recreate
)

$ErrorActionPreference = "Stop"

$infraDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent $infraDirectory
$composeFile = Join-Path $infraDirectory "docker-compose.auction-databases.yaml"
$envFile = Join-Path $repositoryRoot ".env"

function Get-DotEnvValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $line = Get-Content -LiteralPath $envFile |
        Where-Object { $_ -match "^$([regex]::Escape($Name))=" } |
        Select-Object -Last 1
    if ([string]::IsNullOrWhiteSpace($line)) {
        throw "Missing $Name in $envFile"
    }

    $value = $line.Substring($line.IndexOf('=') + 1).Trim()
    if ($value.Length -ge 2 -and
        (($value.StartsWith('"') -and $value.EndsWith('"')) -or
         ($value.StartsWith("'") -and $value.EndsWith("'")))) {
        $value = $value.Substring(1, $value.Length - 2)
    }

    return $value
}

function Wait-ContainerHealthy {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ContainerName,

        [int]$TimeoutSeconds = 120
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $status = docker inspect --format "{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}" $ContainerName 2>$null
        if ($LASTEXITCODE -eq 0 -and ($status | Out-String).Trim() -eq "healthy") {
            return
        }
        Start-Sleep -Seconds 2
    }

    throw "Container '$ContainerName' did not become healthy within $TimeoutSeconds seconds."
}

function Initialize-Replica {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ContainerName,

        [Parameter(Mandatory = $true)]
        [string]$SourceHost,

        [Parameter(Mandatory = $true)]
        [string]$RootPassword,

        [Parameter(Mandatory = $true)]
        [string]$ReplicationPassword
    )

    $sql = @"
STOP REPLICA;
RESET REPLICA ALL;
CHANGE REPLICATION SOURCE TO
    SOURCE_HOST='$SourceHost',
    SOURCE_PORT=3306,
    SOURCE_USER='auction_replicator',
    SOURCE_PASSWORD='$ReplicationPassword',
    SOURCE_AUTO_POSITION=1,
    GET_SOURCE_PUBLIC_KEY=1;
START REPLICA;
SET GLOBAL read_only = ON;
SET GLOBAL super_read_only = ON;
"@

    $sql | docker exec -i -e "MYSQL_PWD=$RootPassword" $ContainerName mysql -uroot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize replication for $ContainerName"
    }
}

if (-not (Test-Path -LiteralPath $envFile)) {
    throw "Create $envFile from .env.example before starting the databases."
}

$rootPassword = Get-DotEnvValue -Name "MYSQL_ROOT_PASSWORD"
$appPassword = Get-DotEnvValue -Name "MYSQL_PASSWORD"

if ($Recreate) {
    docker compose --env-file $envFile -f $composeFile down --volumes
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to remove the existing auction database environment."
    }
}

docker compose --env-file $envFile -f $composeFile up -d
if ($LASTEXITCODE -ne 0) {
    throw "Failed to start the auction database environment."
}

$containers = @(
    "gameserverportfolio-game-db-primary",
    "gameserverportfolio-game-db-replica",
    "gameserverportfolio-game-db-replica-2",
    "gameserverportfolio-auction-db-primary",
    "gameserverportfolio-auction-db-replica",
    "gameserverportfolio-auction-db-replica-2"
)
foreach ($container in $containers) {
    Wait-ContainerHealthy -ContainerName $container
}

$gameMigration = Join-Path $repositoryRoot "Auction\Database\GameDB\003_migrate_item_data_id.sql"
$gameInventoryProcedures = Join-Path $repositoryRoot "Auction\Database\GameDB\004_inventory_procedures.sql"
$gameAuctionRegistration = Join-Path $repositoryRoot "Auction\Database\GameDB\005_inventory_auction_registration.sql"
$gameMailProcedures = Join-Path $repositoryRoot "Auction\Database\GameDB\006_mail_procedures.sql"
$auctionMigration = Join-Path $repositoryRoot "Auction\Database\AuctionDB\003_migrate_item_search_columns.sql"
$auctionSearchSortingMigration = Join-Path $repositoryRoot "Auction\Database\AuctionDB\008_search_sorting.sql"
$auctionSearchSortIndexMigration = Join-Path $repositoryRoot "Auction\Database\AuctionDB\009_migrate_search_sort_index_directions.sql"
$auctionProcedures = Join-Path $repositoryRoot "Auction\Database\AuctionDB\002_listing_bid_procedures.sql"

foreach ($sqlFile in @($gameMigration, $gameInventoryProcedures, $gameAuctionRegistration, $gameMailProcedures)) {
    Get-Content -LiteralPath $sqlFile -Raw |
        docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary mysql -uroot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to apply GameDB SQL: $sqlFile"
    }
}

foreach ($sqlFile in @($auctionMigration, $auctionSearchSortingMigration, $auctionSearchSortIndexMigration, $auctionProcedures)) {
    Get-Content -LiteralPath $sqlFile -Raw |
        docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-auction-db-primary mysql -uroot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to apply AuctionDB SQL: $sqlFile"
    }
}

Initialize-Replica `
    -ContainerName "gameserverportfolio-game-db-replica" `
    -SourceHost "game-db-primary" `
    -RootPassword $rootPassword `
    -ReplicationPassword $appPassword
Initialize-Replica `
    -ContainerName "gameserverportfolio-game-db-replica-2" `
    -SourceHost "game-db-primary" `
    -RootPassword $rootPassword `
    -ReplicationPassword $appPassword
Initialize-Replica `
    -ContainerName "gameserverportfolio-auction-db-replica" `
    -SourceHost "auction-db-primary" `
    -RootPassword $rootPassword `
    -ReplicationPassword $appPassword
Initialize-Replica `
    -ContainerName "gameserverportfolio-auction-db-replica-2" `
    -SourceHost "auction-db-primary" `
    -RootPassword $rootPassword `
    -ReplicationPassword $appPassword

Start-Sleep -Seconds 2

foreach ($container in @(
    "gameserverportfolio-game-db-replica",
    "gameserverportfolio-game-db-replica-2",
    "gameserverportfolio-auction-db-replica",
    "gameserverportfolio-auction-db-replica-2")) {
    $status = docker exec -e "MYSQL_PWD=$rootPassword" $container mysql -uroot -Nse `
        "SELECT CONCAT(SERVICE_STATE, ':', LAST_ERROR_NUMBER) FROM performance_schema.replication_applier_status_by_coordinator;"
    if ($LASTEXITCODE -ne 0 -or ($status | Out-String).Trim() -notmatch "ON:0") {
        throw "Replication is not healthy for $container. status=$status"
    }
}

Write-Host "Auction database environment is ready."
Write-Host "  GameDB Primary   : 127.0.0.1:3310"
Write-Host "  GameDB Replica   : 127.0.0.1:3311"
Write-Host "  GameDB Replica 2 : 127.0.0.1:3312"
Write-Host "  AuctionDB Primary: 127.0.0.1:3320"
Write-Host "  AuctionDB Replica: 127.0.0.1:3321"
Write-Host "  AuctionDB Replica2: 127.0.0.1:3322"

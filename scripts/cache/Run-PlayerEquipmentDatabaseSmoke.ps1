param(
    [uint64]$UserId = 93000002,
    [switch]$SkipDatabaseStart
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$envFile = Join-Path $repositoryRoot ".env"
$databaseStartScript = Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1"
$procedureFile = Join-Path $repositoryRoot "Database\GameDB\011_player_equipment_procedures.sql"
$container = "gameserverportfolio-game-db-primary"
$firstItemInstanceId = $UserId * 10 + 1
$secondItemInstanceId = $UserId * 10 + 2

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

function Invoke-GameDbSql
{
    param(
        [Parameter(Mandatory = $true)][string]$Sql,
        [switch]$AllowFailure,
        [switch]$ContinueOnError
    )

    $previousErrorActionPreference = $ErrorActionPreference
    try
    {
        $ErrorActionPreference = "Continue"
        if ($ContinueOnError)
        {
            $output = $Sql |
                docker exec -i -e "MYSQL_PWD=$rootPassword" $container `
                    mysql -uroot --batch --raw --skip-column-names --force 2>&1
        }
        else
        {
            $output = $Sql |
                docker exec -i -e "MYSQL_PWD=$rootPassword" $container `
                    mysql -uroot --batch --raw --skip-column-names 2>&1
        }
        $exitCode = $LASTEXITCODE
    }
    finally
    {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    $outputText = ($output | Out-String).Trim()
    if (-not $AllowFailure -and $exitCode -ne 0)
    {
        throw "GameDB SQL failed. exit=$exitCode output=$outputText"
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $outputText }
}

function Assert-GameDbValue
{
    param(
        [Parameter(Mandatory = $true)][string]$Sql,
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $result = Invoke-GameDbSql -Sql $Sql
    if ($result.Output -ne $Expected)
    {
        throw "$Label mismatch. expected='$Expected' actual='$($result.Output)'"
    }
    Write-Host "[PASS] $Label = $Expected"
}

function Assert-GameDbFailure
{
    param(
        [Parameter(Mandatory = $true)][string]$Sql,
        [Parameter(Mandatory = $true)][string]$ExpectedError,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $transactionSql = "USE gamedb; START TRANSACTION; $Sql ROLLBACK;"
    $result = Invoke-GameDbSql -Sql $transactionSql -AllowFailure -ContinueOnError
    if ($result.Output -notmatch [regex]::Escape($ExpectedError))
    {
        throw "$Label did not fail with '$ExpectedError'. exit=$($result.ExitCode) output=$($result.Output)"
    }
    Write-Host "[PASS] $Label rejected with $ExpectedError"
}

if ($UserId -eq 0 -or $firstItemInstanceId -gt [uint64]::MaxValue -or $secondItemInstanceId -gt [uint64]::MaxValue)
{
    throw "UserId is outside the smoke-test range."
}
if (-not (Test-Path -LiteralPath $envFile))
{
    throw "Create $envFile from .env.example before running the smoke test."
}

$rootPassword = Get-DotEnvValue -Name "MYSQL_ROOT_PASSWORD"
$databaseReady = $false

try
{
    if (-not $SkipDatabaseStart)
    {
        & $databaseStartScript
        if ($LASTEXITCODE -ne 0)
        {
            throw "GameDB environment startup failed."
        }
    }
    $databaseReady = $true

    foreach ($attempt in 1..2)
    {
        Invoke-GameDbSql -Sql (Get-Content -LiteralPath $procedureFile -Raw) | Out-Null
    }
    Write-Host "[PASS] player equipment procedures are re-runnable"

    Invoke-GameDbSql -Sql @"
USE gamedb;
DELETE FROM inventory_items WHERE owner_user_id = $UserId;
INSERT INTO inventory_items
    (item_instance_id,owner_user_id,item_data_id,quantity,item_data,is_equipped,is_tradable,version)
VALUES
    ($firstItemInstanceId,$UserId,1001,1,JSON_OBJECT('str',3,'dex',0,'int',0,'luk',0),0,1,1),
    ($secondItemInstanceId,$UserId,1002,1,JSON_OBJECT('str',0,'dex',4,'int',0,'luk',0),1,1,5);
"@ | Out-Null

    Invoke-GameDbSql -Sql "USE gamedb; START TRANSACTION; CALL sp_gd_u_player_item_equip($UserId,$firstItemInstanceId,1,$secondItemInstanceId,5); COMMIT;" | Out-Null
    Assert-GameDbValue `
        -Label "same-slot atomic replacement" `
        -Expected "${firstItemInstanceId}:1:2|${secondItemInstanceId}:0:6" `
        -Sql "USE gamedb; SELECT GROUP_CONCAT(CONCAT(item_instance_id,':',is_equipped,':',version) ORDER BY item_instance_id SEPARATOR '|') FROM inventory_items WHERE owner_user_id=$UserId;"

    Assert-GameDbFailure `
        -Label "stale previous equipment version" `
        -ExpectedError "PREVIOUS_EQUIPMENT_CONFLICT" `
        -Sql "CALL sp_gd_u_player_item_equip($UserId,$secondItemInstanceId,6,$firstItemInstanceId,1);"
    Assert-GameDbValue `
        -Label "failed replacement rolls back both rows" `
        -Expected "${firstItemInstanceId}:1:2|${secondItemInstanceId}:0:6" `
        -Sql "USE gamedb; SELECT GROUP_CONCAT(CONCAT(item_instance_id,':',is_equipped,':',version) ORDER BY item_instance_id SEPARATOR '|') FROM inventory_items WHERE owner_user_id=$UserId;"

    Assert-GameDbFailure `
        -Label "stale target item version" `
        -ExpectedError "ITEM_VERSION_MISMATCH" `
        -Sql "CALL sp_gd_u_player_item_unequip($UserId,$firstItemInstanceId,1);"

    Invoke-GameDbSql -Sql "USE gamedb; START TRANSACTION; CALL sp_gd_u_player_item_unequip($UserId,$firstItemInstanceId,2); COMMIT;" | Out-Null
    Assert-GameDbValue `
        -Label "explicit unequip" `
        -Expected "0:3" `
        -Sql "USE gamedb; SELECT CONCAT(is_equipped,':',version) FROM inventory_items WHERE item_instance_id=$firstItemInstanceId;"

    Assert-GameDbFailure `
        -Label "already unequipped state" `
        -ExpectedError "EQUIPMENT_STATE_CONFLICT" `
        -Sql "CALL sp_gd_u_player_item_unequip($UserId,$firstItemInstanceId,3);"

    Write-Host "[PASS] Player equipment GameDB smoke completed."
}
finally
{
    if ($databaseReady)
    {
        Invoke-GameDbSql -Sql "USE gamedb; DELETE FROM inventory_items WHERE owner_user_id = $UserId;" -AllowFailure | Out-Null
    }
}

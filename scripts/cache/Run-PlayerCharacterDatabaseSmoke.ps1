param(
    [uint64]$UserId = 93000001,
    [switch]$SkipDatabaseStart
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$envFile = Join-Path $repositoryRoot ".env"
$databaseStartScript = Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1"
$migrationFile = Join-Path $repositoryRoot "Database\GameDB\009_migrate_player_characters.sql"
$procedureFile = Join-Path $repositoryRoot "Database\GameDB\010_player_character_procedures.sql"
$container = "gameserverportfolio-game-db-primary"

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
        [switch]$AllowFailure
    )

    $previousErrorActionPreference = $ErrorActionPreference
    try
    {
        $ErrorActionPreference = "Continue"
        $output = $Sql |
            docker exec -i -e "MYSQL_PWD=$rootPassword" $container `
                mysql -uroot --batch --raw --skip-column-names 2>&1
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

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = $outputText
    }
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

    $result = Invoke-GameDbSql -Sql $Sql -AllowFailure
    if ($result.ExitCode -eq 0 -or $result.Output -notmatch [regex]::Escape($ExpectedError))
    {
        throw "$Label did not fail with '$ExpectedError'. exit=$($result.ExitCode) output=$($result.Output)"
    }
    Write-Host "[PASS] $Label rejected with $ExpectedError"
}

if ($UserId -eq 0)
{
    throw "UserId must not be zero."
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

    # 같은 파일을 두 번 적용해 기존 DB 재기동 시에도 안전한지 확인한다.
    foreach ($attempt in 1..2)
    {
        Invoke-GameDbSql -Sql (Get-Content -LiteralPath $migrationFile -Raw) | Out-Null
        Invoke-GameDbSql -Sql (Get-Content -LiteralPath $procedureFile -Raw) | Out-Null
    }
    Write-Host "[PASS] player character migration/procedures are re-runnable"

    Invoke-GameDbSql -Sql "USE gamedb; DELETE FROM player_characters WHERE user_id = $UserId;" | Out-Null

    Assert-GameDbValue `
        -Label "gameplay columns have no DB defaults" `
        -Expected "0" `
        -Sql @"
SELECT COUNT(*)
  FROM information_schema.columns
 WHERE table_schema = 'gamedb'
   AND table_name = 'player_characters'
   AND column_name IN
       ('character_data_id', 'level', 'exp', 'stat_str', 'stat_dex',
        'stat_int', 'stat_luk', 'unspent_stat_points')
   AND column_default IS NOT NULL;
"@

    Assert-GameDbValue `
        -Label "player character foreign keys" `
        -Expected "0" `
        -Sql @"
SELECT COUNT(*)
  FROM information_schema.referential_constraints
 WHERE constraint_schema = 'gamedb'
   AND table_name = 'player_characters';
"@

    Invoke-GameDbSql -Sql @"
USE gamedb;
CALL sp_gd_c_player_character($UserId, 1001, 1, 0, 7, 8, 9, 10, 3);
"@ | Out-Null

    Assert-GameDbValue `
        -Label "initial character state" `
        -Expected "1001|1|0|7|8|9|10|3|1|1" `
        -Sql @"
USE gamedb;
SELECT CONCAT_WS('|', character_data_id, level, exp,
                 stat_str, stat_dex, stat_int, stat_luk,
                 unspent_stat_points, progress_version, stat_version)
  FROM player_characters
 WHERE user_id = $UserId;
"@

    Assert-GameDbFailure `
        -Label "one character per user" `
        -ExpectedError "PLAYER_CHARACTER_ALREADY_EXISTS" `
        -Sql "USE gamedb; CALL sp_gd_c_player_character($UserId, 1002, 1, 0, 1, 1, 1, 1, 0);"

    # 1 -> 3 다중 레벨 보상 합계는 CacheServer가 계산해 한 번에 전달한다.
    Invoke-GameDbSql -Sql "USE gamedb; CALL sp_gd_u_player_experience($UserId, 1, 3, 25, 11);" | Out-Null
    Assert-GameDbValue `
        -Label "multi-level progress and reward" `
        -Expected "3|25|14|2|2" `
        -Sql @"
USE gamedb;
SELECT CONCAT_WS('|', level, exp, unspent_stat_points, progress_version, stat_version)
  FROM player_characters
 WHERE user_id = $UserId;
"@

    # 동일 레벨의 양수 EXP 반영은 진행 Version만 올리고 스탯 Version은 유지한다.
    Invoke-GameDbSql -Sql "USE gamedb; CALL sp_gd_u_player_experience($UserId, 2, 3, 35, 0);" | Out-Null
    Assert-GameDbValue `
        -Label "same-level experience version isolation" `
        -Expected "3|35|14|3|2" `
        -Sql @"
USE gamedb;
SELECT CONCAT_WS('|', level, exp, unspent_stat_points, progress_version, stat_version)
  FROM player_characters
 WHERE user_id = $UserId;
"@

    Assert-GameDbFailure `
        -Label "stale progress version" `
        -ExpectedError "PLAYER_EXPERIENCE_CONFLICT" `
        -Sql "USE gamedb; CALL sp_gd_u_player_experience($UserId, 2, 4, 0, 5);"

    Invoke-GameDbSql -Sql "USE gamedb; CALL sp_gd_u_player_stat_allocation($UserId, 2, 2, 3, 1, 0);" | Out-Null
    Assert-GameDbValue `
        -Label "atomic stat allocation" `
        -Expected "9|11|10|10|8|3" `
        -Sql @"
USE gamedb;
SELECT CONCAT_WS('|', stat_str, stat_dex, stat_int, stat_luk,
                 unspent_stat_points, stat_version)
  FROM player_characters
 WHERE user_id = $UserId;
"@

    Assert-GameDbFailure `
        -Label "over-allocation" `
        -ExpectedError "PLAYER_STAT_ALLOCATION_CONFLICT" `
        -Sql "USE gamedb; CALL sp_gd_u_player_stat_allocation($UserId, 3, 9, 0, 0, 0);"

    Assert-GameDbFailure `
        -Label "stale stat version" `
        -ExpectedError "PLAYER_STAT_ALLOCATION_CONFLICT" `
        -Sql "USE gamedb; CALL sp_gd_u_player_stat_allocation($UserId, 2, 1, 0, 0, 0);"

    Assert-GameDbValue `
        -Label "failed mutations preserve state" `
        -Expected "3|35|9|11|10|10|8|3|3" `
        -Sql @"
USE gamedb;
SELECT CONCAT_WS('|', level, exp, stat_str, stat_dex, stat_int, stat_luk,
                 unspent_stat_points, progress_version, stat_version)
  FROM player_characters
 WHERE user_id = $UserId;
"@

    $readResult = Invoke-GameDbSql -Sql "USE gamedb; CALL sp_gd_r_player_character($UserId);"
    if ($readResult.Output -notmatch "(?m)^\d+\s+$UserId\s+1001\s+3\s+35\s+9\s+11\s+10\s+10\s+8\s+3\s+3\s+")
    {
        throw "sp_gd_r_player_character result mismatch. output=$($readResult.Output)"
    }
    Write-Host "[PASS] player character read procedure"

    $experienceJobs = foreach ($requestIndex in 1..2)
    {
        Start-Job -ScriptBlock {
            param($Container, $RootPassword, $TargetUserId)

            $ErrorActionPreference = "Continue"
            $sql = "USE gamedb; CALL sp_gd_u_player_experience($TargetUserId, 3, 3, 40, 0);"
            $output = $sql |
                docker exec -i -e "MYSQL_PWD=$RootPassword" $Container `
                    mysql -uroot --batch --raw --skip-column-names 2>&1
            [pscustomobject]@{
                ExitCode = $LASTEXITCODE
                Output = ($output | Out-String).Trim()
            }
        } -ArgumentList $container, $rootPassword, $UserId
    }

    try
    {
        $experienceResults = @($experienceJobs | Receive-Job -Wait)
        $successCount = @($experienceResults | Where-Object { $_.ExitCode -eq 0 }).Count
        $conflictCount = @(
            $experienceResults |
                Where-Object {
                    $_.ExitCode -ne 0 -and
                    $_.Output -match "PLAYER_EXPERIENCE_CONFLICT"
                }
        ).Count
        if ($successCount -ne 1 -or $conflictCount -ne 1)
        {
            $details = $experienceResults | ConvertTo-Json -Compress
            throw "Concurrent experience mismatch. success=$successCount conflict=$conflictCount details=$details"
        }
    }
    finally
    {
        $experienceJobs | Remove-Job -Force -ErrorAction SilentlyContinue
    }

    Assert-GameDbValue `
        -Label "concurrent experience commits once" `
        -Expected "3|40|8|4|3" `
        -Sql @"
USE gamedb;
SELECT CONCAT_WS('|', level, exp, unspent_stat_points, progress_version, stat_version)
  FROM player_characters
 WHERE user_id = $UserId;
"@

    $allocationJobs = foreach ($requestIndex in 1..2)
    {
        Start-Job -ScriptBlock {
            param($Container, $RootPassword, $TargetUserId)

            $ErrorActionPreference = "Continue"
            $sql = "USE gamedb; CALL sp_gd_u_player_stat_allocation($TargetUserId, 3, 1, 0, 0, 0);"
            $output = $sql |
                docker exec -i -e "MYSQL_PWD=$RootPassword" $Container `
                    mysql -uroot --batch --raw --skip-column-names 2>&1
            [pscustomobject]@{
                ExitCode = $LASTEXITCODE
                Output = ($output | Out-String).Trim()
            }
        } -ArgumentList $container, $rootPassword, $UserId
    }

    try
    {
        $allocationResults = @($allocationJobs | Receive-Job -Wait)
        $successCount = @($allocationResults | Where-Object { $_.ExitCode -eq 0 }).Count
        $conflictCount = @(
            $allocationResults |
                Where-Object {
                    $_.ExitCode -ne 0 -and
                    $_.Output -match "PLAYER_STAT_ALLOCATION_CONFLICT"
                }
        ).Count
        if ($successCount -ne 1 -or $conflictCount -ne 1)
        {
            $details = $allocationResults | ConvertTo-Json -Compress
            throw "Concurrent allocation mismatch. success=$successCount conflict=$conflictCount details=$details"
        }
    }
    finally
    {
        $allocationJobs | Remove-Job -Force -ErrorAction SilentlyContinue
    }

    Assert-GameDbValue `
        -Label "concurrent allocation commits once" `
        -Expected "10|7|4" `
        -Sql @"
USE gamedb;
SELECT CONCAT_WS('|', stat_str, unspent_stat_points, stat_version)
  FROM player_characters
 WHERE user_id = $UserId;
"@

    Write-Host "[PASS] Player character GameDB smoke completed."
}
finally
{
    if ($databaseReady)
    {
        Invoke-GameDbSql -Sql "USE gamedb; DELETE FROM player_characters WHERE user_id = $UserId;" -AllowFailure | Out-Null
    }
}

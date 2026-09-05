param(
    [int]$Port = 19103,
    [int]$ShardCount = 4,
    [uint64]$UserId = 92000001,
    [ValidateSet("iocp", "rio-direct", "rio-owner")]
    [string]$Backend = "iocp",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$SkipBuild,
    [switch]$SkipDatabaseStart
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$envFile = Join-Path $repositoryRoot ".env"
$databaseStartScript = Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1"
$serverProject = Join-Path $repositoryRoot "Cache\CacheServer\CacheServer.vcxproj"
$clientProject = Join-Path $repositoryRoot "Cache\CacheRpcPingClient\CacheRpcPingClient.vcxproj"
$serverExecutable = Join-Path $repositoryRoot "Out\CacheServer\$Configuration\CacheServer.exe"
$clientExecutable = Join-Path $repositoryRoot "Out\CacheRpcPingClient\$Configuration\CacheRpcPingClient.exe"
$cacheServerConfigTemplate = Join-Path $repositoryRoot "Config\Server\CacheServer.yaml"
$serverConfigHelper = Join-Path $scriptsRoot "common\ServerConfig.ps1"

if (-not (Test-Path -LiteralPath $serverConfigHelper -PathType Leaf))
{
    throw "Server config helper is missing: $serverConfigHelper"
}
. $serverConfigHelper

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

if ($Port -le 0 -or $Port -gt 65535)
{
    throw "Port must be in range 1..65535."
}
if ($ShardCount -le 0 -or $ShardCount -gt 64)
{
    throw "ShardCount must be in range 1..64."
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
$appPassword = Get-DotEnvValue -Name "MYSQL_PASSWORD"
$previousMySqlPassword = $env:MYSQL_PASSWORD
$serverProcess = $null
$seeded = $false

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

    if (-not $SkipBuild)
    {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        $msbuild = $null
        if (Test-Path -LiteralPath $vswhere)
        {
            $visualStudioPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
            if (-not [string]::IsNullOrWhiteSpace($visualStudioPath))
            {
                $msbuild = Join-Path $visualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
            }
        }
        if ($null -eq $msbuild -or -not (Test-Path -LiteralPath $msbuild))
        {
            $msbuildCommand = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
            if ($null -ne $msbuildCommand)
            {
                $msbuild = $msbuildCommand.Source
            }
        }
        if ($null -eq $msbuild -or -not (Test-Path -LiteralPath $msbuild))
        {
            throw "MSBuild.exe was not found."
        }

        foreach ($project in @($serverProject, $clientProject))
        {
            & $msbuild $project /t:Build /p:Configuration=$Configuration /p:Platform=x64 /m /nologo /v:minimal
            if ($LASTEXITCODE -ne 0)
            {
                throw "Build failed: $project"
            }
        }
    }

    if (-not (Test-Path -LiteralPath $serverExecutable) -or
        -not (Test-Path -LiteralPath $clientExecutable) -or
        -not (Test-Path -LiteralPath $cacheServerConfigTemplate -PathType Leaf))
    {
        throw "Player Cache Load smoke dependency is missing."
    }

    $seedSql = @"
USE gamedb;
DELETE FROM inventory_items WHERE owner_user_id = $UserId;
DELETE FROM player_currencies WHERE user_id = $UserId;
INSERT INTO player_currencies(user_id,currency_id,amount,version) VALUES
($UserId,1,123456,3),
($UserId,2,77,2);
INSERT INTO inventory_items(owner_user_id,item_data_id,quantity,item_data,is_equipped,is_tradable,version) VALUES
($UserId,1001,1,JSON_OBJECT('str',12,'dex',3,'int',0,'luk',1),1,1,4),
($UserId,2001,15,JSON_OBJECT(),0,1,2),
($UserId,3001,8,JSON_OBJECT(),0,0,5);
"@
    $seedSql | docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary mysql -uroot
    if ($LASTEXITCODE -ne 0)
    {
        throw "GameDB seed failed."
    }
    $seeded = $true

    $testRunId = "{0}_{1}_{2}" -f (Get-Date -Format "yyyyMMdd_HHmmss_fff"), $PID, ([guid]::NewGuid().ToString("N").Substring(0, 8))
    $testDirectory = Join-Path $repositoryRoot ("Out\PlayerCacheLoadTest\" + $testRunId)
    New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
    $serverStandardOutput = Join-Path $testDirectory "server.stdout.log"
    $serverStandardError = Join-Path $testDirectory "server.stderr.log"
    $clientStandardOutput = Join-Path $testDirectory "client.stdout.log"
    $clientStandardError = Join-Path $testDirectory "client.stderr.log"
    $invalidRouteStandardOutput = Join-Path $testDirectory "invalid-route.stdout.log"
    $invalidRouteStandardError = Join-Path $testDirectory "invalid-route.stderr.log"
    $serverConfigPath = Join-Path $testDirectory "cache.server.yaml"

    $cacheBackend = "Iocp"
    $rioSendDispatchMode = "Direct"
    switch ($Backend)
    {
        "rio-direct"
        {
            $cacheBackend = "Rio"
        }
        "rio-owner"
        {
            $cacheBackend = "Rio"
            $rioSendDispatchMode = "OwnerThread"
        }
    }

    New-ServerConfigFile `
        -TemplatePath $cacheServerConfigTemplate `
        -DestinationPath $serverConfigPath `
        -Overrides @{
            "CacheServer.Backend" = $cacheBackend
            "CacheServer.RioSendDispatchMode" = $rioSendDispatchMode
            "CacheServer.Port" = $Port
            "CacheServer.PlayerCacheShardCount" = $ShardCount
            "CacheServer.DatabaseEnabled" = $true
            "GameDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
            "Debug.RunSeconds" = 10
        } | Out-Null

    $env:MYSQL_PASSWORD = $appPassword
    $serverProcess = Start-Process `
        -FilePath $serverExecutable `
        -ArgumentList @("--config", $serverConfigPath) `
        -WorkingDirectory $testDirectory `
        -RedirectStandardOutput $serverStandardOutput `
        -RedirectStandardError $serverStandardError `
        -WindowStyle Hidden `
        -PassThru

    $listenDeadline = [DateTime]::UtcNow.AddSeconds(5)
    $listening = $false
    while ([DateTime]::UtcNow -lt $listenDeadline -and -not $serverProcess.HasExited)
    {
        if ($null -ne (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue))
        {
            $listening = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $listening)
    {
        $serverError = Get-Content -LiteralPath $serverStandardError -Raw -ErrorAction SilentlyContinue
        throw "CacheServer did not enter Listen state. error=$serverError"
    }

    & $clientExecutable --port $Port --ping-count 4 --expected-shards $ShardCount `
        --load-user-id $UserId --expected-currency-count 2 --expected-inventory-count 3 `
        1> $clientStandardOutput 2> $clientStandardError
    $clientExitCode = $LASTEXITCODE
    $clientOutput = Get-Content -LiteralPath $clientStandardOutput -Raw -ErrorAction SilentlyContinue
    if ($clientExitCode -ne 0 -or $clientOutput -notmatch "\[PASS\] Cache User Load")
    {
        $clientError = Get-Content -LiteralPath $clientStandardError -Raw -ErrorAction SilentlyContinue
        throw "Player cache load smoke test failed. exit=$clientExitCode error=$clientError"
    }

    $invalidUserId = $UserId + [uint64]$ShardCount
    & $clientExecutable --port $Port --ping-count 1 --expected-shards $ShardCount `
        --load-user-id $invalidUserId --load-routing-key $UserId --expect-invalid-load `
        1> $invalidRouteStandardOutput 2> $invalidRouteStandardError
    $invalidRouteExitCode = $LASTEXITCODE
    $invalidRouteOutput = Get-Content -LiteralPath $invalidRouteStandardOutput -Raw -ErrorAction SilentlyContinue
    if ($invalidRouteExitCode -ne 0 -or $invalidRouteOutput -notmatch "\[PASS\] Cache User Load invalid route rejected")
    {
        $invalidRouteError = Get-Content -LiteralPath $invalidRouteStandardError -Raw -ErrorAction SilentlyContinue
        throw "Player cache routing-key validation failed. exit=$invalidRouteExitCode error=$invalidRouteError"
    }

    if (-not $serverProcess.WaitForExit(15000))
    {
        throw "CacheServer did not exit cleanly within the timeout."
    }
    $serverProcess.WaitForExit()
    $serverProcess.Refresh()
    $serverExitCode = $serverProcess.ExitCode
    if ($null -ne $serverExitCode -and $serverExitCode -ne 0)
    {
        throw "CacheServer exited with code $serverExitCode."
    }

    $serverOutput = Get-Content -LiteralPath $serverStandardOutput -Raw -ErrorAction SilentlyContinue
    if ($serverOutput -notmatch "CacheServer stopped\.")
    {
        throw "CacheServer did not record a clean shutdown."
    }
    $loadLogCount = ([regex]::Matches($serverOutput, "Player cache loaded\. userId=$UserId\b")).Count
    if ($loadLogCount -ne 1)
    {
        throw "Expected exactly one GameDB load log, actual=$loadLogCount"
    }

    Write-Host $clientOutput.Trim()
    Write-Host $invalidRouteOutput.Trim()
    Write-Host "[PASS] GameDB load count=1; second request was served from cache."
    Write-Host "Logs: $testDirectory"
}
finally
{
    if ($serverProcess -ne $null -and -not $serverProcess.HasExited)
    {
        Stop-Process -Id $serverProcess.Id -Force
    }

    if ($seeded)
    {
        $cleanupSql = "USE gamedb; DELETE FROM inventory_items WHERE owner_user_id=$UserId; DELETE FROM player_currencies WHERE user_id=$UserId;"
        $cleanupSql | docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary mysql -uroot 2>$null
    }

    $env:MYSQL_PASSWORD = $previousMySqlPassword
}

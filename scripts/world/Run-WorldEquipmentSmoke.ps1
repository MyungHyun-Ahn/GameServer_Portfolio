param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Release",
	[int]$CachePort = 19303,
	[int]$WorldPort = 19300,
	[switch]$SkipBuild,
	[switch]$SkipInfrastructureStart
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$envFile = Join-Path $repositoryRoot ".env"
$baseWorldConfigPath = Join-Path $repositoryRoot "Config\Server\WorldServer.yaml"
$cacheServerConfigTemplate = Join-Path $repositoryRoot "Config\Server\CacheServer.yaml"
$serverConfigHelper = Join-Path $scriptsRoot "common\ServerConfig.ps1"
$cacheExecutable = Join-Path $repositoryRoot "Out\CacheServer\$Configuration\CacheServer.exe"
$worldExecutable = Join-Path $repositoryRoot "Out\WorldServer\$Configuration\WorldServer.exe"
$clientAssembly =
	Join-Path $repositoryRoot "Out\WorldClientWinForms\$Configuration\net9.0-windows\WorldClientWinForms.dll"
$runId = "{0}_{1}_{2}" -f (Get-Date -Format "yyyyMMdd_HHmmss_fff"), $PID, ([guid]::NewGuid().ToString("N").Substring(0, 8))
$runDirectory = Join-Path $repositoryRoot "Out\WorldEquipmentSmoke\$runId"
$loginServerBaseUrl = "http://127.0.0.1:18080"
$redisContainer = "gameserverportfolio-chat-redis"
$accountDbContainer = "gameserverportfolio-account-mysql"
$gameDbContainer = "gameserverportfolio-game-db-primary"
$rootPassword = ""
$testUserId = [uint64]0
$firstEquipmentItemInstanceId = [uint64]0
$secondEquipmentItemInstanceId = [uint64]0
$consumableItemInstanceId = [uint64]0
$redisKeys = [System.Collections.Generic.List[string]]::new()
$processes = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()

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

function Get-MSBuildPath
{
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path -LiteralPath $vswhere)
	{
		$visualStudioPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
		if (-not [string]::IsNullOrWhiteSpace($visualStudioPath))
		{
			$found = Join-Path $visualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
			if (Test-Path -LiteralPath $found)
			{
				return $found
			}
		}
	}

	$msbuildCommand = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
	if ($null -ne $msbuildCommand)
	{
		return $msbuildCommand.Source
	}

	throw "MSBuild.exe was not found."
}

function Wait-Listening
{
	param(
		[Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
		[Parameter(Mandatory = $true)][int]$Port,
		[int]$TimeoutSeconds = 15
	)

	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	do
	{
		Start-Sleep -Milliseconds 100
		$listener = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue |
			Where-Object { $_.OwningProcess -eq $Process.Id } |
			Select-Object -First 1
	}
	while ($null -eq $listener -and [DateTime]::UtcNow -lt $deadline -and -not $Process.HasExited)

	if ($null -eq $listener)
	{
		throw "Process $($Process.Id) did not listen on port $Port."
	}
}

function Stop-TestProcess
{
	param([System.Diagnostics.Process]$Process)

	if ($null -ne $Process -and -not $Process.HasExited)
	{
		Stop-Process -Id $Process.Id -Force
		$Process.WaitForExit(5000) | Out-Null
	}
}

function Wait-CleanExit
{
	param(
		[Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
		[int]$TimeoutMilliseconds = 70000
	)

	if (-not $Process.WaitForExit($TimeoutMilliseconds))
	{
		throw "Process $($Process.Id) did not exit within $TimeoutMilliseconds ms."
	}
	$Process.WaitForExit()
	$Process.Refresh()
	$exitCode = $Process.ExitCode
	if ($null -ne $exitCode -and $exitCode -ne 0)
	{
		throw "Process $($Process.Id) exited with code $exitCode."
	}
}

function Invoke-RootSql
{
	param(
		[Parameter(Mandatory = $true)][string]$Container,
		[Parameter(Mandatory = $true)][string]$Sql
	)

	$output = $Sql | docker exec -i -e "MYSQL_PWD=$rootPassword" $Container mysql -uroot -N
	if ($LASTEXITCODE -ne 0)
	{
		throw "MySQL command failed for container $Container."
	}
	return ($output | Out-String).Trim()
}

function Remove-TestData
{
	if ($testUserId -eq 0)
	{
		return
	}

	Invoke-RootSql -Container $gameDbContainer -Sql @"
USE gamedb;
DELETE attachment
FROM mail_attachments AS attachment
INNER JOIN mails AS mail ON mail.mail_id = attachment.mail_id
WHERE mail.receiver_user_id = $testUserId;
DELETE FROM mails WHERE receiver_user_id = $testUserId;
DELETE FROM inventory_items WHERE owner_user_id = $testUserId;
DELETE FROM player_currencies WHERE user_id = $testUserId;
DELETE FROM player_characters WHERE user_id = $testUserId;
"@ | Out-Null
	Invoke-RootSql -Container $accountDbContainer -Sql "DELETE FROM accountdb.accounts WHERE account_id = $testUserId;" | Out-Null
}

function Remove-TestRedisKeys
{
	if ($testUserId -ne 0)
	{
		$redisKeys.Add("chat:active-login:$testUserId")
	}
	if ($redisKeys.Count -eq 0)
	{
		return
	}

	& docker exec $redisContainer redis-cli DEL @($redisKeys | Select-Object -Unique) | Out-Null
	if ($LASTEXITCODE -ne 0)
	{
		throw "Redis cleanup failed."
	}
}

function Add-LoginResponseKeys
{
	param([Parameter(Mandatory = $true)]$LoginResponse)

	$redisKeys.Add("chat:ticket:$($LoginResponse.ticket)")
	$redisKeys.Add("auction:ticket:$($LoginResponse.auctionTicket)")
	$redisKeys.Add("world:ticket:$($LoginResponse.worldTicket)")
}

function Invoke-Login
{
	param(
		[Parameter(Mandatory = $true)][string]$LoginId,
		[Parameter(Mandatory = $true)][string]$Password
	)

	$response = Invoke-RestMethod `
		-Method Post `
		-Uri "$loginServerBaseUrl/auth/login" `
		-ContentType "application/json" `
		-Body (@{ loginId = $LoginId; password = $Password } | ConvertTo-Json -Compress)
	Add-LoginResponseKeys -LoginResponse $response
	return $response
}

function New-WorldConfig
{
	$path = Join-Path $runDirectory "world.yaml"
	$logDirectory = (Join-Path $runDirectory "world-logs").Replace('\', '/')
	$text = Get-Content -LiteralPath $baseWorldConfigPath -Raw
	$text = $text.Replace("  AuthMode: Disabled", "  AuthMode: Redis")
	$text = $text.Replace("  CacheEnabled: false", "  CacheEnabled: true")
	$text = $text.Replace("  Port: 19200", "  Port: $WorldPort")
	$text = $text.Replace("  CachePort: 19103", "  CachePort: $CachePort")
	$text = $text.Replace("  RpcServerInstanceId: 1", "  RpcServerInstanceId: 1")
	$text = $text.Replace('  LogOutputDirectory: ""', "  LogOutputDirectory: `"$logDirectory`"")
	[System.IO.File]::WriteAllText($path, $text, [System.Text.UTF8Encoding]::new($false))
	return $path
}

function Start-LoggedProcess
{
	param(
		[Parameter(Mandatory = $true)][string]$FilePath,
		[Parameter(Mandatory = $true)][string[]]$ArgumentList,
		[Parameter(Mandatory = $true)][string]$Name,
		[Parameter(Mandatory = $true)][string]$WorkingDirectory
	)

	$process = Start-Process `
		-FilePath $FilePath `
		-ArgumentList $ArgumentList `
		-WorkingDirectory $WorkingDirectory `
		-RedirectStandardOutput (Join-Path $runDirectory "$Name.stdout.log") `
		-RedirectStandardError (Join-Path $runDirectory "$Name.stderr.log") `
		-WindowStyle Hidden `
		-PassThru
	$processes.Add($process)
	return $process
}

function Invoke-ClientCase
{
	param(
		[Parameter(Mandatory = $true)][string]$Name,
		[Parameter(Mandatory = $true)][string]$Ticket,
		[Parameter(Mandatory = $true)][uint32]$MapDataId,
		[Parameter(Mandatory = $true)][string[]]$EquipmentActions
	)

	$arguments = [System.Collections.Generic.List[string]]::new()
	foreach ($value in @(
		$clientAssembly,
		"--auth-smoke",
		"--world-ticket", $Ticket,
		"--world-host", "127.0.0.1",
		"--world-port", "$WorldPort",
		"--expect-auth-result", "0",
		"--map-data-id", "$MapDataId"))
	{
		$arguments.Add($value)
	}
	foreach ($action in $EquipmentActions)
	{
		$arguments.Add("--equipment-action")
		$arguments.Add($action)
	}

	$process = Start-Process `
		-FilePath "dotnet" `
		-ArgumentList $arguments `
		-WorkingDirectory (Split-Path -Parent $clientAssembly) `
		-RedirectStandardOutput (Join-Path $runDirectory "$Name.stdout.log") `
		-RedirectStandardError (Join-Path $runDirectory "$Name.stderr.log") `
		-WindowStyle Hidden `
		-Wait `
		-PassThru
	if ($process.ExitCode -ne 0)
	{
		$stderr = Get-Content -LiteralPath (Join-Path $runDirectory "$Name.stderr.log") -Raw -ErrorAction SilentlyContinue
		throw "World equipment client case failed: $Name (exit=$($process.ExitCode)) stderr=$stderr"
	}
}

function Get-FnvEquipmentVersion
{
	param([Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Items)

	[System.Numerics.BigInteger]$hash = 14695981039346656037
	[System.Numerics.BigInteger]$prime = 1099511628211
	[System.Numerics.BigInteger]$modulus = [System.Numerics.BigInteger]::Pow(2, 64)
	foreach ($item in ($Items | Sort-Object -Property ItemInstanceId))
	{
		foreach ($value in @(
			[uint64]$item.ItemInstanceId,
			[uint64]$item.ItemDataId,
			[uint64]$item.ItemVersion))
		{
			for ($byteIndex = 0; $byteIndex -lt 8; ++$byteIndex)
			{
				[System.Numerics.BigInteger]$byte =
					(([System.Numerics.BigInteger]$value -shr ($byteIndex * 8)) -band 255)
				$hash = (($hash -bxor $byte) * $prime) % $modulus
			}
		}
	}
	return [uint64]$hash
}

function Assert-LogMatch
{
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[Parameter(Mandatory = $true)][string]$Pattern,
		[Parameter(Mandatory = $true)][string]$Label
	)

	$text = Get-Content -LiteralPath $Path -Raw
	if (-not [regex]::IsMatch($text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline))
	{
		throw "Missing expected $Label in $Path. pattern=$Pattern`n$text"
	}
}

function Assert-EmptyLog
{
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[Parameter(Mandatory = $true)][string]$Label
	)

	$text = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
	if (-not [string]::IsNullOrWhiteSpace($text))
	{
		throw "$Label must be empty. path=$Path`n$text"
	}
}

if ($CachePort -le 0 -or $CachePort -gt 65535 -or $WorldPort -le 0 -or $WorldPort -gt 65535 -or
	$CachePort -eq $WorldPort)
{
	throw "Cache/World ports must be distinct valid TCP ports."
}
if (-not (Test-Path -LiteralPath $envFile))
{
	throw "Create $envFile before running the World equipment smoke."
}
foreach ($port in @($CachePort, $WorldPort))
{
	if ($null -ne (Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue | Select-Object -First 1))
	{
		throw "TCP port $port is already in use."
	}
}

$previousMySqlPassword = $env:MYSQL_PASSWORD
try
{
	New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null
	$appPassword = Get-DotEnvValue -Name "MYSQL_PASSWORD"
	$env:MYSQL_PASSWORD = $appPassword
	$rootPassword = Get-DotEnvValue -Name "MYSQL_ROOT_PASSWORD"

	if (-not $SkipInfrastructureStart)
	{
		& (Join-Path $repositoryRoot "Infra\Start-LoginPlatform.ps1")
		if ($LASTEXITCODE -ne 0)
		{
			throw "Login platform startup failed."
		}
		& (Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1")
		if ($LASTEXITCODE -ne 0)
		{
			throw "GameDB environment startup failed."
		}
	}

	$health = Invoke-RestMethod -Method Get -Uri "$loginServerBaseUrl/healthz"
	if (-not $health.success)
	{
		throw "LoginServer health check failed."
	}
	foreach ($container in @($redisContainer, $accountDbContainer, $gameDbContainer))
	{
		$status = (docker inspect --format "{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}" $container | Out-String).Trim()
		if ($LASTEXITCODE -ne 0 -or $status -notin @("healthy", "running"))
		{
			throw "Required container is not healthy: $container status=$status"
		}
	}

	if (-not $SkipBuild)
	{
		$msbuild = Get-MSBuildPath
		foreach ($project in @(
			(Join-Path $repositoryRoot "Cache\CacheServer\CacheServer.vcxproj"),
			(Join-Path $repositoryRoot "World\WorldServer\WorldServer.vcxproj")))
		{
			& $msbuild $project /t:Build /p:Configuration=$Configuration /p:Platform=x64 /m:1 /nr:false /nologo /v:minimal
			if ($LASTEXITCODE -ne 0)
			{
				throw "Build failed: $project"
			}
		}

		dotnet build (Join-Path $repositoryRoot "World\WorldClientWinForms\WorldClientWinForms.csproj") `
			-c $Configuration --no-restore
		if ($LASTEXITCODE -ne 0)
		{
			throw "WorldClientWinForms build failed."
		}
	}

	foreach ($path in @(
		$cacheExecutable,
		$worldExecutable,
		$clientAssembly,
		$baseWorldConfigPath,
		$cacheServerConfigTemplate))
	{
		if (-not (Test-Path -LiteralPath $path))
		{
			throw "World equipment smoke dependency is missing: $path"
		}
	}

	$suffix = $runId.Replace('_', '')
	$suffix = $suffix.Substring([Math]::Max(0, $suffix.Length - 18))
	$loginId = "world_equip_$suffix"
	$password = "WorldEquip!$suffix"
	$nickname = "equip_$suffix"
	$registration = Invoke-RestMethod `
		-Method Post `
		-Uri "$loginServerBaseUrl/auth/register" `
		-ContentType "application/json" `
		-Body (@{ loginId = $loginId; password = $password; nickname = $nickname } | ConvertTo-Json -Compress)
	$testUserId = [uint64]$registration.userId

	$seededItemIds = Invoke-RootSql -Container $gameDbContainer -Sql @"
USE gamedb;
INSERT INTO inventory_items
    (owner_user_id,item_data_id,quantity,item_data,is_equipped,is_tradable,version)
VALUES
    ($testUserId,1001,1,JSON_OBJECT('str',3,'dex',0,'int',0,'luk',0),0,1,1),
    ($testUserId,1002,1,JSON_OBJECT('str',0,'dex',4,'int',0,'luk',0),0,1,5),
    ($testUserId,2001,1,JSON_OBJECT('str',0,'dex',0,'int',0,'luk',0),0,1,1);
SELECT CONCAT(LAST_INSERT_ID(), ',', LAST_INSERT_ID() + 1, ',', LAST_INSERT_ID() + 2);
"@
	$seededItemIdFields = $seededItemIds.Split(',')
	if ($seededItemIdFields.Count -ne 3 -or
		-not [uint64]::TryParse($seededItemIdFields[0], [ref]$firstEquipmentItemInstanceId) -or
		-not [uint64]::TryParse($seededItemIdFields[1], [ref]$secondEquipmentItemInstanceId) -or
		-not [uint64]::TryParse($seededItemIdFields[2], [ref]$consumableItemInstanceId))
	{
		throw "Failed to read seeded equipment item instance ids. output=$seededItemIds"
	}

	$worldConfig = New-WorldConfig
	$cacheServerConfig = Join-Path $runDirectory "cache.server.yaml"
	New-ServerConfigFile `
		-TemplatePath $cacheServerConfigTemplate `
		-DestinationPath $cacheServerConfig `
		-Overrides @{
			"CacheServer.Backend" = "Iocp"
			"CacheServer.RioSendDispatchMode" = "Direct"
			"CacheServer.Port" = $CachePort
			"CacheServer.PlayerCacheShardCount" = 4
			"CacheServer.RpcServerInstanceId" = 1
			"CacheServer.DatabaseEnabled" = $true
			"GameDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
			"CachePolicy.GameOwnerLeaseMilliseconds" = 5000
			"CachePolicy.MaintenanceIntervalMilliseconds" = 500
			"CachePolicy.RevokeTimeoutMilliseconds" = 3000
			"Debug.RunSeconds" = 50
		} | Out-Null
	$cache = Start-LoggedProcess `
		-FilePath $cacheExecutable `
		-ArgumentList @("--config", $cacheServerConfig) `
		-Name "cache" `
		-WorkingDirectory $runDirectory
	Wait-Listening -Process $cache -Port $CachePort

	$world = Start-LoggedProcess `
		-FilePath $worldExecutable `
		-ArgumentList @("--config", $worldConfig, "--run-seconds", "45") `
		-Name "world" `
		-WorkingDirectory $runDirectory
	Wait-Listening -Process $world -Port $WorldPort

	$serialLogin = Invoke-Login -LoginId $loginId -Password $password
	Invoke-ClientCase `
		-Name "client-serial" `
		-Ticket $serialLogin.worldTicket `
		-MapDataId 1 `
		-EquipmentActions @(
			"equip:${consumableItemInstanceId}:1:21",
			"equip:${firstEquipmentItemInstanceId}:1:0",
			"equip:${secondEquipmentItemInstanceId}:5:0",
			"equip:${secondEquipmentItemInstanceId}:5:20")

	Start-Sleep -Milliseconds 500
	$taskGraphLogin = Invoke-Login -LoginId $loginId -Password $password
	Invoke-ClientCase `
		-Name "client-taskgraph" `
		-Ticket $taskGraphLogin.worldTicket `
		-MapDataId 2 `
		-EquipmentActions @(
			"unequip:${secondEquipmentItemInstanceId}:6:0",
			"equip:${firstEquipmentItemInstanceId}:3:0",
			"unequip:${firstEquipmentItemInstanceId}:4:0")

	$emptyEquipmentVersion = Get-FnvEquipmentVersion -Items @()
	$swordVersion2 = Get-FnvEquipmentVersion -Items @(
		[pscustomobject]@{ ItemInstanceId = $firstEquipmentItemInstanceId; ItemDataId = 1001; ItemVersion = 2 })
	$bowVersion6 = Get-FnvEquipmentVersion -Items @(
		[pscustomobject]@{ ItemInstanceId = $secondEquipmentItemInstanceId; ItemDataId = 1002; ItemVersion = 6 })
	$swordVersion4 = Get-FnvEquipmentVersion -Items @(
		[pscustomobject]@{ ItemInstanceId = $firstEquipmentItemInstanceId; ItemDataId = 1001; ItemVersion = 4 })

	$serialLog = Join-Path $runDirectory "client-serial.stdout.log"
	Assert-LogMatch -Path $serialLog -Label "Serial baseline snapshot" -Pattern `
		("MapEnter after WorldAuth: map=\d+, entity=\d+, stats=4/4/4/4, hp=120/120, mp=70/70, " +
		 "attack=26, defense=9, moveSpeedMilli=96400, equipmentVersion=$emptyEquipmentVersion, statRevision=1")
	Assert-LogMatch -Path $serialLog -Label "non-equipment rejection" -Pattern `
		("\[PASS\] Equip: item=$consumableItemInstanceId, result=21,")
	Assert-LogMatch -Path $serialLog -Label "sword final stats" -Pattern `
		("\[PASS\] Equip: item=$firstEquipmentItemInstanceId, result=0, itemVersion=2, equipped=True, " +
		 "stats=19/4/4/4, hp=120/195, mp=70/70, attack=53, defense=9, moveSpeedMilli=96400, " +
		 "equipmentVersion=$swordVersion2, statRevision=2")
	Assert-LogMatch -Path $serialLog -Label "same-slot bow replacement final stats" -Pattern `
		("\[PASS\] Equip: item=$secondEquipmentItemInstanceId, result=0, itemVersion=6, equipped=True, " +
		 "stats=4/20/4/4, hp=120/120, mp=70/70, attack=54, defense=9, moveSpeedMilli=98000, " +
		 "equipmentVersion=$bowVersion6, statRevision=3")
	Assert-LogMatch -Path $serialLog -Label "stale item version rejection" -Pattern `
		("\[PASS\] Equip: item=$secondEquipmentItemInstanceId, result=20,")

	$taskGraphLog = Join-Path $runDirectory "client-taskgraph.stdout.log"
	Assert-LogMatch -Path $taskGraphLog -Label "TaskGraph reconnect snapshot" -Pattern `
		("MapEnter after WorldAuth: map=\d+, entity=\d+, stats=4/20/4/4, hp=120/120, mp=70/70, " +
		 "attack=54, defense=9, moveSpeedMilli=98000, equipmentVersion=$bowVersion6, statRevision=3")
	Assert-LogMatch -Path $taskGraphLog -Label "TaskGraph bow unequip" -Pattern `
		("\[PASS\] Unequip: item=$secondEquipmentItemInstanceId, result=0, itemVersion=7, equipped=False, " +
		 "stats=4/4/4/4, hp=120/120, mp=70/70, attack=26, defense=9, moveSpeedMilli=96400, " +
		 "equipmentVersion=$emptyEquipmentVersion, statRevision=4")
	Assert-LogMatch -Path $taskGraphLog -Label "TaskGraph sword re-equip" -Pattern `
		("\[PASS\] Equip: item=$firstEquipmentItemInstanceId, result=0, itemVersion=4, equipped=True, " +
		 "stats=19/4/4/4, hp=120/195, mp=70/70, attack=53, defense=9, moveSpeedMilli=96400, " +
		 "equipmentVersion=$swordVersion4, statRevision=5")
	Assert-LogMatch -Path $taskGraphLog -Label "TaskGraph sword unequip" -Pattern `
		("\[PASS\] Unequip: item=$firstEquipmentItemInstanceId, result=0, itemVersion=5, equipped=False, " +
		 "stats=4/4/4/4, hp=120/120, mp=70/70, attack=26, defense=9, moveSpeedMilli=96400, " +
		 "equipmentVersion=$emptyEquipmentVersion, statRevision=6")

	$equipmentState = Invoke-RootSql -Container $gameDbContainer -Sql `
		"SELECT GROUP_CONCAT(CONCAT(item_instance_id,':',is_equipped,':',version) ORDER BY item_instance_id SEPARATOR '|') FROM gamedb.inventory_items WHERE owner_user_id=$testUserId;"
	$expectedEquipmentState =
		"${firstEquipmentItemInstanceId}:0:5|${secondEquipmentItemInstanceId}:0:7|${consumableItemInstanceId}:0:1"
	if ($equipmentState -ne $expectedEquipmentState)
	{
		throw "Equipment DB state mismatch. expected='$expectedEquipmentState' actual='$equipmentState'"
	}

	Wait-CleanExit -Process $world
	Wait-CleanExit -Process $cache
	foreach ($name in @("cache", "world", "client-serial", "client-taskgraph"))
	{
		Assert-EmptyLog -Path (Join-Path $runDirectory "$name.stderr.log") -Label "$name stderr"
	}

	Write-Host "[PASS] Serial map: rejection, equip, same-slot replacement, stale-version rejection"
	Write-Host "[PASS] TaskGraph map: persisted reconnect snapshot and Tick-boundary equip/unequip"
	Write-Host "[PASS] Exact final stats, equipment/stat revisions, item versions"
	Write-Host "[PASS] Equipment DB state = $expectedEquipmentState"
	Write-Host "[PASS] Cache/World/client stderr logs are empty"
	Write-Host "Logs: $runDirectory"
}
finally
{
	foreach ($process in $processes)
	{
		Stop-TestProcess -Process $process
	}
	if (-not [string]::IsNullOrWhiteSpace($rootPassword))
	{
		try
		{
			Remove-TestRedisKeys
		}
		catch
		{
			Write-Warning "World equipment Redis cleanup failed: $($_.Exception.Message)"
		}
		try
		{
			Remove-TestData
		}
		catch
		{
			Write-Warning "World equipment database cleanup failed: $($_.Exception.Message)"
		}
	}
	$env:MYSQL_PASSWORD = $previousMySqlPassword
}

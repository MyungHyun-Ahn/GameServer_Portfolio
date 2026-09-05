param(
	[int]$Port = 19103,
	[long]$UserId = 880002,
	[int]$FirstGameInstanceId = 1,
	[int]$SecondGameInstanceId = 2,
	[int]$CacheInstanceId = 1,
	[ValidateSet("iocp", "rio-direct", "rio-owner")]
	[string]$Backend = "iocp",
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Debug",
	[switch]$SkipBuild,
	[switch]$SkipDatabaseStart
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$envFile = Join-Path $repositoryRoot ".env"
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
	param(
		[Parameter(Mandatory = $true)]
		[string]$Name
	)

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

function Invoke-GameDbRootSql
{
	param(
		[Parameter(Mandatory = $true)]
		[string]$Sql,

		[Parameter(Mandatory = $true)]
		[string]$RootPassword
	)

	$output = $Sql | docker exec -i -e "MYSQL_PWD=$RootPassword" gameserverportfolio-game-db-primary mysql -uroot -N
	if ($LASTEXITCODE -ne 0)
	{
		throw "GameDB SQL execution failed."
	}

	return ($output | Out-String).Trim()
}

if ($Port -le 0 -or $Port -gt 65535)
{
	throw "Port must be in range 1..65535."
}
if ($UserId -le 0)
{
	throw "UserId must be positive."
}
if ($FirstGameInstanceId -le 0 -or $SecondGameInstanceId -le 0 -or
	$FirstGameInstanceId -eq $SecondGameInstanceId)
{
	throw "Game instance IDs must be distinct positive values."
}
if ($CacheInstanceId -le 0)
{
	throw "CacheInstanceId must be positive."
}
if (-not (Test-Path -LiteralPath $envFile))
{
	throw "Create $envFile from .env.example before running the UserPresence smoke test."
}

$hadPreviousMySqlPassword = Test-Path Env:MYSQL_PASSWORD
$previousMySqlPassword = $env:MYSQL_PASSWORD
$appPassword = Get-DotEnvValue -Name "MYSQL_PASSWORD"
$rootPassword = Get-DotEnvValue -Name "MYSQL_ROOT_PASSWORD"
$seedCurrencyId = 65000
$seedInserted = $false
$inventorySeedInserted = $false
$characterStateChecked = $false
$characterExistedBefore = $false
$firstEquipmentItemInstanceId = $UserId * 10 + 1
$secondEquipmentItemInstanceId = $UserId * 10 + 2
$consumableItemInstanceId = $UserId * 10 + 3

try
{
$env:MYSQL_PASSWORD = $appPassword
if (-not $SkipDatabaseStart)
{
	& (Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1")
	if ($LASTEXITCODE -ne 0)
	{
		throw "GameDB environment startup failed."
	}
}

$existingCharacterCount = Invoke-GameDbRootSql `
	-Sql "SELECT COUNT(*) FROM gamedb.player_characters WHERE user_id = $UserId;" `
	-RootPassword $rootPassword
if ($existingCharacterCount -ne "0")
{
	throw "The mutation smoke requires a fresh user without player_characters. Use another test userId. userId=${UserId} count=$existingCharacterCount"
}
$characterStateChecked = $true
$characterExistedBefore = $false

$existingSeedCount = Invoke-GameDbRootSql `
	-Sql "SELECT COUNT(*) FROM gamedb.player_currencies WHERE user_id = $UserId AND currency_id = $seedCurrencyId;" `
	-RootPassword $rootPassword
if ($existingSeedCount -ne "0")
{
	throw "The smoke seed already exists for userId=$UserId currencyId=$seedCurrencyId. Use another test userId."
}
Invoke-GameDbRootSql `
	-Sql "INSERT INTO gamedb.player_currencies(user_id, currency_id, amount, version) VALUES($UserId, $seedCurrencyId, 1234, 1);" `
	-RootPassword $rootPassword | Out-Null
$seedInserted = $true

$existingInventoryCount = Invoke-GameDbRootSql `
	-Sql "SELECT COUNT(*) FROM gamedb.inventory_items WHERE owner_user_id = $UserId;" `
	-RootPassword $rootPassword
if ($existingInventoryCount -ne "0")
{
	throw "The equipment mutation smoke requires an empty inventory. Use another test userId. userId=${UserId} count=$existingInventoryCount"
}
Invoke-GameDbRootSql `
	-Sql @"
INSERT INTO gamedb.inventory_items
    (item_instance_id,owner_user_id,item_data_id,quantity,item_data,is_equipped,is_tradable,version)
VALUES
    ($firstEquipmentItemInstanceId,$UserId,1001,1,JSON_OBJECT('str',3,'dex',0,'int',0,'luk',0),0,1,1),
    ($secondEquipmentItemInstanceId,$UserId,1002,1,JSON_OBJECT('str',0,'dex',4,'int',0,'luk',0),0,1,5),
    ($consumableItemInstanceId,$UserId,2001,1,JSON_OBJECT('str',0,'dex',0,'int',0,'luk',0),0,1,1);
"@ `
	-RootPassword $rootPassword | Out-Null
$inventorySeedInserted = $true

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
	throw "UserPresence smoke dependency is missing. Build the projects and verify the CacheServer config template."
}

$testDirectory = Join-Path $repositoryRoot ("Out\UserPresenceSmoke\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$serverStandardOutput = Join-Path $testDirectory "server.stdout.log"
$serverStandardError = Join-Path $testDirectory "server.stderr.log"
$clientStandardOutput = Join-Path $testDirectory "client.stdout.log"
$clientStandardError = Join-Path $testDirectory "client.stderr.log"
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
		"CacheServer.PlayerCacheShardCount" = 4
		"CacheServer.RpcServerInstanceId" = $CacheInstanceId
		"CacheServer.DatabaseEnabled" = $true
		"GameDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
		"CachePolicy.GameOwnerLeaseMilliseconds" = 2000
		"CachePolicy.MaintenanceIntervalMilliseconds" = 60000
		"Debug.RunSeconds" = 15
	} | Out-Null

$serverProcess = $null
try
{
	$serverProcess = Start-Process `
		-FilePath $serverExecutable `
		-ArgumentList @("--config", $serverConfigPath) `
		-WorkingDirectory $testDirectory `
		-RedirectStandardOutput $serverStandardOutput `
		-RedirectStandardError $serverStandardError `
		-WindowStyle Hidden `
		-PassThru

	$listenDeadline = [DateTime]::UtcNow.AddSeconds(8)
	$listening = $false
	while ([DateTime]::UtcNow -lt $listenDeadline -and -not $serverProcess.HasExited)
	{
		$listener = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue
		if ($null -ne $listener)
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

	& $clientExecutable `
		--user-presence-smoke `
		--port $Port `
		--user-id $UserId `
		--first-game-instance-id $FirstGameInstanceId `
		--second-game-instance-id $SecondGameInstanceId `
		--cache-instance-id $CacheInstanceId `
		1> $clientStandardOutput 2> $clientStandardError
	$clientExitCode = $LASTEXITCODE

	$clientOutput = Get-Content -LiteralPath $clientStandardOutput -Raw -ErrorAction SilentlyContinue
	if ($clientExitCode -ne 0 -or $clientOutput -notmatch "\[PASS\] Cache UserPresence")
	{
		$clientError = Get-Content -LiteralPath $clientStandardError -Raw -ErrorAction SilentlyContinue
		throw "UserPresence smoke failed. exit=$clientExitCode error=$clientError"
	}

	if (-not $serverProcess.WaitForExit(15000))
	{
		throw "CacheServer did not exit cleanly within the timeout."
	}
	$serverProcess.Refresh()
	$serverExitCode = $serverProcess.ExitCode
	if ($null -ne $serverExitCode -and $serverExitCode -ne 0)
	{
		$serverError = Get-Content -LiteralPath $serverStandardError -Raw -ErrorAction SilentlyContinue
		throw "CacheServer exited with code $serverExitCode. error=$serverError"
	}

	$equipmentState = Invoke-GameDbRootSql `
		-Sql "SELECT GROUP_CONCAT(CONCAT(item_instance_id,':',is_equipped,':',version) ORDER BY item_instance_id SEPARATOR '|') FROM gamedb.inventory_items WHERE owner_user_id=$UserId;" `
		-RootPassword $rootPassword
	$expectedEquipmentState = "${firstEquipmentItemInstanceId}:0:3|${secondEquipmentItemInstanceId}:0:7|${consumableItemInstanceId}:0:1"
	if ($equipmentState -ne $expectedEquipmentState)
	{
		throw "Equipment mutation DB state mismatch. expected='$expectedEquipmentState' actual='$equipmentState'"
	}
	Write-Host "[PASS] Equipment mutation DB state = $expectedEquipmentState"

	Write-Host $clientOutput.Trim()
	Write-Host "Logs: $testDirectory"
}
finally
{
	if ($null -ne $serverProcess -and -not $serverProcess.HasExited)
	{
		Stop-Process -Id $serverProcess.Id -Force
	}
}
}
finally
{
	if ($inventorySeedInserted)
	{
		try
		{
			Invoke-GameDbRootSql `
				-Sql "DELETE FROM gamedb.inventory_items WHERE owner_user_id = $UserId;" `
				-RootPassword $rootPassword | Out-Null
		}
		catch
		{
			Write-Warning "Failed to remove the UserPresence inventory seed for userId=$UserId. $($_.Exception.Message)"
		}
	}

	if ($seedInserted)
	{
		try
		{
			Invoke-GameDbRootSql `
				-Sql "DELETE FROM gamedb.player_currencies WHERE user_id = $UserId AND currency_id = $seedCurrencyId;" `
				-RootPassword $rootPassword | Out-Null
		}
		catch
		{
			Write-Warning "Failed to remove the UserPresence smoke seed for userId=$UserId. $($_.Exception.Message)"
		}
	}

	if ($characterStateChecked -and -not $characterExistedBefore)
	{
		try
		{
			Invoke-GameDbRootSql `
				-Sql "DELETE FROM gamedb.player_characters WHERE user_id = $UserId;" `
				-RootPassword $rootPassword | Out-Null
		}
		catch
		{
			Write-Warning "Failed to remove the UserPresence smoke character for userId=$UserId. $($_.Exception.Message)"
		}
	}

	if ($hadPreviousMySqlPassword)
	{
		$env:MYSQL_PASSWORD = $previousMySqlPassword
	}
	else
	{
		Remove-Item Env:MYSQL_PASSWORD -ErrorAction SilentlyContinue
	}
}

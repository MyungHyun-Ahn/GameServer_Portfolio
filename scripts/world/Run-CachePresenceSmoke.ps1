param(
	[ValidateSet("Snapshot", "Revoke", "Disconnect", "All")]
	[string]$Mode = "All",

	[ValidateSet("iocp", "rio-direct", "rio-owner")]
	[string]$Backend = "iocp",

	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Release",

	[int]$CachePort = 19103,
	[int]$WorldPort = 19200,
	[switch]$SkipBuild,
	[switch]$SkipDatabaseStart
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$envFile = Join-Path $repositoryRoot ".env"
$cacheExecutable = Join-Path $repositoryRoot "Out\CacheServer\$Configuration\CacheServer.exe"
$worldExecutable = Join-Path $repositoryRoot "Out\WorldServer\$Configuration\WorldServer.exe"
$dummyOutputDirectory = Join-Path $repositoryRoot "Out\WorldDummyClient\$Configuration\net9.0"
$dummyExecutable = Join-Path $dummyOutputDirectory "WorldDummyClient.exe"
$baseWorldConfigPath = Join-Path $repositoryRoot "Config\Server\WorldServer.yaml"
$baseDummyConfigPath = Join-Path $dummyOutputDirectory "appsettings.json"
$cacheServerConfigTemplate = Join-Path $repositoryRoot "Config\Server\CacheServer.yaml"
$serverConfigHelper = Join-Path $scriptsRoot "common\ServerConfig.ps1"
$testUserId = [uint64]4294967296
$rootPassword = ""

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

function Invoke-GameDbRootSql
{
	param([Parameter(Mandatory = $true)][string]$Sql)

	$output = $Sql | docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-game-db-primary mysql -uroot -N
	if ($LASTEXITCODE -ne 0)
	{
		throw "GameDB SQL execution failed."
	}
	return ($output | Out-String).Trim()
}

function Clear-TestUserData
{
	Invoke-GameDbRootSql @"
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

	$remainingCount = Invoke-GameDbRootSql @"
SELECT
    (SELECT COUNT(*) FROM gamedb.mails WHERE receiver_user_id = $testUserId)
  + (SELECT COUNT(*) FROM gamedb.inventory_items WHERE owner_user_id = $testUserId)
  + (SELECT COUNT(*) FROM gamedb.player_currencies WHERE user_id = $testUserId)
  + (SELECT COUNT(*) FROM gamedb.player_characters WHERE user_id = $testUserId);
"@
	if ($remainingCount -ne "0")
	{
		throw "World Cache Presence smoke residue remains for userId=$testUserId count=$remainingCount"
	}
}

function Wait-Listening
{
	param(
		[Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
		[Parameter(Mandatory = $true)][int]$Port,
		[int]$TimeoutSeconds = 8
	)

	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	do
	{
		Start-Sleep -Milliseconds 100
		$listener = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue
	}
	while ($null -eq $listener -and [DateTime]::UtcNow -lt $deadline -and -not $Process.HasExited)

	if ($null -eq $listener)
	{
		throw "Process $($Process.Id) did not listen on port $Port."
	}
}

function Wait-CleanExit
{
	param(
		[Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
		[int]$TimeoutMilliseconds = 20000
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

function Stop-TestProcess
{
	param([System.Diagnostics.Process]$Process)

	if ($null -ne $Process -and -not $Process.HasExited)
	{
		Stop-Process -Id $Process.Id -Force
	}
}

function New-TestDirectory
{
	param([Parameter(Mandatory = $true)][string]$Label)

	$runId = "{0}_{1}_{2}" -f (Get-Date -Format "yyyyMMdd_HHmmss_fff"), $PID, ([guid]::NewGuid().ToString("N").Substring(0, 8))
	$directory = Join-Path $repositoryRoot "Out\WorldCachePresenceSmoke\$Label\$runId"
	New-Item -ItemType Directory -Path $directory -Force | Out-Null
	return $directory
}

function New-WorldConfig
{
	param(
		[Parameter(Mandatory = $true)][string]$RunDirectory,
		[Parameter(Mandatory = $true)][string]$Name,
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][int]$RpcServerInstanceId
	)

	$configPath = Join-Path $RunDirectory "$Name.yaml"
	$logDirectory = (Join-Path $RunDirectory "$Name-logs").Replace('\', '/')
	$text = Get-Content -LiteralPath $baseWorldConfigPath -Raw
	$text = $text.Replace("CacheEnabled: false", "CacheEnabled: true")
	$text = $text.Replace("  Port: 19200", "  Port: $Port")
	$text = $text.Replace("  CachePort: 19103", "  CachePort: $CachePort")
	$text = $text.Replace("  RpcServerInstanceId: 1", "  RpcServerInstanceId: $RpcServerInstanceId")
	$text = $text.Replace('  LogOutputDirectory: ""', "  LogOutputDirectory: `"$logDirectory`"")
	[System.IO.File]::WriteAllText($configPath, $text, [System.Text.UTF8Encoding]::new($false))
	return $configPath
}

function New-DummyConfig
{
	param(
		[Parameter(Mandatory = $true)][string]$RunDirectory,
		[Parameter(Mandatory = $true)][string]$Name,
		[Parameter(Mandatory = $true)][int]$Port
	)

	$configPath = Join-Path $RunDirectory "$Name.json"
	$config = Get-Content -LiteralPath $baseDummyConfigPath -Raw | ConvertFrom-Json
	$config.WorldServerPort = $Port
	$config.VirtualUserCount = 1
	$config.WorkerCount = 1
	$config.ConnectsPerSecond = 1
	$config.ResponseTimeoutMs = 10000
	[System.IO.File]::WriteAllText(
		$configPath,
		($config | ConvertTo-Json -Depth 8),
		[System.Text.UTF8Encoding]::new($false))
	return $configPath
}

function Start-CacheServer
{
	param(
		[Parameter(Mandatory = $true)][string]$RunDirectory,
		[Parameter(Mandatory = $true)][int]$RunSeconds
	)

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

	$serverConfigPath = Join-Path $RunDirectory "cache.server.yaml"
	New-ServerConfigFile `
		-TemplatePath $cacheServerConfigTemplate `
		-DestinationPath $serverConfigPath `
		-Overrides @{
			"CacheServer.Backend" = $cacheBackend
			"CacheServer.RioSendDispatchMode" = $rioSendDispatchMode
			"CacheServer.Port" = $CachePort
			"CacheServer.PlayerCacheShardCount" = 4
			"CacheServer.RpcServerInstanceId" = 1
			"CacheServer.DatabaseEnabled" = $true
			"GameDatabase.Password" = (ConvertTo-ServerConfigYamlString $appPassword)
			"CachePolicy.GameOwnerLeaseMilliseconds" = 5000
			"CachePolicy.MaintenanceIntervalMilliseconds" = 500
			"CachePolicy.RevokeTimeoutMilliseconds" = 3000
			"Debug.RunSeconds" = $RunSeconds
		} | Out-Null

	return Start-Process `
		-FilePath $cacheExecutable `
		-ArgumentList @("--config", $serverConfigPath) `
		-WorkingDirectory $RunDirectory `
		-RedirectStandardOutput (Join-Path $RunDirectory "cache.stdout.log") `
		-RedirectStandardError (Join-Path $RunDirectory "cache.stderr.log") `
		-WindowStyle Hidden `
		-PassThru
}

function Start-WorldServer
{
	param(
		[Parameter(Mandatory = $true)][string]$RunDirectory,
		[Parameter(Mandatory = $true)][string]$Name,
		[Parameter(Mandatory = $true)][string]$ConfigPath,
		[Parameter(Mandatory = $true)][int]$RunSeconds
	)

	return Start-Process `
		-FilePath $worldExecutable `
		-ArgumentList @("--config", $ConfigPath, "--run-seconds", "$RunSeconds") `
		-WorkingDirectory $RunDirectory `
		-RedirectStandardOutput (Join-Path $RunDirectory "$Name.stdout.log") `
		-RedirectStandardError (Join-Path $RunDirectory "$Name.stderr.log") `
		-WindowStyle Hidden `
		-PassThru
}

function Start-WorldDummy
{
	param(
		[Parameter(Mandatory = $true)][string]$RunDirectory,
		[Parameter(Mandatory = $true)][string]$Name,
		[Parameter(Mandatory = $true)][string]$ConfigPath,
		[Parameter(Mandatory = $true)][int]$RunSeconds
	)

	return Start-Process `
		-FilePath $dummyExecutable `
		-ArgumentList @("--config", $ConfigPath, "--virtual-users", "1", "--run-seconds", "$RunSeconds", "--map-data-id", "1") `
		-WorkingDirectory $RunDirectory `
		-RedirectStandardOutput (Join-Path $RunDirectory "$Name.stdout.log") `
		-RedirectStandardError (Join-Path $RunDirectory "$Name.stderr.log") `
		-WindowStyle Hidden `
		-PassThru
}

function Assert-Contains
{
	param(
		[Parameter(Mandatory = $true)][string]$Text,
		[Parameter(Mandatory = $true)][string]$Pattern,
		[Parameter(Mandatory = $true)][string]$Message
	)

	if ($Text -notmatch $Pattern)
	{
		throw $Message
	}
}

function Invoke-SnapshotSmoke
{
	$runDirectory = New-TestDirectory -Label "Snapshot"
	$cache = $null
	$world = $null
	try
	{
		$worldConfig = New-WorldConfig -RunDirectory $runDirectory -Name "World" -Port $WorldPort -RpcServerInstanceId 1
		$dummyConfig = New-DummyConfig -RunDirectory $runDirectory -Name "Dummy" -Port $WorldPort
		$cache = Start-CacheServer -RunDirectory $runDirectory -RunSeconds 14
		Wait-Listening -Process $cache -Port $CachePort
		$world = Start-WorldServer -RunDirectory $runDirectory -Name "world" -ConfigPath $worldConfig -RunSeconds 10
		Wait-Listening -Process $world -Port $WorldPort

		& $dummyExecutable --config $dummyConfig --virtual-users 1 --run-seconds 3 --map-data-id 1 `
			1> (Join-Path $runDirectory "dummy.stdout.log") `
			2> (Join-Path $runDirectory "dummy.stderr.log")
		$dummyExitCode = $LASTEXITCODE
		Wait-CleanExit -Process $world
		Wait-CleanExit -Process $cache

		$worldOutput = Get-Content -LiteralPath (Join-Path $runDirectory "world.stdout.log") -Raw
		$cacheOutput = Get-Content -LiteralPath (Join-Path $runDirectory "cache.stdout.log") -Raw
		$dummyOutput = Get-Content -LiteralPath (Join-Path $runDirectory "dummy.stdout.log") -Raw
		if ($dummyExitCode -ne 0)
		{
			throw "Snapshot WorldDummy exited with code $dummyExitCode."
		}
		Assert-Contains $dummyOutput '\[WorldDummy\] PASS' "Snapshot WorldDummy PASS marker is missing."
		Assert-Contains $worldOutput 'World Cache RPC ready' "World Cache ready callback was not observed."
		Assert-Contains $worldOutput 'World player initialized from Cache Snapshot.*characterId=[1-9][0-9]*.*level=[1-9][0-9]*.*hp=[1-9][0-9]*/[1-9][0-9]*.*mp=[1-9][0-9]*/[1-9][0-9]*.*statRevision=[1-9][0-9]*' `
			"World Player Snapshot initialization fields are missing or zero."
		Assert-Contains $cacheOutput 'EnterUser completed' "Cache EnterUser completion is missing."
		Assert-Contains $cacheOutput 'Player world snapshot completed' "Cache Player Snapshot completion is missing."
		Assert-Contains $cacheOutput 'LeaveUser completed' "Cache LeaveUser completion is missing."
		Write-Host "[PASS] World Cache Snapshot + Presence lifecycle"
		Write-Host "Logs: $runDirectory"
	}
	finally
	{
		Stop-TestProcess $world
		Stop-TestProcess $cache
	}
}

function Invoke-RevokeSmoke
{
	$runDirectory = New-TestDirectory -Label "Revoke"
	$cache = $null
	$world1 = $null
	$world2 = $null
	$dummy1 = $null
	try
	{
		$world1Config = New-WorldConfig -RunDirectory $runDirectory -Name "World1" -Port $WorldPort -RpcServerInstanceId 1
		$world2Config = New-WorldConfig -RunDirectory $runDirectory -Name "World2" -Port ($WorldPort + 1) -RpcServerInstanceId 2
		$dummy1Config = New-DummyConfig -RunDirectory $runDirectory -Name "Dummy1" -Port $WorldPort
		$dummy2Config = New-DummyConfig -RunDirectory $runDirectory -Name "Dummy2" -Port ($WorldPort + 1)

		$cache = Start-CacheServer -RunDirectory $runDirectory -RunSeconds 18
		Wait-Listening -Process $cache -Port $CachePort
		$world1 = Start-WorldServer -RunDirectory $runDirectory -Name "world1" -ConfigPath $world1Config -RunSeconds 14
		Wait-Listening -Process $world1 -Port $WorldPort
		$dummy1 = Start-WorldDummy -RunDirectory $runDirectory -Name "dummy1" -ConfigPath $dummy1Config -RunSeconds 9
		Start-Sleep -Seconds 2

		$world2 = Start-WorldServer -RunDirectory $runDirectory -Name "world2" -ConfigPath $world2Config -RunSeconds 11
		Wait-Listening -Process $world2 -Port ($WorldPort + 1)
		& $dummyExecutable --config $dummy2Config --virtual-users 1 --run-seconds 2 --map-data-id 1 `
			1> (Join-Path $runDirectory "dummy2.stdout.log") `
			2> (Join-Path $runDirectory "dummy2.stderr.log")
		$dummy2ExitCode = $LASTEXITCODE

		if (-not $dummy1.WaitForExit(20000))
		{
			throw "First WorldDummy did not exit after Revoke."
		}
		Wait-CleanExit -Process $world1
		Wait-CleanExit -Process $world2
		Wait-CleanExit -Process $cache

		$world1Output = Get-Content -LiteralPath (Join-Path $runDirectory "world1.stdout.log") -Raw
		$world2Output = Get-Content -LiteralPath (Join-Path $runDirectory "world2.stdout.log") -Raw
		$cacheOutput = Get-Content -LiteralPath (Join-Path $runDirectory "cache.stdout.log") -Raw
		$dummy1Error = Get-Content -LiteralPath (Join-Path $runDirectory "dummy1.stderr.log") -Raw
		$dummy2Output = Get-Content -LiteralPath (Join-Path $runDirectory "dummy2.stdout.log") -Raw
		Assert-Contains $world1Output 'Cache revoked World owner' "First WorldServer did not handle RevokeUser."
		Assert-Contains $cacheOutput 'EnterUser completed.*result=2.*gameServerInstanceId=2' `
			"Cache did not replace the previous Game owner."
		Assert-Contains $world2Output 'World player initialized from Cache Snapshot' `
			"Replacement WorldServer did not initialize the Player Snapshot."
		Assert-Contains $dummy1Error '(Connection closed before the scenario completed|MoveRp rejected\..*result=14)' `
			"Revoked World client neither received PlayerRevoked nor disconnected."
		if ($dummy2ExitCode -ne 0)
		{
			throw "Replacement WorldDummy exited with code $dummy2ExitCode."
		}
		Assert-Contains $dummy2Output '\[WorldDummy\] PASS' "Replacement WorldDummy PASS marker is missing."
		Write-Host "[PASS] two-World EnterUser replacement + RevokeUser round-trip"
		Write-Host "Logs: $runDirectory"
	}
	finally
	{
		Stop-TestProcess $dummy1
		Stop-TestProcess $world2
		Stop-TestProcess $world1
		Stop-TestProcess $cache
	}
}

function Invoke-DisconnectSmoke
{
	$runDirectory = New-TestDirectory -Label "Disconnect"
	$cache = $null
	$world = $null
	$dummy = $null
	try
	{
		$worldConfig = New-WorldConfig -RunDirectory $runDirectory -Name "World" -Port $WorldPort -RpcServerInstanceId 1
		$dummyConfig = New-DummyConfig -RunDirectory $runDirectory -Name "Dummy" -Port $WorldPort
		$cache = Start-CacheServer -RunDirectory $runDirectory -RunSeconds 20
		Wait-Listening -Process $cache -Port $CachePort
		$world = Start-WorldServer -RunDirectory $runDirectory -Name "world" -ConfigPath $worldConfig -RunSeconds 12
		Wait-Listening -Process $world -Port $WorldPort
		$dummy = Start-WorldDummy -RunDirectory $runDirectory -Name "dummy" -ConfigPath $dummyConfig -RunSeconds 8
		Start-Sleep -Seconds 3

		Stop-Process -Id $cache.Id -Force
		$cache.WaitForExit()
		if (-not $dummy.WaitForExit(15000))
		{
			throw "WorldDummy did not exit after Cache failure."
		}
		Wait-CleanExit -Process $world

		$worldOutput = Get-Content -LiteralPath (Join-Path $runDirectory "world.stdout.log") -Raw
		$dummyError = Get-Content -LiteralPath (Join-Path $runDirectory "dummy.stderr.log") -Raw
		Assert-Contains $worldOutput 'World player initialized from Cache Snapshot' `
			"Player Snapshot was not initialized before Cache fault injection."
		Assert-Contains $worldOutput 'World Cache RPC disconnected; revoking local player readiness.*playerSessionCount=1' `
			"WorldServer did not revoke the active Player after Cache failure."
		Assert-Contains $dummyError 'Connection closed before the scenario completed' `
			"World client stayed connected after Cache failure."
		Assert-Contains $worldOutput 'WorldServer stopped' "WorldServer did not stop cleanly after Cache failure."
		if ($dummy.ExitCode -eq 0)
		{
			throw "WorldDummy unexpectedly succeeded after Cache failure."
		}
		Write-Host "[PASS] Cache process failure disconnect policy"
		Write-Host "Logs: $runDirectory"
	}
	finally
	{
		Stop-TestProcess $dummy
		Stop-TestProcess $world
		Stop-TestProcess $cache
	}
}

if ($CachePort -le 0 -or $CachePort -gt 65535 -or $WorldPort -le 0 -or $WorldPort -ge 65535)
{
	throw "CachePort and WorldPort must be valid; WorldPort + 1 must also be available."
}
if (-not (Test-Path -LiteralPath $envFile))
{
	throw "Create $envFile from .env.example before running the World Cache Presence smoke."
}

$previousMySqlPassword = $env:MYSQL_PASSWORD
try
{
	$appPassword = Get-DotEnvValue -Name "MYSQL_PASSWORD"
	$env:MYSQL_PASSWORD = $appPassword
	$rootPassword = Get-DotEnvValue -Name "MYSQL_ROOT_PASSWORD"
	if (-not $SkipDatabaseStart)
	{
		& (Join-Path $repositoryRoot "Infra\Start-AuctionDatabases.ps1")
		if ($LASTEXITCODE -ne 0)
		{
			throw "GameDB environment startup failed."
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

		dotnet build (Join-Path $repositoryRoot "World\WorldDummyClient\WorldDummyClient.csproj") -c $Configuration --no-restore
		if ($LASTEXITCODE -ne 0)
		{
			throw "WorldDummyClient build failed."
		}
	}

	foreach ($path in @(
		$cacheExecutable,
		$worldExecutable,
		$dummyExecutable,
		$baseWorldConfigPath,
		$baseDummyConfigPath,
		$cacheServerConfigTemplate))
	{
		if (-not (Test-Path -LiteralPath $path))
		{
			throw "World Cache Presence smoke dependency is missing: $path"
		}
	}

	Clear-TestUserData

	if ($Mode -in @("Snapshot", "All"))
	{
		Invoke-SnapshotSmoke
	}
	if ($Mode -in @("Revoke", "All"))
	{
		Invoke-RevokeSmoke
	}
	if ($Mode -in @("Disconnect", "All"))
	{
		Invoke-DisconnectSmoke
	}
}
finally
{
	if (-not [string]::IsNullOrWhiteSpace($rootPassword))
	{
		try
		{
			Clear-TestUserData
		}
		catch
		{
			Write-Warning "World Cache Presence smoke cleanup failed. $($_.Exception.Message)"
		}
	}
	$env:MYSQL_PASSWORD = $previousMySqlPassword
}

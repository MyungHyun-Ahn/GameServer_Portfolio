param(
	[ValidateSet("Debug", "Release")]
	[string]$Configuration = "Release",
	[int]$CachePort = 19103,
	[int]$WorldPort = 19200,
	[int]$WrongTargetWorldPort = 19201,
	[switch]$SkipBuild,
	[switch]$SkipDatabaseStart
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
$runDirectory = Join-Path $repositoryRoot "Out\WorldAuthSmoke\$runId"
$loginServerBaseUrl = "http://127.0.0.1:18080"
$redisContainer = "gameserverportfolio-chat-redis"
$accountDbContainer = "gameserverportfolio-account-mysql"
$gameDbContainer = "gameserverportfolio-game-db-primary"
$rootPassword = ""
$testUserId = [uint64]0
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
		[int]$TimeoutSeconds = 12
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
		[int]$TimeoutMilliseconds = 30000
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

	$Sql | docker exec -i -e "MYSQL_PWD=$rootPassword" $Container mysql -uroot -N | Out-Null
	if ($LASTEXITCODE -ne 0)
	{
		throw "MySQL cleanup failed for container $Container."
	}
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
"@
	Invoke-RootSql -Container $accountDbContainer -Sql "DELETE FROM accountdb.accounts WHERE account_id = $testUserId;"
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

function New-WorldConfig
{
	param(
		[Parameter(Mandatory = $true)][string]$Name,
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][int]$ServerInstanceId,
		[Parameter(Mandatory = $true)][bool]$CacheEnabled
	)

	$configPath = Join-Path $runDirectory "$Name.yaml"
	$logDirectory = (Join-Path $runDirectory "$Name-logs").Replace('\', '/')
	$text = Get-Content -LiteralPath $baseWorldConfigPath -Raw
	$text = $text.Replace("  AuthMode: Disabled", "  AuthMode: Redis")
	$text = $text.Replace("  CacheEnabled: false", "  CacheEnabled: $($CacheEnabled.ToString().ToLowerInvariant())")
	$text = $text.Replace("  Port: 19200", "  Port: $Port")
	$text = $text.Replace("  CachePort: 19103", "  CachePort: $CachePort")
	$text = $text.Replace("  RpcServerInstanceId: 1", "  RpcServerInstanceId: $ServerInstanceId")
	$text = $text.Replace('  LogOutputDirectory: ""', "  LogOutputDirectory: `"$logDirectory`"")
	[System.IO.File]::WriteAllText($configPath, $text, [System.Text.UTF8Encoding]::new($false))
	return $configPath
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
		[Parameter(Mandatory = $true)][int]$Port,
		[Parameter(Mandatory = $true)][uint16]$ExpectedResult,
		[switch]$PreAuthCheck,
		[switch]$RepeatAuthCheck
	)

	$arguments = [System.Collections.Generic.List[string]]::new()
	foreach ($value in @(
		"--auth-smoke",
		"--world-ticket", $Ticket,
		"--world-host", "127.0.0.1",
		"--world-port", "$Port",
		"--expect-auth-result", "$ExpectedResult",
		"--map-data-id", "1"))
	{
		$arguments.Add($value)
	}
	if ($PreAuthCheck)
	{
		$arguments.Add("--pre-auth-check")
	}
	if ($RepeatAuthCheck)
	{
		$arguments.Add("--repeat-auth-check")
	}

	$clientArguments = [System.Collections.Generic.List[string]]::new()
	$clientArguments.Add($clientAssembly)
	$clientArguments.AddRange($arguments)
	$process = Start-Process `
		-FilePath "dotnet" `
		-ArgumentList $clientArguments `
		-WorkingDirectory (Split-Path -Parent $clientAssembly) `
		-RedirectStandardOutput (Join-Path $runDirectory "$Name.stdout.log") `
		-RedirectStandardError (Join-Path $runDirectory "$Name.stderr.log") `
		-WindowStyle Hidden `
		-Wait `
		-PassThru
	if ($process.ExitCode -ne 0)
	{
		throw "World client case failed: $Name (exit=$($process.ExitCode))"
	}
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

if ($CachePort -le 0 -or $CachePort -gt 65535 -or $WorldPort -le 0 -or $WorldPort -gt 65535 -or
	$WrongTargetWorldPort -le 0 -or $WrongTargetWorldPort -gt 65535 -or $WorldPort -eq $WrongTargetWorldPort)
{
	throw "Cache/World ports must be distinct valid TCP ports."
}
if (-not (Test-Path -LiteralPath $envFile))
{
	throw "Create $envFile before running the WorldAuth smoke."
}

$previousMySqlPassword = $env:MYSQL_PASSWORD
try
{
	New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null
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

	$health = Invoke-RestMethod -Method Get -Uri "$loginServerBaseUrl/healthz"
	if (-not $health.success)
	{
		throw "LoginServer health check failed."
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

		dotnet build (Join-Path $repositoryRoot "World\WorldClientWinForms\WorldClientWinForms.csproj") -c $Configuration
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
			throw "WorldAuth smoke dependency is missing: $path"
		}
	}

	$mainWorldConfig = New-WorldConfig -Name "world-main" -Port $WorldPort -ServerInstanceId 1 -CacheEnabled $true
	$wrongWorldConfig =
		New-WorldConfig -Name "world-wrong-target" -Port $WrongTargetWorldPort -ServerInstanceId 2 -CacheEnabled $false
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
			"Debug.RunSeconds" = 35
		} | Out-Null

	$cache = Start-LoggedProcess `
		-FilePath $cacheExecutable `
		-ArgumentList @("--config", $cacheServerConfig) `
		-Name "cache" `
		-WorkingDirectory $runDirectory
	Wait-Listening -Process $cache -Port $CachePort

	$worldMain = Start-LoggedProcess `
		-FilePath $worldExecutable `
		-ArgumentList @("--config", $mainWorldConfig, "--run-seconds", "30") `
		-Name "world-main" `
		-WorkingDirectory $runDirectory
	Wait-Listening -Process $worldMain -Port $WorldPort

	$worldWrongTarget = Start-LoggedProcess `
		-FilePath $worldExecutable `
		-ArgumentList @("--config", $wrongWorldConfig, "--run-seconds", "30") `
		-Name "world-wrong-target" `
		-WorkingDirectory $runDirectory
	Wait-Listening -Process $worldWrongTarget -Port $WrongTargetWorldPort

	$suffix = $runId.Replace('_', '').Substring([Math]::Max(0, $runId.Replace('_', '').Length - 18))
	$loginId = "world_auth_$suffix"
	$password = "WorldAuth!$suffix"
	$nickname = "world_$suffix"
	$registration = Invoke-RestMethod `
		-Method Post `
		-Uri "$loginServerBaseUrl/auth/register" `
		-ContentType "application/json" `
		-Body (@{ loginId = $loginId; password = $password; nickname = $nickname } | ConvertTo-Json -Compress)
	$testUserId = [uint64]$registration.userId

	$successLogin = Invoke-Login -LoginId $loginId -Password $password
	Invoke-ClientCase `
		-Name "client-success" `
		-Ticket $successLogin.worldTicket `
		-Port $WorldPort `
		-ExpectedResult 0 `
		-PreAuthCheck `
		-RepeatAuthCheck
	Invoke-ClientCase `
		-Name "client-replay" `
		-Ticket $successLogin.worldTicket `
		-Port $WorldPort `
		-ExpectedResult 16

	$wrongTargetLogin = Invoke-Login -LoginId $loginId -Password $password
	Invoke-ClientCase `
		-Name "client-wrong-target" `
		-Ticket $wrongTargetLogin.worldTicket `
		-Port $WrongTargetWorldPort `
		-ExpectedResult 16

	$expiredLogin = Invoke-Login -LoginId $loginId -Password $password
	& docker exec $redisContainer redis-cli EXPIRE "world:ticket:$($expiredLogin.worldTicket)" 1 | Out-Null
	if ($LASTEXITCODE -ne 0)
	{
		throw "Failed to shorten World Ticket TTL."
	}
	Start-Sleep -Seconds 2
	Invoke-ClientCase `
		-Name "client-expired" `
		-Ticket $expiredLogin.worldTicket `
		-Port $WorldPort `
		-ExpectedResult 16

	Wait-CleanExit -Process $worldMain
	Wait-CleanExit -Process $worldWrongTarget
	Wait-CleanExit -Process $cache

	Write-Host "[PASS] WorldAuth success + Cache Snapshot + MapEnter + pre-auth/repeat rejection"
	Write-Host "[PASS] WorldAuth replay rejected"
	Write-Host "[PASS] WorldAuth wrong-target rejected"
	Write-Host "[PASS] WorldAuth expired ticket rejected"
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
			Write-Warning "WorldAuth Redis cleanup failed: $($_.Exception.Message)"
		}
		try
		{
			Remove-TestData
		}
		catch
		{
			Write-Warning "WorldAuth database cleanup failed: $($_.Exception.Message)"
		}
	}
	$env:MYSQL_PASSWORD = $previousMySqlPassword
}

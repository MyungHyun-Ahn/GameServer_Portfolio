[CmdletBinding()]
param(
	[string]$EnvironmentFile = ".env"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$serverConfigHelper = Join-Path $scriptsRoot "common\ServerConfig.ps1"
. $serverConfigHelper

function Get-DotEnvValue
{
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path,

		[Parameter(Mandatory = $true)]
		[string]$Name,

		[switch]$Optional
	)

	$line = Get-Content -LiteralPath $Path |
		Where-Object { $_ -match ("^\s*" + [regex]::Escape($Name) + "\s*=") } |
		Select-Object -First 1
	if ($null -eq $line)
	{
		if ($Optional)
		{
			return ""
		}
		throw "Required value '$Name' was not found in $Path."
	}

	$value = ($line -split "=", 2)[1].Trim()
	if ($value.Length -ge 2 -and
		(($value.StartsWith('"') -and $value.EndsWith('"')) -or
		 ($value.StartsWith("'") -and $value.EndsWith("'"))))
	{
		$value = $value.Substring(1, $value.Length - 2)
	}

	if (-not $Optional -and [string]::IsNullOrEmpty($value))
	{
		throw "Required value '$Name' is empty in $Path."
	}
	return $value
}

$environmentPath = if ([System.IO.Path]::IsPathRooted($EnvironmentFile))
{
	$EnvironmentFile
}
else
{
	Join-Path $repositoryRoot $EnvironmentFile
}

if (-not (Test-Path -LiteralPath $environmentPath -PathType Leaf))
{
	throw "Environment file not found: $environmentPath"
}

$mysqlPassword = Get-DotEnvValue -Path $environmentPath -Name "MYSQL_PASSWORD"
$redisPassword = Get-DotEnvValue -Path $environmentPath -Name "REDIS_PASSWORD" -Optional

$serverConfigDirectory = Join-Path $repositoryRoot "Config\Server"
$cacheTemplate = Join-Path $serverConfigDirectory "CacheServer.yaml"
$cacheLocal = Join-Path $serverConfigDirectory "CacheServer.local.yaml"
$auctionTemplate = Join-Path $serverConfigDirectory "AuctionHouseServer.yaml"
$auctionLocal = Join-Path $serverConfigDirectory "AuctionHouseServer.local.yaml"

New-ServerConfigFile `
	-TemplatePath $cacheTemplate `
	-DestinationPath $cacheLocal `
	-Overrides @{
		"CacheServer.DatabaseEnabled" = $true
		"GameDatabase.Password" = (ConvertTo-ServerConfigYamlString $mysqlPassword)
	} | Out-Null

New-ServerConfigFile `
	-TemplatePath $auctionTemplate `
	-DestinationPath $auctionLocal `
	-Overrides @{
		"Authentication.Enabled" = $true
		"Authentication.RedisPassword" = (ConvertTo-ServerConfigYamlString $redisPassword)
		"AuctionDatabase.Enabled" = $true
		"AuctionDatabase.Password" = (ConvertTo-ServerConfigYamlString $mysqlPassword)
	} | Out-Null

Write-Host "Local server Config generation completed."
Write-Host "  Cache:   $cacheLocal"
Write-Host "  Auction: $auctionLocal"
Write-Host "The generated *.local.yaml files are excluded from Git."

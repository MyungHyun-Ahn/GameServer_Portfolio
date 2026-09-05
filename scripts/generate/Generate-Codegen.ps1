param(
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot

$generatePacketsScript = Join-Path $scriptDirectory "Generate-Packets.ps1"
$generateConfigsScript = Join-Path $scriptDirectory "Generate-Configs.ps1"
$generateGameDataScript = Join-Path $scriptDirectory "Generate-GameData.ps1"
$generateRpcsScript = Join-Path $scriptDirectory "Generate-Rpcs.ps1"
$formatCppScript = Join-Path (Join-Path $scriptsRoot "maintenance") "Format-Cpp.ps1"

if (-not (Test-Path $generatePacketsScript))
{
    throw "Generate-Packets script not found: $generatePacketsScript"
}

if (-not (Test-Path $generateConfigsScript))
{
    throw "Generate-Configs script not found: $generateConfigsScript"
}

if (-not (Test-Path $generateGameDataScript))
{
    throw "Generate-GameData script not found: $generateGameDataScript"
}

if (-not (Test-Path $generateRpcsScript))
{
    throw "Generate-Rpcs script not found: $generateRpcsScript"
}

Write-Host "Running packet/codegen pipeline..." -ForegroundColor Cyan
& powershell -ExecutionPolicy Bypass -File $generatePacketsScript -Configuration $Configuration
if ($LASTEXITCODE -ne 0)
{
	throw "Packet generation pipeline failed."
}

Write-Host "Running config/codegen pipeline..." -ForegroundColor Cyan
& powershell -ExecutionPolicy Bypass -File $generateConfigsScript -Configuration $Configuration
if ($LASTEXITCODE -ne 0)
{
	throw "Config generation pipeline failed."
}

if (Get-ChildItem -LiteralPath (Join-Path $repositoryRoot "GameData\Excel") -Filter "*.xlsx" -File -ErrorAction SilentlyContinue)
{
	Write-Host "Running game-data/codegen pipeline..." -ForegroundColor Cyan
	& powershell -ExecutionPolicy Bypass -File $generateGameDataScript -Configuration $Configuration
	if ($LASTEXITCODE -ne 0)
	{
		throw "Game-data generation pipeline failed."
	}
}
else
{
	Write-Warning "Game-data generation skipped because GameData/Excel has no .xlsx source workbook."
}

Write-Host "Running RPC/codegen pipeline..." -ForegroundColor Cyan
& powershell -ExecutionPolicy Bypass -File $generateRpcsScript -Configuration $Configuration
if ($LASTEXITCODE -ne 0)
{
	throw "RPC generation pipeline failed."
}

Write-Host "Formatting generated C++ code..." -ForegroundColor Cyan
& powershell -ExecutionPolicy Bypass -File $formatCppScript -SourceRoot (Join-Path $repositoryRoot "Generated")
if ($LASTEXITCODE -ne 0)
{
	throw "Generated C++ formatting failed."
}

Write-Host "Code generation completed." -ForegroundColor Green

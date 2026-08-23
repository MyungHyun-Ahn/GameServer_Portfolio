param(
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory

$generatePacketsScript = Join-Path $scriptDirectory "Generate-Packets.ps1"
$generateConfigsScript = Join-Path $scriptDirectory "Generate-Configs.ps1"
$formatCppScript = Join-Path (Join-Path $scriptsRoot "maintenance") "Format-Cpp.ps1"

if (-not (Test-Path $generatePacketsScript))
{
    throw "Generate-Packets script not found: $generatePacketsScript"
}

if (-not (Test-Path $generateConfigsScript))
{
    throw "Generate-Configs script not found: $generateConfigsScript"
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

Write-Host "Formatting generated C++ code..." -ForegroundColor Cyan
& powershell -ExecutionPolicy Bypass -File $formatCppScript
if ($LASTEXITCODE -ne 0)
{
	throw "Generated C++ formatting failed."
}

Write-Host "Code generation completed." -ForegroundColor Green

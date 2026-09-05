param(
    [string]$Configuration = "Debug",
    [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot
$projectPath = Join-Path $repositoryRoot "Tools\\ConfigGenerator\\ConfigGenerator.csproj"
$formatCppScript = Join-Path (Join-Path $scriptsRoot "maintenance") "Format-Cpp.ps1"
$generatedCppRoot = Join-Path $repositoryRoot "Generated\\Config"

if (-not (Test-Path $projectPath))
{
    throw "ConfigGenerator project not found: $projectPath"
}

if (-not (Test-Path -LiteralPath $formatCppScript -PathType Leaf))
{
    throw "C++ format script not found: $formatCppScript"
}

Write-Host "Building ConfigGenerator ($Configuration)..."
dotnet build $projectPath -c $Configuration
if ($LASTEXITCODE -ne 0)
{
    throw "ConfigGenerator build failed."
}

if ($BuildOnly)
{
    Write-Host "BuildOnly specified. Generation step skipped."
    exit 0
}

Write-Host "Running ConfigGenerator..."
dotnet run --project $projectPath -c $Configuration --no-build
if ($LASTEXITCODE -ne 0)
{
    throw "Config generation failed."
}

Write-Host "Formatting generated C++ config code..."
& powershell -ExecutionPolicy Bypass -File $formatCppScript -SourceRoot $generatedCppRoot
if ($LASTEXITCODE -ne 0)
{
    throw "Generated C++ config formatting failed."
}

Write-Host "Config generation completed."

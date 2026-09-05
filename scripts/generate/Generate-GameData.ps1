param(
    [string]$Configuration = "Debug",
    [switch]$BuildOnly,
    [switch]$Check,
    [switch]$ValidateOnly,
    [switch]$DeployRuntime
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$solutionRoot = Split-Path -Parent $scriptsRoot
$projectPath = Join-Path $solutionRoot "Tools\GameDataGenerator\GameDataGenerator.csproj"
$inputRoot = Join-Path $solutionRoot "GameData\Excel"
$outputRoot = Join-Path $solutionRoot "Generated\GameData"

if (-not (Test-Path $projectPath))
{
    throw "GameDataGenerator project not found: $projectPath"
}

if ($DeployRuntime -and ($BuildOnly -or $Check -or $ValidateOnly))
{
    throw "DeployRuntime cannot be combined with BuildOnly, Check, or ValidateOnly."
}

if ($DeployRuntime -and $Configuration -notin @("Debug", "Release"))
{
    throw "DeployRuntime requires Configuration to be Debug or Release."
}

Write-Host "Building GameDataGenerator ($Configuration)..."
dotnet build $projectPath -c $Configuration
if ($LASTEXITCODE -ne 0)
{
    throw "GameDataGenerator build failed."
}

if ($BuildOnly)
{
    Write-Host "BuildOnly specified. Generation step skipped."
    exit 0
}

$generatorArguments = @(
    "--input-root", $inputRoot,
    "--output-root", $outputRoot
)
if ($Check)
{
    $generatorArguments += "--check"
}
if ($ValidateOnly)
{
    $generatorArguments += "--validate-only"
}

Write-Host "Running GameDataGenerator..."
dotnet run --project $projectPath -c $Configuration --no-build -- @generatorArguments
if ($LASTEXITCODE -ne 0)
{
    throw "Game-data generation failed."
}

if ($DeployRuntime)
{
    $runtimeDeployments = @(
        @{
            ServerName = "WorldServer"
            Tables = @(
                "Map",
                "Monster",
                "SpawnArea",
                "MonsterSpawner",
                "Character",
                "CharacterLevel",
                "Item",
                "CombatFormulaPolicy",
                "StatConversion"
            )
        },
        @{
            ServerName = "CacheServer"
            Tables = @(
                "Item",
                "Character",
                "CharacterLevel",
                "CombatFormulaPolicy",
                "StatConversion",
                "InventoryPolicy",
                "Currency",
                "MailPolicy",
                "MailTemplate"
            )
        },
        @{
            ServerName = "AuctionHouseServer"
            Tables = @("Item", "AuctionPolicy", "Currency", "InventoryPolicy", "MailPolicy")
        }
    )

    foreach ($deployment in $runtimeDeployments)
    {
        $gameDataDirectory = Join-Path $solutionRoot "Out\$($deployment.ServerName)\$Configuration\Config\GameData"
        New-Item -ItemType Directory -Path $gameDataDirectory -Force | Out-Null

        foreach ($tableName in $deployment.Tables)
        {
            $source = Join-Path $outputRoot "Data\Server\$tableName.yaml"
            if (-not (Test-Path -LiteralPath $source))
            {
                throw "Generated runtime game data not found: $source"
            }
            Copy-Item -LiteralPath $source -Destination (Join-Path $gameDataDirectory "$tableName.yaml") -Force
        }

        Write-Host "$($deployment.ServerName) runtime game data deployed: $gameDataDirectory" -ForegroundColor Green
    }
}

Write-Host "Game-data generation completed."

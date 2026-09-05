param(
    [switch]$BuildLoginServerLocally,
    [switch]$RebuildLoginServerImage
)

$ErrorActionPreference = "Stop"

$infraDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent $infraDir
$loginServerDir = Join-Path $repositoryRoot "LoginServer"
$composeFile = Join-Path $infraDir "docker-compose.login-platform.yaml"
$schemaFile = Join-Path $loginServerDir "db\\schema.sql"
$envFile = Join-Path $repositoryRoot ".env"

function Get-DotEnvValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $line = Get-Content -LiteralPath $envFile |
        Where-Object { $_ -match "^$([regex]::Escape($Name))=" } |
        Select-Object -Last 1
    if ([string]::IsNullOrWhiteSpace($line)) {
        throw "Missing $Name in $envFile"
    }

    return $line.Substring($line.IndexOf('=') + 1).Trim().Trim('"').Trim("'")
}

function Wait-ContainerHealthy {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ContainerName,

        [int]$TimeoutSeconds = 120
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $state = docker inspect --format "{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}" $ContainerName 2>$null
        if ($LASTEXITCODE -eq 0) {
            $normalized = ($state | Out-String).Trim()
            if ($normalized -eq "healthy" -or $normalized -eq "running") {
                return
            }
        }

        Start-Sleep -Seconds 2
    }

    throw "Container '$ContainerName' did not become healthy within $TimeoutSeconds seconds."
}

function Invoke-LoginServerLocalBuild {
    if (-not (Test-Path (Join-Path $loginServerDir "node_modules"))) {
        Write-Host "[LoginPlatform] node_modules is missing. Running npm install first."
        npm install
        if ($LASTEXITCODE -ne 0) {
            throw "npm install failed."
        }
    }

    Write-Host "[LoginPlatform] Checking LoginServer types."
    npm run check
    if ($LASTEXITCODE -ne 0) {
        throw "npm run check failed."
    }

    Write-Host "[LoginPlatform] Building LoginServer locally."
    npm run build
    if ($LASTEXITCODE -ne 0) {
        throw "npm run build failed."
    }
}

Push-Location $loginServerDir
try {
    if ($BuildLoginServerLocally) {
        Invoke-LoginServerLocalBuild
    }
}
finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $envFile)) {
    throw "Create $envFile from .env.example before starting the login platform."
}

$rootPassword = Get-DotEnvValue -Name "MYSQL_ROOT_PASSWORD"

Write-Host "[LoginPlatform] Starting MySQL and Redis."
docker compose --env-file $envFile -f $composeFile up -d account-mysql chat-redis
if ($LASTEXITCODE -ne 0) {
    throw "docker compose up for account-mysql/chat-redis failed."
}

Wait-ContainerHealthy -ContainerName "gameserverportfolio-account-mysql"
Wait-ContainerHealthy -ContainerName "gameserverportfolio-chat-redis"

Write-Host "[LoginPlatform] Applying the account schema."
Get-Content $schemaFile -Raw | docker exec -i -e "MYSQL_PWD=$rootPassword" gameserverportfolio-account-mysql mysql -uroot accountdb
if ($LASTEXITCODE -ne 0) {
    throw "Failed to apply account schema."
}

$composeArgs = @("--env-file", $envFile, "-f", $composeFile, "up", "-d")
if ($RebuildLoginServerImage) {
    $composeArgs += "--build"
}
$composeArgs += "login-server"

Write-Host "[LoginPlatform] Starting the LoginServer container."
docker compose @composeArgs
if ($LASTEXITCODE -ne 0) {
    throw "docker compose up for login-server failed."
}

Wait-ContainerHealthy -ContainerName "gameserverportfolio-login-server"

Write-Host "[LoginPlatform] Login platform is ready."
Write-Host "  LoginServer: http://127.0.0.1:18080"
Write-Host "  Swagger UI : http://127.0.0.1:18080/docs"

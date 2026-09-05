param(
	[switch]$Check,
	[string[]]$SourceRoot = @()
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptsRoot = Split-Path -Parent $scriptDirectory
$repositoryRoot = Split-Path -Parent $scriptsRoot

function Find-ClangFormat
{
	$command = Get-Command clang-format -ErrorAction SilentlyContinue
	if ($null -ne $command)
	{
		return $command.Source
	}

	$candidates = @()
	if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR))
	{
		$candidates += Join-Path $env:VSINSTALLDIR "VC\Tools\Llvm\x64\bin\clang-format.exe"
	}

	$vswherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path -LiteralPath $vswherePath)
	{
		$visualStudioPath = & $vswherePath -latest -products * -property installationPath
		if (-not [string]::IsNullOrWhiteSpace($visualStudioPath))
		{
			$candidates += Join-Path $visualStudioPath "VC\Tools\Llvm\x64\bin\clang-format.exe"
		}
	}

	if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles))
	{
		$candidates += Join-Path $env:ProgramFiles "LLVM\bin\clang-format.exe"
	}

	foreach ($candidate in $candidates)
	{
		if (Test-Path -LiteralPath $candidate)
		{
			return $candidate
		}
	}

	throw "clang-format was not found. Install the Visual Studio C++ Clang tools component or add clang-format to PATH."
}

$defaultOwnedRoots = @(
	"Libraries\Foundation",
	"Libraries\NetworkLib",
	"Libraries\ClientNetworkLib",
	"Libraries\Connector",
	"Libraries\ContentsRuntime",
	"Libraries\RpcLib",
	"Libraries\GameData",
	"Chatting",
	"Echo",
	"Auction",
	"Cache",
	"World",
	"Generated",
	"SmokeTests"
)

$repositoryFullPath = [System.IO.Path]::GetFullPath($repositoryRoot).TrimEnd(
	[System.IO.Path]::DirectorySeparatorChar,
	[System.IO.Path]::AltDirectorySeparatorChar)
$repositoryPathPrefix = $repositoryFullPath + [System.IO.Path]::DirectorySeparatorChar

$formatRoots = if ($SourceRoot.Count -gt 0)
{
	foreach ($requestedRoot in $SourceRoot)
	{
		if ([string]::IsNullOrWhiteSpace($requestedRoot))
		{
			throw "SourceRoot must not be empty."
		}

		$absoluteRoot = if ([System.IO.Path]::IsPathRooted($requestedRoot))
		{
			[System.IO.Path]::GetFullPath($requestedRoot)
		}
		else
		{
			[System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $requestedRoot))
		}

		if (-not $absoluteRoot.Equals($repositoryFullPath, [System.StringComparison]::OrdinalIgnoreCase) -and
			-not $absoluteRoot.StartsWith($repositoryPathPrefix, [System.StringComparison]::OrdinalIgnoreCase))
		{
			throw "SourceRoot must be inside the repository: $requestedRoot"
		}

		if (-not (Test-Path -LiteralPath $absoluteRoot -PathType Container))
		{
			throw "SourceRoot directory not found: $absoluteRoot"
		}

		$absoluteRoot
	}
}
else
{
	foreach ($ownedRoot in $defaultOwnedRoots)
	{
		$absoluteRoot = Join-Path $repositoryRoot $ownedRoot
		if (Test-Path -LiteralPath $absoluteRoot -PathType Container)
		{
			$absoluteRoot
		}
	}
}

$sourceFiles = @($formatRoots |
	ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File } |
		Where-Object {
			$_.Extension -in @(".h", ".hpp", ".cpp") -and
			$_.FullName -notmatch "\\(Intermediate|Out|HttpLib|includes|ThirdParty|vcpkg_installed|node_modules)\\"
		} |
	Sort-Object -Property FullName -Unique)

$clangFormat = Find-ClangFormat
foreach ($sourceFile in $sourceFiles)
{
	if ($Check)
	{
		& $clangFormat --style=file --dry-run --Werror $sourceFile.FullName
	}
	else
	{
		& $clangFormat --style=file -i $sourceFile.FullName
	}

	if ($LASTEXITCODE -ne 0)
	{
		throw "clang-format failed: $($sourceFile.FullName)"
	}
}

$operation = if ($Check) { "checked" } else { "formatted" }
Write-Host "C++ format $operation successfully. files=$($sourceFiles.Count)"

param(
	[switch]$Check
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

	$candidates += @(
		"E:\apps\VisualStudio\VC\Tools\Llvm\x64\bin\clang-format.exe",
		"C:\Program Files\LLVM\bin\clang-format.exe"
	)

	foreach ($candidate in $candidates)
	{
		if (Test-Path -LiteralPath $candidate)
		{
			return $candidate
		}
	}

	throw "clang-format was not found. Install the Visual Studio C++ Clang tools component or add clang-format to PATH."
}

$ownedRoots = @(
	"Libraries\Foundation",
	"Libraries\NetworkLib",
	"Libraries\ClientNetworkLib",
	"Libraries\Connector",
	"Libraries\ContentsRuntime",
	"Libraries\GameData",
	"Chatting",
	"Echo",
	"Auction",
	"Generated",
	"SmokeTests"
)

$sourceFiles = foreach ($ownedRoot in $ownedRoots)
{
	$absoluteRoot = Join-Path $repositoryRoot $ownedRoot
	if (-not (Test-Path -LiteralPath $absoluteRoot))
	{
		continue
	}

	Get-ChildItem -LiteralPath $absoluteRoot -Recurse -File |
		Where-Object {
			$_.Extension -in @(".h", ".hpp", ".cpp") -and
			$_.FullName -notmatch "\\(Intermediate|Out|HttpLib|includes|ThirdParty|vcpkg_installed|node_modules)\\"
		}
}

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

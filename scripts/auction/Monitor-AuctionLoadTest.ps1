param(
    [Parameter(Mandatory = $true)]
    [int]$RunnerProcessId,
    [int]$IntervalSeconds = 60
)

$ErrorActionPreference = "Continue"
if ($RunnerProcessId -le 0 -or $IntervalSeconds -le 0)
{
    throw "RunnerProcessId and IntervalSeconds must be positive."
}

function Get-ProcessSample([string]$ProcessName)
{
    $process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
        Sort-Object StartTime -Descending |
        Select-Object -First 1
    if ($null -eq $process)
    {
        return @("", "", "", "", "", "")
    }

    return @(
        $process.Id,
        [math]::Round($process.CPU, 3),
        [math]::Round($process.WorkingSet64 / 1MB, 3),
        [math]::Round($process.PrivateMemorySize64 / 1MB, 3),
        $process.Threads.Count,
        $process.HandleCount)
}

Write-Output "timestampUtc,hostCpuPercent,availableMemoryMb,auctionPid,auctionCpuSeconds,auctionWorkingSetMb,auctionPrivateMb,auctionThreads,auctionHandles,cachePid,cacheCpuSeconds,cacheWorkingSetMb,cachePrivateMb,cacheThreads,cacheHandles,dummyPid,dummyCpuSeconds,dummyWorkingSetMb,dummyPrivateMb,dummyThreads,dummyHandles"
while ($null -ne (Get-Process -Id $RunnerProcessId -ErrorAction SilentlyContinue))
{
    $processor = Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue |
        Measure-Object -Property LoadPercentage -Average
    $operatingSystem = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
    $hostCpuPercent = if ($null -eq $processor.Average) { "" } else { [math]::Round($processor.Average, 2) }
    $availableMemoryMb = if ($null -eq $operatingSystem) { "" } else { [math]::Round($operatingSystem.FreePhysicalMemory / 1KB, 2) }
    $auction = Get-ProcessSample "AuctionHouseServer"
    $cache = Get-ProcessSample "CacheServer"
    $dummy = Get-ProcessSample "AuctionDummyClient"
    $values = @((Get-Date).ToUniversalTime().ToString("o"), $hostCpuPercent, $availableMemoryMb) + $auction + $cache + $dummy
    Write-Output ($values -join ",")
    Start-Sleep -Seconds $IntervalSeconds
}

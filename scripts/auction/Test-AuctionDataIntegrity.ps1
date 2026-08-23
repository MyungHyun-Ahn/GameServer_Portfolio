param(
    [Parameter(Mandatory = $true)]
    [uint64]$FirstUserId,

    [Parameter(Mandatory = $true)]
    [uint64]$LastUserId,

    [int]$MaxActiveListings = 0,
    [ValidateRange(1, 100)]
    [int]$SampleLimit = 10,
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"

if ($FirstUserId -eq 0 -or $LastUserId -lt $FirstUserId)
{
    throw "The integrity-check user range is invalid."
}

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $scriptDirectory)
$envFile = Join-Path $repositoryRoot ".env"
$auctionPolicyPath = Join-Path $repositoryRoot "Config\GameData\Auction.yaml"

function Get-DotEnvValue([string]$Name)
{
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

function Invoke-MySqlQuery(
    [string]$Container,
    [string]$Sql,
    [string]$RootPassword)
{
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try
    {
        $output = & docker exec -e "MYSQL_PWD=$RootPassword" $Container `
            mysql -uroot --batch --raw --skip-column-names -e $Sql 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally
    {
        $ErrorActionPreference = $previousPreference
    }

    if ($exitCode -ne 0)
    {
        throw "Integrity query failed in $Container. output=$($output | Out-String)"
    }
    return @($output | ForEach-Object { "$_" } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Add-CheckResult(
    [System.Collections.Generic.List[object]]$Results,
    [string]$Name,
    [uint64]$ViolationCount,
    [string]$Sample,
    [string]$Description)
{
    $Results.Add([pscustomobject]@{
        name = $Name
        passed = ($ViolationCount -eq 0)
        violationCount = $ViolationCount
        sample = $Sample
        description = $Description
    })
}

function Add-SqlCheckResults(
    [System.Collections.Generic.List[object]]$Results,
    [string[]]$Rows,
    [hashtable]$Descriptions)
{
    foreach ($row in $Rows)
    {
        $values = $row -split "`t", 3
        if ($values.Count -lt 2)
        {
            throw "Integrity query returned an invalid row: $row"
        }
        $name = $values[0]
        $count = [uint64]$values[1]
        $sample = if ($values.Count -ge 3) { $values[2] } else { "" }
        $description = if ($Descriptions.ContainsKey($name)) { $Descriptions[$name] } else { $name }
        Add-CheckResult $Results $name $count $sample $description
    }
}

function Convert-ToIdList([string[]]$Rows, [int]$ColumnIndex)
{
    $ids = [System.Collections.Generic.List[uint64]]::new()
    foreach ($row in $Rows)
    {
        $values = $row -split "`t"
        if ($values.Count -gt $ColumnIndex)
        {
            $ids.Add([uint64]$values[$ColumnIndex])
        }
    }
    return $ids
}

if (-not (Test-Path -LiteralPath $envFile))
{
    throw "Create $envFile from .env.example first."
}

if ($MaxActiveListings -le 0)
{
    $policyLine = Get-Content -LiteralPath $auctionPolicyPath |
        Where-Object { $_ -match '^\s*MaxActiveListings\s*:\s*(\d+)\s*$' } |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($policyLine) -or $policyLine -notmatch '^\s*MaxActiveListings\s*:\s*(\d+)\s*$')
    {
        throw "MaxActiveListings was not found in $auctionPolicyPath"
    }
    $MaxActiveListings = [int]$Matches[1]
}

$rootPassword = Get-DotEnvValue "MYSQL_ROOT_PASSWORD"
$results = [System.Collections.Generic.List[object]]::new()
$auctionDescriptions = @{
    LISTING_INVALID_STATE = "Listing state is outside EAuctionListingState."
    BID_INVALID_STATE = "Bid state is outside EAuctionBidState."
    LISTING_TRANSIENT_STATE = "A quiescent test left a listing in a transaction intermediate state."
    BID_TRANSIENT_STATE = "A quiescent test left a bid in a transaction intermediate state."
    ACTIVE_HIGHEST_MISMATCH = "An active listing and its highest bid disagree on identity, user, amount, currency, or state."
    MULTIPLE_HIGHEST_BIDS = "A listing has more than one HIGHEST bid."
    HIGHEST_BID_ON_NON_ACTIVE_LISTING = "A non-active listing still owns a HIGHEST bid."
    ACTIVE_FINAL_FIELDS_SET = "An active listing already contains final sale fields."
    SOLD_FINAL_FIELDS_INVALID = "A sold listing has incomplete or invalid final sale fields."
    BID_SALE_WINNER_MISMATCH = "A bid-sale listing does not reference the matching WON bid."
    UNSOLD_TERMINAL_FIELDS_SET = "A cancelled or expired listing still contains highest/final sale fields."
    WON_BID_MISMATCH = "A WON bid does not match its sold listing and final buyer."
    ACTIVE_LISTING_LIMIT_EXCEEDED = "A seller exceeds the configured active-listing limit."
    DUPLICATE_ESCROW_ITEM = "The same item instance is escrowed by multiple non-terminal listings."
    ORPHAN_BID = "A bid references a listing that does not exist."
}

$auctionSql = @"
USE auctiondb;
WITH relevant_listings AS
(
    SELECT l.*
      FROM auction_listings l
     WHERE l.seller_user_id BETWEEN $FirstUserId AND $LastUserId
        OR l.final_buyer_user_id BETWEEN $FirstUserId AND $LastUserId
        OR EXISTS
          (
              SELECT 1 FROM auction_bids scope_bid
               WHERE scope_bid.listing_id=l.listing_id
                 AND scope_bid.bidder_user_id BETWEEN $FirstUserId AND $LastUserId
          )
),
relevant_bids AS
(
    SELECT b.*
      FROM auction_bids b
      JOIN relevant_listings l ON l.listing_id=b.listing_id
)
SELECT 'LISTING_INVALID_STATE',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(listing_id ORDER BY listing_id),',',$SampleLimit),'')
  FROM relevant_listings WHERE state NOT IN (1,2,3,4,5,6,7,8,9)
UNION ALL
SELECT 'BID_INVALID_STATE',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(bid_id ORDER BY bid_id),',',$SampleLimit),'')
  FROM relevant_bids WHERE state NOT IN (1,2,3,4,5,6,7)
UNION ALL
SELECT 'LISTING_TRANSIENT_STATE',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(listing_id ORDER BY listing_id),',',$SampleLimit),'')
  FROM relevant_listings WHERE state IN (1,3,4,5,6)
UNION ALL
SELECT 'BID_TRANSIENT_STATE',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(bid_id ORDER BY bid_id),',',$SampleLimit),'')
  FROM relevant_bids WHERE state IN (1,4)
UNION ALL
SELECT 'ACTIVE_HIGHEST_MISMATCH',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(l.listing_id ORDER BY l.listing_id),',',$SampleLimit),'')
  FROM relevant_listings l
  LEFT JOIN auction_bids b ON b.bid_id=l.highest_bid_id
 WHERE l.state=2
   AND ((l.highest_bid_id IS NULL AND EXISTS(SELECT 1 FROM auction_bids hb WHERE hb.listing_id=l.listing_id AND hb.state=2))
        OR (l.highest_bid_id IS NOT NULL AND
            (b.bid_id IS NULL OR b.listing_id<>l.listing_id OR b.state<>2 OR
             b.bidder_user_id<>l.highest_bidder_user_id OR b.bid_amount<>l.current_bid_price OR
             b.currency_id<>l.currency_id)))
UNION ALL
SELECT 'MULTIPLE_HIGHEST_BIDS',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(listing_id ORDER BY listing_id),',',$SampleLimit),'')
  FROM (SELECT b.listing_id FROM relevant_bids b WHERE b.state=2 GROUP BY b.listing_id HAVING COUNT(*)>1) duplicate_highest
UNION ALL
SELECT 'HIGHEST_BID_ON_NON_ACTIVE_LISTING',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(b.bid_id ORDER BY b.bid_id),',',$SampleLimit),'')
  FROM relevant_bids b JOIN relevant_listings l ON l.listing_id=b.listing_id
 WHERE b.state=2 AND l.state<>2
UNION ALL
SELECT 'ACTIVE_FINAL_FIELDS_SET',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(listing_id ORDER BY listing_id),',',$SampleLimit),'')
  FROM relevant_listings
 WHERE state=2 AND (final_buyer_user_id IS NOT NULL OR final_price IS NOT NULL OR sale_type IS NOT NULL)
UNION ALL
SELECT 'SOLD_FINAL_FIELDS_INVALID',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(listing_id ORDER BY listing_id),',',$SampleLimit),'')
  FROM relevant_listings
 WHERE state=7 AND (final_buyer_user_id IS NULL OR final_price IS NULL OR final_price=0 OR sale_type NOT IN (1,2))
UNION ALL
SELECT 'BID_SALE_WINNER_MISMATCH',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(l.listing_id ORDER BY l.listing_id),',',$SampleLimit),'')
  FROM relevant_listings l
 WHERE l.state=7 AND l.sale_type=1
   AND (SELECT COUNT(*) FROM auction_bids b
         WHERE b.listing_id=l.listing_id AND b.state=6 AND
               b.bidder_user_id=l.final_buyer_user_id AND b.bid_amount=l.final_price)<>1
UNION ALL
SELECT 'UNSOLD_TERMINAL_FIELDS_SET',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(listing_id ORDER BY listing_id),',',$SampleLimit),'')
  FROM relevant_listings
 WHERE state IN (8,9) AND
       (highest_bid_id IS NOT NULL OR highest_bidder_user_id IS NOT NULL OR current_bid_price<>0 OR
        final_buyer_user_id IS NOT NULL OR final_price IS NOT NULL OR sale_type IS NOT NULL)
UNION ALL
SELECT 'WON_BID_MISMATCH',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(b.bid_id ORDER BY b.bid_id),',',$SampleLimit),'')
  FROM relevant_bids b JOIN relevant_listings l ON l.listing_id=b.listing_id
 WHERE b.state=6 AND
       (l.state<>7 OR l.final_buyer_user_id<>b.bidder_user_id OR (l.sale_type=1 AND l.final_price<>b.bid_amount))
UNION ALL
SELECT 'ACTIVE_LISTING_LIMIT_EXCEEDED',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(seller_user_id ORDER BY seller_user_id),',',$SampleLimit),'')
  FROM (SELECT seller_user_id FROM relevant_listings WHERE state IN (1,2) GROUP BY seller_user_id HAVING COUNT(*)>$MaxActiveListings) excessive_sellers
UNION ALL
SELECT 'DUPLICATE_ESCROW_ITEM',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(item_instance_id ORDER BY item_instance_id),',',$SampleLimit),'')
  FROM (SELECT item_instance_id FROM relevant_listings WHERE state IN (1,2,3,4,5,6) GROUP BY item_instance_id HAVING COUNT(*)>1) duplicate_items
UNION ALL
SELECT 'ORPHAN_BID',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(b.bid_id ORDER BY b.bid_id),',',$SampleLimit),'')
  FROM auction_bids b LEFT JOIN auction_listings l ON l.listing_id=b.listing_id
 WHERE b.bidder_user_id BETWEEN $FirstUserId AND $LastUserId AND l.listing_id IS NULL;
"@
Add-SqlCheckResults $results (Invoke-MySqlQuery "gameserverportfolio-auction-db-primary" $auctionSql $rootPassword) $auctionDescriptions

$gameDescriptions = @{
    INVENTORY_INVALID_DATA = "Inventory data has an invalid owner, item, quantity, flags, or version."
    CURRENCY_INVALID_DATA = "Currency data has an invalid user, currency, or version."
    MAIL_INVALID_STATE = "Mail state is outside the supported NEW/PARTIAL/CLAIMED states."
    MAIL_WITHOUT_ATTACHMENT = "A mail has no attachment."
    ORPHAN_ATTACHMENT = "A mail attachment references a mail that does not exist."
    ATTACHMENT_INVALID_STATE = "Attachment state is outside CLAIMABLE/CLAIMED."
    ATTACHMENT_CLAIM_TIMESTAMP_MISMATCH = "Attachment state and claimed_at disagree."
    MAIL_STATE_ATTACHMENT_MISMATCH = "Mail state disagrees with its remaining claimable attachments."
    UNCLAIMED_MAIL_ITEM_IN_INVENTORY = "An unclaimed mail item already exists in an inventory."
}

$gameSql = @"
USE gamedb;
SELECT 'INVENTORY_INVALID_DATA',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(item_instance_id ORDER BY item_instance_id),',',$SampleLimit),'')
  FROM inventory_items
 WHERE owner_user_id BETWEEN $FirstUserId AND $LastUserId
   AND (owner_user_id=0 OR item_instance_id=0 OR item_data_id=0 OR quantity=0 OR
        is_equipped NOT IN (0,1) OR is_tradable NOT IN (0,1) OR version=0)
UNION ALL
SELECT 'CURRENCY_INVALID_DATA',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(CONCAT(user_id,':',currency_id) ORDER BY user_id,currency_id),',',$SampleLimit),'')
  FROM player_currencies
 WHERE user_id BETWEEN $FirstUserId AND $LastUserId
   AND (user_id=0 OR currency_id=0 OR version=0)
UNION ALL
SELECT 'MAIL_INVALID_STATE',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(mail_id ORDER BY mail_id),',',$SampleLimit),'')
  FROM mails WHERE receiver_user_id BETWEEN $FirstUserId AND $LastUserId AND state NOT IN (1,2,3)
UNION ALL
SELECT 'MAIL_WITHOUT_ATTACHMENT',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(mail_id ORDER BY mail_id),',',$SampleLimit),'')
  FROM
  (
      SELECT m.mail_id FROM mails m LEFT JOIN mail_attachments a ON a.mail_id=m.mail_id
       WHERE m.receiver_user_id BETWEEN $FirstUserId AND $LastUserId
       GROUP BY m.mail_id HAVING COUNT(a.attachment_id)=0
  ) missing_mails
UNION ALL
SELECT 'ORPHAN_ATTACHMENT',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(a.attachment_id ORDER BY a.attachment_id),',',$SampleLimit),'')
  FROM mail_attachments a LEFT JOIN mails m ON m.mail_id=a.mail_id
 WHERE m.mail_id IS NULL
UNION ALL
SELECT 'ATTACHMENT_INVALID_STATE',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(a.attachment_id ORDER BY a.attachment_id),',',$SampleLimit),'')
  FROM mail_attachments a JOIN mails m ON m.mail_id=a.mail_id
 WHERE m.receiver_user_id BETWEEN $FirstUserId AND $LastUserId AND a.state NOT IN (1,2)
UNION ALL
SELECT 'ATTACHMENT_CLAIM_TIMESTAMP_MISMATCH',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(a.attachment_id ORDER BY a.attachment_id),',',$SampleLimit),'')
  FROM mail_attachments a JOIN mails m ON m.mail_id=a.mail_id
 WHERE m.receiver_user_id BETWEEN $FirstUserId AND $LastUserId
   AND ((a.state=1 AND a.claimed_at IS NOT NULL) OR (a.state=2 AND a.claimed_at IS NULL))
UNION ALL
SELECT 'MAIL_STATE_ATTACHMENT_MISMATCH',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(m.mail_id ORDER BY m.mail_id),',',$SampleLimit),'')
  FROM mails m
 WHERE m.receiver_user_id BETWEEN $FirstUserId AND $LastUserId
   AND ((m.state IN (1,2) AND NOT EXISTS(SELECT 1 FROM mail_attachments a WHERE a.mail_id=m.mail_id AND a.state=1))
        OR (m.state=3 AND EXISTS(SELECT 1 FROM mail_attachments a WHERE a.mail_id=m.mail_id AND a.state=1)))
UNION ALL
SELECT 'UNCLAIMED_MAIL_ITEM_IN_INVENTORY',COUNT(*),COALESCE(SUBSTRING_INDEX(GROUP_CONCAT(a.item_instance_id ORDER BY a.item_instance_id),',',$SampleLimit),'')
  FROM mail_attachments a JOIN mails m ON m.mail_id=a.mail_id
  JOIN inventory_items i ON i.item_instance_id=a.item_instance_id
 WHERE m.receiver_user_id BETWEEN $FirstUserId AND $LastUserId AND a.attachment_type=1 AND a.state=1;
"@
Add-SqlCheckResults $results (Invoke-MySqlQuery "gameserverportfolio-game-db-primary" $gameSql $rootPassword) $gameDescriptions

$relevantCondition = "(l.seller_user_id BETWEEN $FirstUserId AND $LastUserId OR l.final_buyer_user_id BETWEEN $FirstUserId AND $LastUserId OR EXISTS(SELECT 1 FROM auction_bids scope_bid WHERE scope_bid.listing_id=l.listing_id AND scope_bid.bidder_user_id BETWEEN $FirstUserId AND $LastUserId))"
$activeItemSql = "USE auctiondb; SELECT l.listing_id,l.item_instance_id FROM auction_listings l WHERE $relevantCondition AND l.state IN (1,2,3,4,5,6) ORDER BY l.listing_id;"
$activeItemRows = Invoke-MySqlQuery "gameserverportfolio-auction-db-primary" $activeItemSql $rootPassword
$activeItemIds = Convert-ToIdList $activeItemRows 1
$activeInventoryConflicts = [System.Collections.Generic.List[uint64]]::new()
$activeMailConflicts = [System.Collections.Generic.List[uint64]]::new()

for ($offset = 0; $offset -lt $activeItemIds.Count; $offset += 500)
{
    $end = [math]::Min($offset + 499, $activeItemIds.Count - 1)
    $chunk = ($activeItemIds[$offset..$end] | Select-Object -Unique) -join ','
    if ([string]::IsNullOrWhiteSpace($chunk))
    {
        continue
    }
    $inventoryRows = Invoke-MySqlQuery "gameserverportfolio-game-db-primary" `
        "USE gamedb; SELECT item_instance_id FROM inventory_items WHERE item_instance_id IN ($chunk);" $rootPassword
    foreach ($row in $inventoryRows) { $activeInventoryConflicts.Add([uint64]$row) }
    $mailRows = Invoke-MySqlQuery "gameserverportfolio-game-db-primary" `
        "USE gamedb; SELECT item_instance_id FROM mail_attachments WHERE attachment_type=1 AND state=1 AND item_instance_id IN ($chunk);" $rootPassword
    foreach ($row in $mailRows) { $activeMailConflicts.Add([uint64]$row) }
}

$inventorySamples = ($activeInventoryConflicts | Select-Object -Unique -First $SampleLimit) -join ','
$mailSamples = ($activeMailConflicts | Select-Object -Unique -First $SampleLimit) -join ','
Add-CheckResult $results "ESCROW_ITEM_IN_INVENTORY" (($activeInventoryConflicts | Select-Object -Unique).Count) $inventorySamples `
    "An item escrowed by a non-terminal listing also exists in GameDB inventory."
Add-CheckResult $results "ESCROW_ITEM_IN_UNCLAIMED_MAIL" (($activeMailConflicts | Select-Object -Unique).Count) $mailSamples `
    "An item escrowed by a non-terminal listing also exists in an unclaimed GameDB mail."

$terminalSql = "USE auctiondb; SELECT l.listing_id,l.item_instance_id,IF(l.state=7,l.final_buyer_user_id,l.seller_user_id) expected_receiver FROM auction_listings l WHERE $relevantCondition AND l.state IN (7,8,9) ORDER BY l.listing_id;"
$terminalRows = Invoke-MySqlQuery "gameserverportfolio-auction-db-primary" $terminalSql $rootPassword
$terminalItemIds = Convert-ToIdList $terminalRows 1
$mailItemReceivers = @{}
for ($offset = 0; $offset -lt $terminalItemIds.Count; $offset += 500)
{
    $end = [math]::Min($offset + 499, $terminalItemIds.Count - 1)
    $chunk = ($terminalItemIds[$offset..$end] | Select-Object -Unique) -join ','
    if ([string]::IsNullOrWhiteSpace($chunk))
    {
        continue
    }
    $rows = Invoke-MySqlQuery "gameserverportfolio-game-db-primary" `
        "USE gamedb; SELECT a.item_instance_id,m.receiver_user_id FROM mail_attachments a JOIN mails m ON m.mail_id=a.mail_id WHERE a.attachment_type=1 AND a.item_instance_id IN ($chunk);" $rootPassword
    foreach ($row in $rows)
    {
        $values = $row -split "`t"
        $mailItemReceivers["$($values[0]):$($values[1])"] = $true
    }
}

$missingTerminalMailListings = [System.Collections.Generic.List[uint64]]::new()
foreach ($row in $terminalRows)
{
    $values = $row -split "`t"
    if ($values.Count -lt 3 -or -not $mailItemReceivers.ContainsKey("$($values[1]):$($values[2])"))
    {
        $missingTerminalMailListings.Add([uint64]$values[0])
    }
}
$terminalSamples = ($missingTerminalMailListings | Select-Object -Unique -First $SampleLimit) -join ','
Add-CheckResult $results "TERMINAL_LISTING_ITEM_MAIL_MISSING" (($missingTerminalMailListings | Select-Object -Unique).Count) $terminalSamples `
    "A sold/cancelled/expired listing has no item mail for its buyer or seller."

$violations = @($results | Where-Object { -not $_.passed })
$report = [pscustomobject]@{
    checkedAtUtc = [DateTime]::UtcNow.ToString("o")
    firstUserId = $FirstUserId
    lastUserId = $LastUserId
    maxActiveListings = $MaxActiveListings
    passed = ($violations.Count -eq 0)
    checkCount = $results.Count
    violationTypeCount = $violations.Count
    violationRowCount = [uint64](($violations | Measure-Object -Property violationCount -Sum).Sum)
    checks = $results
}

if (-not [string]::IsNullOrWhiteSpace($OutputPath))
{
    $outputDirectory = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($outputDirectory))
    {
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    }
    [System.IO.File]::WriteAllText(
        $OutputPath,
        ($report | ConvertTo-Json -Depth 6),
        [System.Text.UTF8Encoding]::new($false))
}

foreach ($check in $results)
{
    $status = if ($check.passed) { "PASS" } else { "FAIL" }
    $sampleText = if ([string]::IsNullOrWhiteSpace($check.sample)) { "-" } else { $check.sample }
    Write-Host "AUCTION_INTEGRITY_CHECK status=$status name=$($check.name) violations=$($check.violationCount) sample=$sampleText"
}

if ($violations.Count -ne 0)
{
    throw "Auction integrity validation failed. violationTypes=$($violations.Count) violationRows=$($report.violationRowCount)"
}

Write-Host "AUCTION_INTEGRITY_SUCCESS checks=$($results.Count) userRange=$FirstUserId-$LastUserId"

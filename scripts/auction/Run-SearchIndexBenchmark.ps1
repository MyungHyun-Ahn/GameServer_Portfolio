param(
    [int]$RowCount = 100000,
    [int]$QueryIterations = 2000,
    [int]$WriteRows = 5000,
    [int]$Rounds = 6
)

$ErrorActionPreference = "Stop"

if ($RowCount -lt 10000 -or $RowCount -gt 1000000 -or
    $QueryIterations -le 0 -or $WriteRows -le 0 -or $WriteRows -gt ($RowCount / 2) -or
    $Rounds -le 0 -or $Rounds -gt 20)
{
    throw "Benchmark parameters are outside the supported range."
}

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $scriptDirectory)
$envFile = Join-Path $repositoryRoot ".env"
$resultDirectory = Join-Path $repositoryRoot ("Out\AuctionIndexBenchmark\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
$resultFile = Join-Path $resultDirectory "benchmark-results.txt"
$container = "gameserverportfolio-auction-db-primary"

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

if (-not (Test-Path -LiteralPath $envFile))
{
    throw "Create $envFile before running the benchmark."
}

$rootPassword = Get-DotEnvValue "MYSQL_ROOT_PASSWORD"
New-Item -ItemType Directory -Path $resultDirectory -Force | Out-Null

$sql = @"
DROP DATABASE IF EXISTS auction_index_benchmark;
CREATE DATABASE auction_index_benchmark CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE auction_index_benchmark;

CREATE TABLE listings_single LIKE auctiondb.auction_listings;
CREATE TABLE listings_dual LIKE auctiondb.auction_listings;

ALTER TABLE listings_dual
    ADD INDEX idx_listing_active_price_desc (state, effective_price DESC, listing_id DESC),
    ADD INDEX idx_listing_sale_price_desc (state, final_price DESC, listing_id DESC),
    ADD INDEX idx_listing_active_expiring (state, expires_at ASC, listing_id DESC);

INSERT INTO listings_single
(
    listing_id, seller_user_id, seller_login_id, item_instance_id, item_data_id, item_category,
    quantity, item_data, search_name, search_grade, search_enhancement_level,
    search_str, search_dex, search_int, search_luk,
    currency_id, start_price, current_bid_price, buyout_price,
    highest_bid_id, highest_bidder_user_id, final_buyer_user_id, final_price, sale_type,
    state, expires_at, version, created_at, updated_at
)
WITH digits AS
(
    SELECT 0 AS n UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4
    UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9
),
sequence AS
(
    SELECT 1 + a.n + b.n * 10 + c.n * 100 + d.n * 1000 + e.n * 10000 + f.n * 100000 AS n
      FROM digits a CROSS JOIN digits b CROSS JOIN digits c
      CROSS JOIN digits d CROSS JOIN digits e CROSS JOIN digits f
)
SELECT n,
       1000000 + (n % 10000), CONCAT('seller-', n % 10000), 100000000 + n,
       1000 + (n % 100), 1, 1,
       JSON_OBJECT('benchmark', TRUE, 'n', n), CONCAT('Benchmark Item ', n % 100),
       n % 5, n % 20, n % 100, (n * 3) % 100, (n * 7) % 100, (n * 11) % 100,
       1, 1000 + (n % 1000), 0, 5000 + (n % 5000),
       NULL, NULL,
       IF(n % 5 = 0, 2000000 + n, NULL),
       IF(n % 5 = 0, 1000 + (n % 1000), NULL),
       IF(n % 5 = 0, 1, NULL),
       IF(n % 5 = 0, 7, 2),
       TIMESTAMPADD(MICROSECOND, (n % 1000) * 1000,
           TIMESTAMPADD(SECOND, n % 3600, UTC_TIMESTAMP(6) + INTERVAL 1 DAY)),
       1,
       TIMESTAMPADD(SECOND, -(n % 86400), UTC_TIMESTAMP(6)),
       TIMESTAMPADD(MICROSECOND, (n % 1000) * 1000,
           TIMESTAMPADD(SECOND, -(n % 86400), UTC_TIMESTAMP(6)))
  FROM sequence
 WHERE n <= $RowCount
 ORDER BY n;

INSERT INTO listings_dual
(
    listing_id, seller_user_id, seller_login_id, item_instance_id, item_data_id, item_category,
    quantity, item_data, search_name, search_grade, search_enhancement_level,
    search_str, search_dex, search_int, search_luk,
    currency_id, start_price, current_bid_price, buyout_price,
    highest_bid_id, highest_bidder_user_id, final_buyer_user_id, final_price, sale_type,
    state, expires_at, version, created_at, updated_at
)
SELECT listing_id, seller_user_id, seller_login_id, item_instance_id, item_data_id, item_category,
       quantity, item_data, search_name, search_grade, search_enhancement_level,
       search_str, search_dex, search_int, search_luk,
       currency_id, start_price, current_bid_price, buyout_price,
       highest_bid_id, highest_bidder_user_id, final_buyer_user_id, final_price, sale_type,
       state, expires_at, version, created_at, updated_at
  FROM listings_single;
ANALYZE TABLE listings_single, listings_dual;

CREATE TABLE benchmark_results
(
    test_name VARCHAR(32) NOT NULL,
    variant_name VARCHAR(16) NOT NULL,
    round_number INT NOT NULL,
    operation_count INT NOT NULL,
    elapsed_us BIGINT NOT NULL
);

DELIMITER `$
CREATE PROCEDURE run_query_single(IN p_round INT, IN p_iterations INT)
BEGIN
    DECLARE v_index INT DEFAULT 0;
    DECLARE v_sink DECIMAL(65,0) DEFAULT 0;
    DECLARE v_started DATETIME(6);

    SET v_started = NOW(6);
    WHILE v_index < p_iterations DO
        SELECT COALESCE(SUM(listing_id), 0) INTO v_sink
          FROM
          (
              SELECT listing_id FROM listings_single FORCE INDEX (idx_listing_active_price)
               WHERE state = 2
               ORDER BY effective_price ASC, listing_id DESC LIMIT 20
          ) rows_for_page;
        SET v_index = v_index + 1;
    END WHILE;
    INSERT INTO benchmark_results VALUES
        ('price_asc', 'single', p_round, p_iterations, TIMESTAMPDIFF(MICROSECOND, v_started, NOW(6)));

    SET v_index = 0;
    SET v_started = NOW(6);
    WHILE v_index < p_iterations DO
        SELECT COALESCE(SUM(listing_id), 0) INTO v_sink
          FROM
          (
              SELECT listing_id FROM listings_single FORCE INDEX (idx_listing_active_price)
               WHERE state = 2
               ORDER BY effective_price DESC, listing_id ASC LIMIT 20
          ) rows_for_page;
        SET v_index = v_index + 1;
    END WHILE;
    INSERT INTO benchmark_results VALUES
        ('price_desc', 'single', p_round, p_iterations, TIMESTAMPDIFF(MICROSECOND, v_started, NOW(6)));

    SET v_index = 0;
    SET v_started = NOW(6);
    WHILE v_index < p_iterations DO
        SELECT COALESCE(SUM(listing_id), 0) INTO v_sink
          FROM
          (
              SELECT listing_id FROM listings_single FORCE INDEX (idx_listing_expiration)
               WHERE state = 2 AND expires_at > UTC_TIMESTAMP(6)
               ORDER BY expires_at ASC, listing_id ASC LIMIT 20
          ) rows_for_page;
        SET v_index = v_index + 1;
    END WHILE;
    INSERT INTO benchmark_results VALUES
        ('expiring', 'single', p_round, p_iterations, TIMESTAMPDIFF(MICROSECOND, v_started, NOW(6)));
END`$

CREATE PROCEDURE run_query_dual(IN p_round INT, IN p_iterations INT)
BEGIN
    DECLARE v_index INT DEFAULT 0;
    DECLARE v_sink DECIMAL(65,0) DEFAULT 0;
    DECLARE v_started DATETIME(6);

    SET v_started = NOW(6);
    WHILE v_index < p_iterations DO
        SELECT COALESCE(SUM(listing_id), 0) INTO v_sink
          FROM
          (
              SELECT listing_id FROM listings_dual FORCE INDEX (idx_listing_active_price)
               WHERE state = 2
               ORDER BY effective_price ASC, listing_id DESC LIMIT 20
          ) rows_for_page;
        SET v_index = v_index + 1;
    END WHILE;
    INSERT INTO benchmark_results VALUES
        ('price_asc', 'dual', p_round, p_iterations, TIMESTAMPDIFF(MICROSECOND, v_started, NOW(6)));

    SET v_index = 0;
    SET v_started = NOW(6);
    WHILE v_index < p_iterations DO
        SELECT COALESCE(SUM(listing_id), 0) INTO v_sink
          FROM
          (
              SELECT listing_id FROM listings_dual FORCE INDEX (idx_listing_active_price_desc)
               WHERE state = 2
               ORDER BY effective_price DESC, listing_id DESC LIMIT 20
          ) rows_for_page;
        SET v_index = v_index + 1;
    END WHILE;
    INSERT INTO benchmark_results VALUES
        ('price_desc', 'dual', p_round, p_iterations, TIMESTAMPDIFF(MICROSECOND, v_started, NOW(6)));

    SET v_index = 0;
    SET v_started = NOW(6);
    WHILE v_index < p_iterations DO
        SELECT COALESCE(SUM(listing_id), 0) INTO v_sink
          FROM
          (
              SELECT listing_id FROM listings_dual FORCE INDEX (idx_listing_active_expiring)
               WHERE state = 2 AND expires_at > UTC_TIMESTAMP(6)
               ORDER BY expires_at ASC, listing_id DESC LIMIT 20
          ) rows_for_page;
        SET v_index = v_index + 1;
    END WHILE;
    INSERT INTO benchmark_results VALUES
        ('expiring', 'dual', p_round, p_iterations, TIMESTAMPDIFF(MICROSECOND, v_started, NOW(6)));
END`$

CREATE PROCEDURE run_write_single(IN p_round INT, IN p_write_rows INT)
BEGIN
    DECLARE v_started DATETIME(6);
    SET v_started = NOW(6);
    START TRANSACTION;
    UPDATE listings_single
       SET current_bid_price = IF(current_bid_price = 0, start_price + 1, current_bid_price + 1),
           highest_bid_id = listing_id,
           highest_bidder_user_id = seller_user_id,
           version = version + 1
     WHERE listing_id <= p_write_rows AND state = 2;
    COMMIT;
    INSERT INTO benchmark_results VALUES
        ('bid_price_update', 'single', p_round, p_write_rows, TIMESTAMPDIFF(MICROSECOND, v_started, NOW(6)));
END`$

CREATE PROCEDURE run_write_dual(IN p_round INT, IN p_write_rows INT)
BEGIN
    DECLARE v_started DATETIME(6);
    SET v_started = NOW(6);
    START TRANSACTION;
    UPDATE listings_dual
       SET current_bid_price = IF(current_bid_price = 0, start_price + 1, current_bid_price + 1),
           highest_bid_id = listing_id,
           highest_bidder_user_id = seller_user_id,
           version = version + 1
     WHERE listing_id <= p_write_rows AND state = 2;
    COMMIT;
    INSERT INTO benchmark_results VALUES
        ('bid_price_update', 'dual', p_round, p_write_rows, TIMESTAMPDIFF(MICROSECOND, v_started, NOW(6)));
END`$

CREATE PROCEDURE run_all(IN p_rounds INT, IN p_query_iterations INT, IN p_write_rows INT)
BEGIN
    DECLARE v_round INT DEFAULT 1;
    WHILE v_round <= p_rounds DO
        IF MOD(v_round, 2) = 1 THEN
            CALL run_query_dual(v_round, p_query_iterations);
            CALL run_query_single(v_round, p_query_iterations);
            CALL run_write_dual(v_round, p_write_rows);
            CALL run_write_single(v_round, p_write_rows);
        ELSE
            CALL run_query_single(v_round, p_query_iterations);
            CALL run_query_dual(v_round, p_query_iterations);
            CALL run_write_single(v_round, p_write_rows);
            CALL run_write_dual(v_round, p_write_rows);
        END IF;
        SET v_round = v_round + 1;
    END WHILE;
END`$
DELIMITER ;

CALL run_all($Rounds, $QueryIterations, $WriteRows);
ANALYZE TABLE listings_single, listings_dual;

SELECT test_name, variant_name, round_number, operation_count, elapsed_us,
       ROUND(elapsed_us / operation_count / 1000, 6) AS ms_per_operation
  FROM benchmark_results
 ORDER BY test_name, round_number, variant_name;

SELECT test_name,
       variant_name,
       COUNT(*) AS samples,
       SUM(operation_count) AS operations,
       ROUND(SUM(elapsed_us) / SUM(operation_count) / 1000, 6) AS average_ms_per_operation,
       ROUND(MIN(elapsed_us / operation_count) / 1000, 6) AS best_round_ms_per_operation,
       ROUND(MAX(elapsed_us / operation_count) / 1000, 6) AS worst_round_ms_per_operation
  FROM benchmark_results
 GROUP BY test_name, variant_name
 ORDER BY test_name, variant_name;

SELECT table_name, table_rows, data_length, index_length,
       ROUND(index_length / 1024 / 1024, 2) AS index_mib
  FROM information_schema.tables
 WHERE table_schema = 'auction_index_benchmark'
   AND table_name IN ('listings_single', 'listings_dual')
 ORDER BY table_name;
"@

try
{
    $output = $sql | docker exec -i -e "MYSQL_PWD=$rootPassword" $container mysql -uroot --batch --raw 2>&1
    if ($LASTEXITCODE -ne 0)
    {
        throw "Index benchmark SQL failed.`n$output"
    }
    $header = @(
        "Auction search index benchmark",
        "created_at=$(Get-Date -Format o)",
        "row_count=$RowCount",
        "query_iterations=$QueryIterations",
        "write_rows=$WriteRows",
        "rounds=$Rounds",
        ""
    ) -join [Environment]::NewLine
    [System.IO.File]::WriteAllText(
        $resultFile,
        $header + ($output -join [Environment]::NewLine),
        [System.Text.UTF8Encoding]::new($false))
    $output
    Write-Host "Benchmark result: $resultFile"
}
finally
{
    "DROP DATABASE IF EXISTS auction_index_benchmark;" |
        docker exec -i -e "MYSQL_PWD=$rootPassword" $container mysql -uroot 2>$null | Out-Null
}

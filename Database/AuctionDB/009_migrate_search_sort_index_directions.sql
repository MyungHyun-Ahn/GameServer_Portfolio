USE auctiondb;

DROP PROCEDURE IF EXISTS sp_ad_migrate_search_sort_index_directions;
DELIMITER $$
CREATE PROCEDURE sp_ad_migrate_search_sort_index_directions()
BEGIN
    IF
    (
        SELECT COALESCE
        (
            GROUP_CONCAT(CONCAT(column_name, ':', collation) ORDER BY seq_in_index SEPARATOR ','),
            ''
        )
          FROM information_schema.statistics
         WHERE table_schema = DATABASE()
           AND table_name = 'auction_listings'
           AND index_name = 'idx_listing_active_price'
    ) <> 'state:A,effective_price:A,listing_id:D' THEN
        ALTER TABLE auction_listings DROP INDEX idx_listing_active_price;
        ALTER TABLE auction_listings
            ADD INDEX idx_listing_active_price (state, effective_price ASC, listing_id DESC);
    END IF;

    IF EXISTS
    (
        SELECT 1 FROM information_schema.statistics
         WHERE table_schema = DATABASE()
           AND table_name = 'auction_listings'
           AND index_name = 'idx_listing_active_price_desc'
    ) THEN
        ALTER TABLE auction_listings DROP INDEX idx_listing_active_price_desc;
    END IF;

    IF
    (
        SELECT COALESCE
        (
            GROUP_CONCAT(CONCAT(column_name, ':', collation) ORDER BY seq_in_index SEPARATOR ','),
            ''
        )
          FROM information_schema.statistics
         WHERE table_schema = DATABASE()
           AND table_name = 'auction_listings'
           AND index_name = 'idx_listing_sale_price'
    ) <> 'state:A,final_price:A,listing_id:D' THEN
        ALTER TABLE auction_listings DROP INDEX idx_listing_sale_price;
        ALTER TABLE auction_listings
            ADD INDEX idx_listing_sale_price (state, final_price ASC, listing_id DESC);
    END IF;

    IF EXISTS
    (
        SELECT 1 FROM information_schema.statistics
         WHERE table_schema = DATABASE()
           AND table_name = 'auction_listings'
           AND index_name = 'idx_listing_sale_price_desc'
    ) THEN
        ALTER TABLE auction_listings DROP INDEX idx_listing_sale_price_desc;
    END IF;

    IF EXISTS
    (
        SELECT 1 FROM information_schema.statistics
         WHERE table_schema = DATABASE()
           AND table_name = 'auction_listings'
           AND index_name = 'idx_listing_active_expiring'
    ) THEN
        ALTER TABLE auction_listings DROP INDEX idx_listing_active_expiring;
    END IF;
END$$
DELIMITER ;

CALL sp_ad_migrate_search_sort_index_directions();
DROP PROCEDURE sp_ad_migrate_search_sort_index_directions;

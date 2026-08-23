USE auctiondb;

DROP PROCEDURE IF EXISTS sp_ad_migrate_search_sorting;
DELIMITER $$
CREATE PROCEDURE sp_ad_migrate_search_sorting()
BEGIN
    IF NOT EXISTS
    (
        SELECT 1 FROM information_schema.columns
         WHERE table_schema = DATABASE()
           AND table_name = 'auction_listings'
           AND column_name = 'effective_price'
    ) THEN
        ALTER TABLE auction_listings
            ADD COLUMN effective_price BIGINT UNSIGNED GENERATED ALWAYS AS
            (
                CASE WHEN current_bid_price = 0 THEN start_price ELSE current_bid_price END
            ) STORED AFTER current_bid_price;
    END IF;

    IF NOT EXISTS
    (
        SELECT 1 FROM information_schema.statistics
         WHERE table_schema = DATABASE()
           AND table_name = 'auction_listings'
           AND index_name = 'idx_listing_active_price'
    ) THEN
        ALTER TABLE auction_listings
            ADD INDEX idx_listing_active_price (state, effective_price, listing_id);
    END IF;

    IF NOT EXISTS
    (
        SELECT 1 FROM information_schema.statistics
         WHERE table_schema = DATABASE()
           AND table_name = 'auction_listings'
           AND index_name = 'idx_listing_sale_price'
    ) THEN
        ALTER TABLE auction_listings
            ADD INDEX idx_listing_sale_price (state, final_price, listing_id);
    END IF;
END$$
DELIMITER ;

CALL sp_ad_migrate_search_sorting();
DROP PROCEDURE sp_ad_migrate_search_sorting;

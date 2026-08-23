USE auctiondb;

DROP PROCEDURE IF EXISTS sp_ad_migrate_seller_login_id;
DELIMITER $$
CREATE PROCEDURE sp_ad_migrate_seller_login_id()
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_schema = 'auctiondb' AND table_name = 'auction_listings'
                     AND column_name = 'seller_login_id') THEN
        ALTER TABLE auction_listings
            ADD COLUMN seller_login_id VARCHAR(64) NOT NULL DEFAULT '' AFTER seller_user_id;
        UPDATE auction_listings
           SET seller_login_id = CONCAT('user-', seller_user_id)
         WHERE seller_login_id = '';
        ALTER TABLE auction_listings
            ALTER COLUMN seller_login_id DROP DEFAULT;
    END IF;

    IF NOT EXISTS (SELECT 1 FROM information_schema.statistics
                   WHERE table_schema = 'auctiondb' AND table_name = 'auction_listings'
                     AND index_name = 'idx_listing_search_name') THEN
        ALTER TABLE auction_listings
            ADD INDEX idx_listing_search_name (state, search_name, listing_id);
    END IF;
END$$
DELIMITER ;

CALL sp_ad_migrate_seller_login_id();
DROP PROCEDURE sp_ad_migrate_seller_login_id;

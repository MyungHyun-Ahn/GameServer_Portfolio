USE auctiondb;

DROP PROCEDURE IF EXISTS sp_ad_migrate_item_search_columns;
DELIMITER $$
CREATE PROCEDURE sp_ad_migrate_item_search_columns()
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_schema='auctiondb' AND table_name='auction_listings'
                     AND column_name='listing_id' AND extra LIKE '%auto_increment%') THEN
        ALTER TABLE auction_listings
            MODIFY COLUMN listing_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT;
    END IF;
    IF EXISTS (SELECT 1 FROM information_schema.columns
               WHERE table_schema='auctiondb' AND table_name='auction_listings' AND column_name='item_template_id')
       AND NOT EXISTS (SELECT 1 FROM information_schema.columns
                       WHERE table_schema='auctiondb' AND table_name='auction_listings' AND column_name='item_data_id') THEN
        ALTER TABLE auction_listings RENAME COLUMN item_template_id TO item_data_id;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_schema='auctiondb' AND table_name='auction_bids'
                     AND column_name='bid_id' AND extra LIKE '%auto_increment%') THEN
        ALTER TABLE auction_bids
            MODIFY COLUMN bid_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_schema='auctiondb' AND table_name='auction_listings' AND column_name='item_category') THEN
        ALTER TABLE auction_listings ADD COLUMN item_category TINYINT UNSIGNED NOT NULL DEFAULT 3 AFTER item_data_id;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_schema='auctiondb' AND table_name='auction_listings' AND column_name='search_str') THEN
        ALTER TABLE auction_listings
            ADD COLUMN search_str INT UNSIGNED NOT NULL DEFAULT 0 AFTER search_enhancement_level,
            ADD COLUMN search_dex INT UNSIGNED NOT NULL DEFAULT 0 AFTER search_str,
            ADD COLUMN search_int INT UNSIGNED NOT NULL DEFAULT 0 AFTER search_dex,
            ADD COLUMN search_luk INT UNSIGNED NOT NULL DEFAULT 0 AFTER search_int;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM information_schema.statistics
                   WHERE table_schema='auctiondb' AND table_name='auction_listings' AND index_name='idx_listing_search_str') THEN
        ALTER TABLE auction_listings
            ADD INDEX idx_listing_search_str (state, item_category, search_str, listing_id),
            ADD INDEX idx_listing_search_dex (state, item_category, search_dex, listing_id),
            ADD INDEX idx_listing_search_int (state, item_category, search_int, listing_id),
            ADD INDEX idx_listing_search_luk (state, item_category, search_luk, listing_id);
    END IF;
    IF EXISTS (SELECT 1 FROM information_schema.statistics
               WHERE table_schema='auctiondb' AND table_name='auction_listings' AND index_name='idx_listing_search_template')
       AND NOT EXISTS (SELECT 1 FROM information_schema.statistics
                       WHERE table_schema='auctiondb' AND table_name='auction_listings' AND index_name='idx_listing_search_item_data') THEN
        ALTER TABLE auction_listings RENAME INDEX idx_listing_search_template TO idx_listing_search_item_data;
    END IF;
    IF EXISTS (SELECT 1 FROM information_schema.statistics
               WHERE table_schema='auctiondb' AND table_name='auction_listings' AND index_name='idx_listing_search_name') THEN
        ALTER TABLE auction_listings DROP INDEX idx_listing_search_name;
    END IF;
	IF EXISTS (SELECT 1 FROM information_schema.statistics
	           WHERE table_schema='auctiondb' AND table_name='auction_listings' AND index_name='uq_listing_item') THEN
		ALTER TABLE auction_listings DROP INDEX uq_listing_item;
	END IF;
	IF NOT EXISTS (SELECT 1 FROM information_schema.statistics
	               WHERE table_schema='auctiondb' AND table_name='auction_listings' AND index_name='idx_listing_item_history') THEN
		ALTER TABLE auction_listings ADD INDEX idx_listing_item_history (item_instance_id, listing_id);
	END IF;
	IF NOT EXISTS (SELECT 1 FROM information_schema.statistics
	               WHERE table_schema='auctiondb' AND table_name='auction_listings' AND index_name='idx_listing_sale_history') THEN
		ALTER TABLE auction_listings
			ADD INDEX idx_listing_sale_history (state, item_data_id, updated_at DESC, listing_id DESC);
	END IF;
END$$
DELIMITER ;

CALL sp_ad_migrate_item_search_columns();
DROP PROCEDURE sp_ad_migrate_item_search_columns;

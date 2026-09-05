CREATE DATABASE IF NOT EXISTS auctiondb
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE auctiondb;

CREATE TABLE IF NOT EXISTS auction_listings
(
    listing_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    seller_user_id BIGINT UNSIGNED NOT NULL,
    seller_login_id VARCHAR(64) NOT NULL,
    item_instance_id BIGINT UNSIGNED NOT NULL,
    item_data_id INT UNSIGNED NOT NULL,
    item_category TINYINT UNSIGNED NOT NULL,
    quantity INT UNSIGNED NOT NULL,
    item_data JSON NOT NULL,
    search_name VARCHAR(100) NOT NULL,
    search_grade SMALLINT UNSIGNED NOT NULL,
    search_enhancement_level SMALLINT UNSIGNED NOT NULL,
    search_str INT UNSIGNED NOT NULL DEFAULT 0,
    search_dex INT UNSIGNED NOT NULL DEFAULT 0,
    search_int INT UNSIGNED NOT NULL DEFAULT 0,
    search_luk INT UNSIGNED NOT NULL DEFAULT 0,
    currency_id SMALLINT UNSIGNED NOT NULL,
    start_price BIGINT UNSIGNED NOT NULL,
    current_bid_price BIGINT UNSIGNED NOT NULL DEFAULT 0,
    effective_price BIGINT UNSIGNED GENERATED ALWAYS AS
    (
        CASE WHEN current_bid_price = 0 THEN start_price ELSE current_bid_price END
    ) STORED,
    buyout_price BIGINT UNSIGNED NULL,
    highest_bid_id BIGINT UNSIGNED NULL,
    highest_bidder_user_id BIGINT UNSIGNED NULL,
    final_buyer_user_id BIGINT UNSIGNED NULL,
    final_price BIGINT UNSIGNED NULL,
    sale_type TINYINT UNSIGNED NULL,
    state TINYINT UNSIGNED NOT NULL,
    expires_at DATETIME(6) NOT NULL,
    version BIGINT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (listing_id),
	INDEX idx_listing_item_history (item_instance_id, listing_id),
    INDEX idx_listing_search_item_data (state, item_data_id, current_bid_price, listing_id),
    INDEX idx_listing_active_price (state, effective_price ASC, listing_id DESC),
    INDEX idx_listing_search_grade (state, search_grade, search_enhancement_level, listing_id),
    INDEX idx_listing_search_str (state, item_category, search_str, listing_id),
    INDEX idx_listing_search_dex (state, item_category, search_dex, listing_id),
    INDEX idx_listing_search_int (state, item_category, search_int, listing_id),
    INDEX idx_listing_search_luk (state, item_category, search_luk, listing_id),
    INDEX idx_listing_seller (seller_user_id, created_at DESC, listing_id DESC),
    INDEX idx_listing_sale_history (state, item_data_id, updated_at DESC, listing_id DESC),
    INDEX idx_listing_sale_price (state, final_price ASC, listing_id DESC),
    INDEX idx_listing_highest_bidder (highest_bidder_user_id, state, updated_at DESC),
    INDEX idx_listing_expiration (state, expires_at, listing_id),
    CONSTRAINT chk_listing_quantity CHECK (quantity > 0),
    CONSTRAINT chk_listing_category CHECK (item_category IN (1, 2, 3)),
    CONSTRAINT chk_listing_non_equipment_stats CHECK
    (
        item_category = 1
        OR (search_str = 0 AND search_dex = 0 AND search_int = 0 AND search_luk = 0)
    ),
    CONSTRAINT chk_listing_start_price CHECK (start_price > 0),
    CONSTRAINT chk_listing_buyout CHECK (buyout_price IS NULL OR buyout_price >= start_price),
    CONSTRAINT chk_listing_highest_bid CHECK
    (
        (highest_bid_id IS NULL AND highest_bidder_user_id IS NULL AND current_bid_price = 0)
        OR
        (highest_bid_id IS NOT NULL AND highest_bidder_user_id IS NOT NULL AND current_bid_price > 0)
    ),
    CONSTRAINT chk_listing_final_sale CHECK
    (
        (final_buyer_user_id IS NULL AND final_price IS NULL AND sale_type IS NULL)
        OR
        (final_buyer_user_id IS NOT NULL AND final_price > 0 AND sale_type IS NOT NULL)
    )
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS auction_bids
(
    bid_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    listing_id BIGINT UNSIGNED NOT NULL,
    bidder_user_id BIGINT UNSIGNED NOT NULL,
    currency_id SMALLINT UNSIGNED NOT NULL,
    bid_amount BIGINT UNSIGNED NOT NULL,
    state TINYINT UNSIGNED NOT NULL,
    version BIGINT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (bid_id),
    UNIQUE INDEX uq_bid_listing_bidder (listing_id, bidder_user_id),
    INDEX idx_bid_listing_state (listing_id, state, bid_id),
    INDEX idx_bid_bidder_state (bidder_user_id, state, updated_at DESC, bid_id),
    CONSTRAINT chk_bid_amount CHECK (bid_amount > 0)
) ENGINE = InnoDB;

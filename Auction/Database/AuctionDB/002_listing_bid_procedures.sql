USE auctiondb;

DROP PROCEDURE IF EXISTS sp_ad_c_listing_prepare;
DROP PROCEDURE IF EXISTS sp_ad_u_listing_activate;
DROP PROCEDURE IF EXISTS sp_ad_d_listing_pending;
DROP PROCEDURE IF EXISTS sp_ad_r_my_bids;
DROP PROCEDURE IF EXISTS sp_ad_r_listings;
DROP PROCEDURE IF EXISTS sp_ad_r_listing_detail;
DROP PROCEDURE IF EXISTS sp_ad_r_sale_history;
DROP PROCEDURE IF EXISTS sp_ad_r_sale_history_detail;
DROP PROCEDURE IF EXISTS sp_ad_cu_bid_prepare;
DROP PROCEDURE IF EXISTS sp_ad_u_bid_complete;
DROP PROCEDURE IF EXISTS sp_ad_r_outbid_claimable;
DROP PROCEDURE IF EXISTS sp_ad_cu_buyout_prepare;
DROP PROCEDURE IF EXISTS sp_ad_u_buyout_complete;
DROP PROCEDURE IF EXISTS sp_ad_u_cancel_prepare;
DROP PROCEDURE IF EXISTS sp_ad_u_cancel_complete;
DROP PROCEDURE IF EXISTS sp_ad_r_expired_listing_candidates;
DROP PROCEDURE IF EXISTS sp_ad_u_expire_prepare;
DROP PROCEDURE IF EXISTS sp_ad_u_expire_complete;
DROP PROCEDURE IF EXISTS sp_ad_u_bid_refund_prepare;
DROP PROCEDURE IF EXISTS sp_ad_u_bid_refund_complete;
DROP PROCEDURE IF EXISTS sp_ad_u_bid_refund_revert;

DELIMITER $$

CREATE PROCEDURE sp_ad_c_listing_prepare
(
    IN p_seller_user_id BIGINT UNSIGNED,
    IN p_seller_login_id VARCHAR(64),
    IN p_item_instance_id BIGINT UNSIGNED,
    IN p_item_data_id INT UNSIGNED,
    IN p_item_category TINYINT UNSIGNED,
    IN p_quantity INT UNSIGNED,
    IN p_item_data JSON,
    IN p_search_name VARCHAR(100),
    IN p_search_grade SMALLINT UNSIGNED,
    IN p_search_enhancement_level SMALLINT UNSIGNED,
    IN p_search_str INT UNSIGNED,
    IN p_search_dex INT UNSIGNED,
    IN p_search_int INT UNSIGNED,
    IN p_search_luk INT UNSIGNED,
    IN p_currency_id SMALLINT UNSIGNED,
    IN p_start_price BIGINT UNSIGNED,
    IN p_buyout_price BIGINT UNSIGNED,
    IN p_expires_at DATETIME(6),
	IN p_max_active_listings INT UNSIGNED
)
BEGIN
	DECLARE v_active_listing_count INT UNSIGNED DEFAULT 0;

    IF p_seller_user_id = 0 OR CHAR_LENGTH(TRIM(p_seller_login_id)) = 0 OR
       p_item_instance_id = 0 OR p_item_data_id = 0 OR
       p_quantity = 0 OR p_start_price = 0 OR
       (p_buyout_price IS NOT NULL AND p_buyout_price < p_start_price) OR
       p_expires_at <= UTC_TIMESTAMP(6) OR p_max_active_listings = 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_LISTING';
    END IF;

	-- Registration requests for the same authenticated user are serialized by
	-- UserId routing and the Command Content mailbox. Keep this read non-locking:
	-- locking an empty seller range makes unrelated new sellers contend on the
	-- same idx_listing_seller supremum gap before INSERT.
	SELECT COUNT(*)
	  INTO v_active_listing_count
	  FROM auction_listings
	 WHERE seller_user_id = p_seller_user_id
	   AND state IN (1, 2);

	IF v_active_listing_count >= p_max_active_listings THEN
		SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'LISTING_LIMIT_EXCEEDED';
	END IF;

    INSERT INTO auction_listings
    (
        seller_user_id, seller_login_id, item_instance_id, item_data_id, item_category,
        quantity, item_data, search_name, search_grade, search_enhancement_level,
        search_str, search_dex, search_int, search_luk,
        currency_id, start_price, buyout_price, state, expires_at
    )
    VALUES
    (
        p_seller_user_id, p_seller_login_id, p_item_instance_id, p_item_data_id, p_item_category,
        p_quantity, p_item_data, p_search_name, p_search_grade, p_search_enhancement_level,
        p_search_str, p_search_dex, p_search_int, p_search_luk,
        p_currency_id, p_start_price, p_buyout_price, 1, p_expires_at
    );

    SELECT LAST_INSERT_ID() AS listing_id, 1 AS version;
END$$

CREATE PROCEDURE sp_ad_u_listing_activate
(
    IN p_listing_id BIGINT UNSIGNED,
    IN p_expected_version BIGINT UNSIGNED
)
BEGIN
    UPDATE auction_listings
       SET state = 2,
           version = version + 1
     WHERE listing_id = p_listing_id
       AND state = 1
       AND version = p_expected_version;

    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CONCURRENT_MODIFICATION';
    END IF;
END$$

CREATE PROCEDURE sp_ad_r_my_bids
(
    IN p_bidder_user_id BIGINT UNSIGNED,
    IN p_cursor_bid_id BIGINT UNSIGNED,
    IN p_limit INT UNSIGNED
)
BEGIN
    SELECT b.bid_id,
           b.listing_id,
           b.currency_id,
           b.bid_amount,
           b.state,
           b.version,
           l.item_data_id,
           l.item_data,
           l.current_bid_price,
           l.highest_bidder_user_id,
           l.state AS listing_state,
           l.expires_at
      FROM auction_bids AS b
      JOIN auction_listings AS l ON l.listing_id = b.listing_id
     WHERE b.bidder_user_id = p_bidder_user_id
       AND (p_cursor_bid_id = 0 OR b.bid_id < p_cursor_bid_id)
     ORDER BY b.bid_id DESC
     LIMIT p_limit;
END$$

CREATE PROCEDURE sp_ad_r_listings
(
    IN p_item_category TINYINT UNSIGNED,
    IN p_item_data_ids JSON,
    IN p_min_str INT UNSIGNED,
    IN p_min_dex INT UNSIGNED,
    IN p_min_int INT UNSIGNED,
    IN p_min_luk INT UNSIGNED,
    IN p_seller_user_id BIGINT UNSIGNED,
    IN p_sort_type TINYINT UNSIGNED,
    IN p_cursor_sort_value BIGINT UNSIGNED,
    IN p_cursor_listing_id BIGINT UNSIGNED,
    IN p_limit INT UNSIGNED
)
BEGIN
    DECLARE v_cursor_time DATETIME(6);

    IF p_item_category NOT IN (0, 1, 2, 3) OR p_sort_type NOT IN (1, 2, 3, 4) OR p_limit = 0 OR p_limit > 100 OR
       p_item_data_ids IS NULL OR JSON_TYPE(p_item_data_ids) <> 'ARRAY' OR JSON_LENGTH(p_item_data_ids) > 100 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_LISTING_SEARCH';
    END IF;

    IF p_sort_type = 4 AND p_cursor_listing_id <> 0 THEN
        SET v_cursor_time = COALESCE
        (
            (SELECT expires_at FROM auction_listings WHERE listing_id = p_cursor_listing_id),
            FROM_UNIXTIME(p_cursor_sort_value / 1000.0)
        );
    END IF;

    IF p_sort_type = 1 THEN
        SELECT listing_id, seller_user_id, seller_login_id, item_data_id, item_category, quantity,
               search_name, search_str, search_dex, search_int, search_luk,
               currency_id, start_price, current_bid_price, buyout_price,
               CAST(UNIX_TIMESTAMP(expires_at) * 1000 AS UNSIGNED) AS expires_at_unix_ms,
               version
          FROM auction_listings
         WHERE state = 2
           AND expires_at > UTC_TIMESTAMP(6)
           AND (p_item_category = 0 OR item_category = p_item_category)
           AND (JSON_LENGTH(p_item_data_ids) = 0 OR item_data_id IN
               (
                   SELECT filter_ids.item_data_id
                     FROM JSON_TABLE(p_item_data_ids, '$[*]'
                          COLUMNS(item_data_id INT UNSIGNED PATH '$')) AS filter_ids
               ))
           AND search_str >= p_min_str
           AND search_dex >= p_min_dex
           AND search_int >= p_min_int
           AND search_luk >= p_min_luk
           AND (p_seller_user_id = 0 OR seller_user_id = p_seller_user_id)
           AND (p_cursor_listing_id = 0 OR listing_id < p_cursor_listing_id)
         ORDER BY listing_id DESC
         LIMIT p_limit;
    ELSEIF p_sort_type = 2 THEN
        SELECT listing_id, seller_user_id, seller_login_id, item_data_id, item_category, quantity,
               search_name, search_str, search_dex, search_int, search_luk,
               currency_id, start_price, current_bid_price, buyout_price,
               CAST(UNIX_TIMESTAMP(expires_at) * 1000 AS UNSIGNED) AS expires_at_unix_ms,
               version
          FROM auction_listings
         WHERE state = 2
           AND expires_at > UTC_TIMESTAMP(6)
           AND (p_item_category = 0 OR item_category = p_item_category)
           AND (JSON_LENGTH(p_item_data_ids) = 0 OR item_data_id IN
               (
                   SELECT filter_ids.item_data_id
                     FROM JSON_TABLE(p_item_data_ids, '$[*]'
                          COLUMNS(item_data_id INT UNSIGNED PATH '$')) AS filter_ids
               ))
           AND search_str >= p_min_str
           AND search_dex >= p_min_dex
           AND search_int >= p_min_int
           AND search_luk >= p_min_luk
           AND (p_seller_user_id = 0 OR seller_user_id = p_seller_user_id)
           AND
           (
               p_cursor_listing_id = 0
               OR effective_price > p_cursor_sort_value
               OR (effective_price = p_cursor_sort_value AND listing_id < p_cursor_listing_id)
           )
         ORDER BY effective_price ASC, listing_id DESC
         LIMIT p_limit;
    ELSEIF p_sort_type = 3 THEN
        SELECT listing_id, seller_user_id, seller_login_id, item_data_id, item_category, quantity,
               search_name, search_str, search_dex, search_int, search_luk,
               currency_id, start_price, current_bid_price, buyout_price,
               CAST(UNIX_TIMESTAMP(expires_at) * 1000 AS UNSIGNED) AS expires_at_unix_ms,
               version
          FROM auction_listings
         WHERE state = 2
           AND expires_at > UTC_TIMESTAMP(6)
           AND (p_item_category = 0 OR item_category = p_item_category)
           AND (JSON_LENGTH(p_item_data_ids) = 0 OR item_data_id IN
               (
                   SELECT filter_ids.item_data_id
                     FROM JSON_TABLE(p_item_data_ids, '$[*]'
                          COLUMNS(item_data_id INT UNSIGNED PATH '$')) AS filter_ids
               ))
           AND search_str >= p_min_str
           AND search_dex >= p_min_dex
           AND search_int >= p_min_int
           AND search_luk >= p_min_luk
           AND (p_seller_user_id = 0 OR seller_user_id = p_seller_user_id)
           AND
           (
               p_cursor_listing_id = 0
               OR effective_price < p_cursor_sort_value
               OR (effective_price = p_cursor_sort_value AND listing_id > p_cursor_listing_id)
           )
         ORDER BY effective_price DESC, listing_id ASC
         LIMIT p_limit;
    ELSE
        SELECT listing_id, seller_user_id, seller_login_id, item_data_id, item_category, quantity,
               search_name, search_str, search_dex, search_int, search_luk,
               currency_id, start_price, current_bid_price, buyout_price,
               CAST(UNIX_TIMESTAMP(expires_at) * 1000 AS UNSIGNED) AS expires_at_unix_ms,
               version
          FROM auction_listings
         WHERE state = 2
           AND expires_at > UTC_TIMESTAMP(6)
           AND (p_item_category = 0 OR item_category = p_item_category)
           AND (JSON_LENGTH(p_item_data_ids) = 0 OR item_data_id IN
               (
                   SELECT filter_ids.item_data_id
                     FROM JSON_TABLE(p_item_data_ids, '$[*]'
                          COLUMNS(item_data_id INT UNSIGNED PATH '$')) AS filter_ids
               ))
           AND search_str >= p_min_str
           AND search_dex >= p_min_dex
           AND search_int >= p_min_int
           AND search_luk >= p_min_luk
           AND (p_seller_user_id = 0 OR seller_user_id = p_seller_user_id)
           AND
           (
               p_cursor_listing_id = 0
               OR expires_at > v_cursor_time
               OR (expires_at = v_cursor_time AND listing_id > p_cursor_listing_id)
           )
         ORDER BY expires_at ASC, listing_id ASC
         LIMIT p_limit;
    END IF;
END$$

CREATE PROCEDURE sp_ad_r_listing_detail
(
    IN p_listing_id BIGINT UNSIGNED
)
BEGIN
    SELECT listing_id, seller_user_id, seller_login_id, item_instance_id, item_data_id, item_category,
           quantity, item_data, search_name, search_str, search_dex, search_int, search_luk,
           currency_id, start_price, current_bid_price, buyout_price,
           highest_bidder_user_id,
           CAST(UNIX_TIMESTAMP(expires_at) * 1000 AS UNSIGNED) AS expires_at_unix_ms,
           version
      FROM auction_listings
     WHERE listing_id = p_listing_id
       AND state = 2
       AND expires_at > UTC_TIMESTAMP(6);
END$$

CREATE PROCEDURE sp_ad_r_sale_history
(
    IN p_item_category TINYINT UNSIGNED,
    IN p_item_data_ids JSON,
    IN p_min_str INT UNSIGNED,
    IN p_min_dex INT UNSIGNED,
    IN p_min_int INT UNSIGNED,
    IN p_min_luk INT UNSIGNED,
    IN p_sort_type TINYINT UNSIGNED,
    IN p_cursor_sort_value BIGINT UNSIGNED,
    IN p_cursor_listing_id BIGINT UNSIGNED,
    IN p_limit INT UNSIGNED
)
BEGIN
    DECLARE v_cursor_time DATETIME(6);

    IF p_item_category NOT IN (0, 1, 2, 3) OR p_sort_type NOT IN (1, 2, 3) OR p_limit = 0 OR p_limit > 100 OR
       p_item_data_ids IS NULL OR JSON_TYPE(p_item_data_ids) <> 'ARRAY' OR JSON_LENGTH(p_item_data_ids) > 100 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_SALE_HISTORY_SEARCH';
    END IF;

    IF p_sort_type = 1 AND p_cursor_listing_id <> 0 THEN
        SET v_cursor_time = COALESCE
        (
            (SELECT updated_at FROM auction_listings WHERE listing_id = p_cursor_listing_id),
            FROM_UNIXTIME(p_cursor_sort_value / 1000.0)
        );
    END IF;

    IF p_sort_type = 1 THEN
        SELECT listing_id, item_data_id, item_category, quantity, search_name,
               search_str, search_dex, search_int, search_luk, currency_id,
               final_price, sale_type,
               CAST(UNIX_TIMESTAMP(updated_at) * 1000 AS UNSIGNED) AS sold_at_unix_ms
          FROM auction_listings
         WHERE state = 7
           AND final_price IS NOT NULL
           AND sale_type IN (1, 2)
           AND (p_item_category = 0 OR item_category = p_item_category)
           AND (JSON_LENGTH(p_item_data_ids) = 0 OR item_data_id IN
               (
                   SELECT filter_ids.item_data_id
                     FROM JSON_TABLE(p_item_data_ids, '$[*]'
                          COLUMNS(item_data_id INT UNSIGNED PATH '$')) AS filter_ids
               ))
           AND search_str >= p_min_str
           AND search_dex >= p_min_dex
           AND search_int >= p_min_int
           AND search_luk >= p_min_luk
           AND
           (
               p_cursor_listing_id = 0
               OR updated_at < v_cursor_time
               OR (updated_at = v_cursor_time AND listing_id < p_cursor_listing_id)
           )
         ORDER BY updated_at DESC, listing_id DESC
         LIMIT p_limit;
    ELSEIF p_sort_type = 2 THEN
        SELECT listing_id, item_data_id, item_category, quantity, search_name,
               search_str, search_dex, search_int, search_luk, currency_id,
               final_price, sale_type,
               CAST(UNIX_TIMESTAMP(updated_at) * 1000 AS UNSIGNED) AS sold_at_unix_ms
          FROM auction_listings
         WHERE state = 7
           AND final_price IS NOT NULL
           AND sale_type IN (1, 2)
           AND (p_item_category = 0 OR item_category = p_item_category)
           AND (JSON_LENGTH(p_item_data_ids) = 0 OR item_data_id IN
               (
                   SELECT filter_ids.item_data_id
                     FROM JSON_TABLE(p_item_data_ids, '$[*]'
                          COLUMNS(item_data_id INT UNSIGNED PATH '$')) AS filter_ids
               ))
           AND search_str >= p_min_str
           AND search_dex >= p_min_dex
           AND search_int >= p_min_int
           AND search_luk >= p_min_luk
           AND
           (
               p_cursor_listing_id = 0
               OR final_price > p_cursor_sort_value
               OR (final_price = p_cursor_sort_value AND listing_id < p_cursor_listing_id)
           )
         ORDER BY final_price ASC, listing_id DESC
         LIMIT p_limit;
    ELSE
        SELECT listing_id, item_data_id, item_category, quantity, search_name,
               search_str, search_dex, search_int, search_luk, currency_id,
               final_price, sale_type,
               CAST(UNIX_TIMESTAMP(updated_at) * 1000 AS UNSIGNED) AS sold_at_unix_ms
          FROM auction_listings
         WHERE state = 7
           AND final_price IS NOT NULL
           AND sale_type IN (1, 2)
           AND (p_item_category = 0 OR item_category = p_item_category)
           AND (JSON_LENGTH(p_item_data_ids) = 0 OR item_data_id IN
               (
                   SELECT filter_ids.item_data_id
                     FROM JSON_TABLE(p_item_data_ids, '$[*]'
                          COLUMNS(item_data_id INT UNSIGNED PATH '$')) AS filter_ids
               ))
           AND search_str >= p_min_str
           AND search_dex >= p_min_dex
           AND search_int >= p_min_int
           AND search_luk >= p_min_luk
           AND
           (
               p_cursor_listing_id = 0
               OR final_price < p_cursor_sort_value
               OR (final_price = p_cursor_sort_value AND listing_id > p_cursor_listing_id)
           )
         ORDER BY final_price DESC, listing_id ASC
         LIMIT p_limit;
    END IF;
END$$

CREATE PROCEDURE sp_ad_r_sale_history_detail
(
    IN p_listing_id BIGINT UNSIGNED
)
BEGIN
    SELECT listing_id, seller_login_id, item_data_id, item_category, quantity,
           item_data, search_name, search_str, search_dex, search_int, search_luk,
           currency_id, start_price, final_price, sale_type,
           CAST(UNIX_TIMESTAMP(updated_at) * 1000 AS UNSIGNED) AS sold_at_unix_ms
      FROM auction_listings
     WHERE listing_id = p_listing_id
       AND state = 7
       AND final_price IS NOT NULL
       AND sale_type IN (1, 2);
END$$

CREATE PROCEDURE sp_ad_cu_bid_prepare
(
    IN p_listing_id BIGINT UNSIGNED,
    IN p_bidder_user_id BIGINT UNSIGNED,
    IN p_bid_amount BIGINT UNSIGNED,
    IN p_expected_listing_version BIGINT UNSIGNED
)
BEGIN
    DECLARE v_seller_user_id BIGINT UNSIGNED;
    DECLARE v_currency_id SMALLINT UNSIGNED;
    DECLARE v_start_price BIGINT UNSIGNED;
    DECLARE v_current_bid_price BIGINT UNSIGNED;
    DECLARE v_highest_bid_id BIGINT UNSIGNED;
    DECLARE v_highest_bidder_user_id BIGINT UNSIGNED;
    DECLARE v_listing_version BIGINT UNSIGNED;
    DECLARE v_existing_bid_id BIGINT UNSIGNED;
    DECLARE v_existing_bid_amount BIGINT UNSIGNED DEFAULT 0;
    DECLARE v_existing_bid_state TINYINT UNSIGNED;
    DECLARE v_existing_bid_version BIGINT UNSIGNED;
    DECLARE v_additional_debit BIGINT UNSIGNED;
    DECLARE v_bid_id BIGINT UNSIGNED;
    DECLARE v_listing_found BOOLEAN DEFAULT TRUE;
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_listing_found = FALSE;

    SELECT seller_user_id, currency_id, start_price, current_bid_price,
           highest_bid_id, highest_bidder_user_id, version
      INTO v_seller_user_id, v_currency_id, v_start_price, v_current_bid_price,
           v_highest_bid_id, v_highest_bidder_user_id, v_listing_version
      FROM auction_listings
     WHERE listing_id = p_listing_id
       AND state = 2
       AND expires_at > UTC_TIMESTAMP(6)
     FOR UPDATE;

    IF NOT v_listing_found THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'LISTING_VERSION_MISMATCH';
    END IF;

    IF v_listing_version <> p_expected_listing_version THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'LISTING_VERSION_MISMATCH';
    END IF;
    IF v_seller_user_id = p_bidder_user_id THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'SELLER_CANNOT_BID';
    END IF;
    IF p_bid_amount <= v_current_bid_price OR p_bid_amount < v_start_price THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'BID_TOO_LOW';
    END IF;

    -- The locked listing row is the aggregate lock for every bid mutation on this listing.
    -- Keep this as a consistent read so a missing bid does not create a next-key/gap lock.
    SELECT MAX(bid_id), COALESCE(MAX(bid_amount), 0), MAX(state), MAX(version)
      INTO v_existing_bid_id, v_existing_bid_amount, v_existing_bid_state, v_existing_bid_version
      FROM auction_bids
     WHERE listing_id = p_listing_id
       AND bidder_user_id = p_bidder_user_id;

    IF v_existing_bid_id IS NULL THEN
        SET v_additional_debit = p_bid_amount;
        INSERT INTO auction_bids(listing_id, bidder_user_id, currency_id, bid_amount, state, version)
        VALUES(p_listing_id, p_bidder_user_id, v_currency_id, p_bid_amount, 1, 1);
        SET v_bid_id = LAST_INSERT_ID();
    ELSE
        IF v_existing_bid_state IN (2, 3) THEN
            IF p_bid_amount <= v_existing_bid_amount THEN
                SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'BID_TOO_LOW';
            END IF;
            SET v_additional_debit = p_bid_amount - v_existing_bid_amount;
        ELSEIF v_existing_bid_state = 5 THEN
            SET v_additional_debit = p_bid_amount;
        ELSE
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'BID_STATE_INVALID';
        END IF;
        UPDATE auction_bids
           SET bid_amount = p_bid_amount, state = 1, version = version + 1
         WHERE bid_id = v_existing_bid_id AND version = v_existing_bid_version;
        SET v_bid_id = v_existing_bid_id;
    END IF;

    IF v_highest_bid_id IS NOT NULL AND v_highest_bid_id <> v_bid_id THEN
        UPDATE auction_bids
           SET state = 3, version = version + 1
         WHERE bid_id = v_highest_bid_id AND state = 2;
        IF ROW_COUNT() <> 1 THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'HIGHEST_BID_STATE_INVALID';
        END IF;
    END IF;

    UPDATE auction_listings
       SET state = 3,
           current_bid_price = p_bid_amount,
           highest_bid_id = v_bid_id,
           highest_bidder_user_id = p_bidder_user_id,
           version = version + 1
     WHERE listing_id = p_listing_id AND state = 2 AND version = p_expected_listing_version;

    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CONCURRENT_MODIFICATION';
    END IF;

    SELECT v_bid_id AS bid_id,
           v_additional_debit AS additional_debit,
           v_currency_id AS currency_id,
           COALESCE(v_highest_bid_id, 0) AS previous_highest_bid_id,
           COALESCE(v_highest_bidder_user_id, 0) AS previous_highest_bidder_user_id,
           v_current_bid_price AS previous_highest_amount,
           p_expected_listing_version + 1 AS prepared_listing_version;
END$$

CREATE PROCEDURE sp_ad_u_bid_complete
(
    IN p_listing_id BIGINT UNSIGNED,
    IN p_bid_id BIGINT UNSIGNED,
    IN p_bidder_user_id BIGINT UNSIGNED,
    IN p_expected_listing_version BIGINT UNSIGNED
)
BEGIN
    DECLARE v_listing_found BOOLEAN DEFAULT TRUE;
    DECLARE v_locked_listing_id BIGINT UNSIGNED;
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_listing_found = FALSE;

    SELECT listing_id
      INTO v_locked_listing_id
      FROM auction_listings
     WHERE listing_id = p_listing_id
       AND state = 3
       AND version = p_expected_listing_version
       AND highest_bid_id = p_bid_id
       AND highest_bidder_user_id = p_bidder_user_id
     FOR UPDATE;

    IF NOT v_listing_found THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'LISTING_COMPLETE_FAILED';
    END IF;

    UPDATE auction_bids
       SET state = 2, version = version + 1
     WHERE bid_id = p_bid_id
       AND listing_id = p_listing_id
       AND bidder_user_id = p_bidder_user_id
       AND state = 1;
    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'BID_COMPLETE_FAILED';
    END IF;

    UPDATE auction_listings
       SET state = 2, version = version + 1
     WHERE listing_id = p_listing_id
       AND state = 3
       AND version = p_expected_listing_version
       AND highest_bid_id = p_bid_id
       AND highest_bidder_user_id = p_bidder_user_id;
    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'LISTING_COMPLETE_FAILED';
    END IF;

    SELECT version FROM auction_listings WHERE listing_id = p_listing_id;
END$$

CREATE PROCEDURE sp_ad_r_outbid_claimable
(
    IN p_bidder_user_id BIGINT UNSIGNED,
    IN p_limit INT UNSIGNED
)
BEGIN
    SELECT b.listing_id, b.bid_id, b.bid_amount,
           COALESCE(l.final_price, l.current_bid_price)
      FROM auction_bids AS b
      JOIN auction_listings AS l ON l.listing_id = b.listing_id
     WHERE b.bidder_user_id = p_bidder_user_id
       AND b.state = 3
     ORDER BY b.updated_at DESC, b.bid_id DESC
     LIMIT p_limit;
END$$

CREATE PROCEDURE sp_ad_cu_buyout_prepare
(
    IN p_listing_id BIGINT UNSIGNED,
    IN p_buyer_user_id BIGINT UNSIGNED,
    IN p_expected_listing_version BIGINT UNSIGNED
)
BEGIN
    DECLARE v_seller_user_id BIGINT UNSIGNED;
    DECLARE v_item_instance_id BIGINT UNSIGNED;
    DECLARE v_item_data_id INT UNSIGNED;
    DECLARE v_quantity INT UNSIGNED;
    DECLARE v_item_data JSON;
    DECLARE v_currency_id SMALLINT UNSIGNED;
    DECLARE v_buyout_price BIGINT UNSIGNED;
    DECLARE v_highest_bid_id BIGINT UNSIGNED;
    DECLARE v_highest_bidder_user_id BIGINT UNSIGNED;
    DECLARE v_highest_bid_amount BIGINT UNSIGNED DEFAULT 0;
    DECLARE v_listing_version BIGINT UNSIGNED;
    DECLARE v_buyer_bid_id BIGINT UNSIGNED;
    DECLARE v_buyer_bid_amount BIGINT UNSIGNED DEFAULT 0;
    DECLARE v_buyer_bid_state TINYINT UNSIGNED;
    DECLARE v_additional_debit BIGINT UNSIGNED;

    SELECT seller_user_id,item_instance_id,item_data_id,quantity,item_data,
           currency_id,buyout_price,highest_bid_id,highest_bidder_user_id,
           current_bid_price,version
      INTO v_seller_user_id,v_item_instance_id,v_item_data_id,v_quantity,v_item_data,
           v_currency_id,v_buyout_price,v_highest_bid_id,v_highest_bidder_user_id,
           v_highest_bid_amount,v_listing_version
      FROM auction_listings
     WHERE listing_id = p_listing_id
       AND state = 2
       AND expires_at > UTC_TIMESTAMP(6)
     FOR UPDATE;

    IF v_seller_user_id IS NULL OR v_buyout_price IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'BUYOUT_NOT_AVAILABLE';
    END IF;
    IF v_listing_version <> p_expected_listing_version THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'LISTING_VERSION_MISMATCH';
    END IF;
    IF v_seller_user_id = p_buyer_user_id THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'SELLER_CANNOT_BUY';
    END IF;

    -- The listing row above serializes buyout and bid mutations for this listing.
    -- Do not lock a missing buyer bid because its index gap may be shared by other listings.
    SELECT MAX(bid_id), COALESCE(MAX(bid_amount),0), MAX(state)
      INTO v_buyer_bid_id, v_buyer_bid_amount, v_buyer_bid_state
      FROM auction_bids
     WHERE listing_id = p_listing_id AND bidder_user_id = p_buyer_user_id;

    IF v_buyer_bid_id IS NOT NULL AND v_buyer_bid_state IN (2,3) THEN
        SET v_additional_debit = IF(v_buyer_bid_amount < v_buyout_price,
                                    v_buyout_price - v_buyer_bid_amount, 0);
        UPDATE auction_bids SET state = 6, version = version + 1
         WHERE bid_id = v_buyer_bid_id AND state IN (2,3);
    ELSE
        SET v_additional_debit = v_buyout_price;
    END IF;

    IF v_highest_bid_id IS NOT NULL AND v_highest_bid_id <> COALESCE(v_buyer_bid_id,0) THEN
        UPDATE auction_bids
           SET state = 3, version = version + 1
         WHERE bid_id = v_highest_bid_id AND state = 2;
        IF ROW_COUNT() <> 1 THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'HIGHEST_BID_STATE_INVALID';
        END IF;
    END IF;

    UPDATE auction_listings
       SET state = 4,
           current_bid_price = 0,
           highest_bid_id = NULL,
           highest_bidder_user_id = NULL,
           final_buyer_user_id = p_buyer_user_id,
           final_price = v_buyout_price,
           sale_type = 2,
           version = version + 1
     WHERE listing_id = p_listing_id AND state = 2 AND version = p_expected_listing_version;
    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CONCURRENT_MODIFICATION';
    END IF;

    SELECT v_seller_user_id AS seller_user_id,
           v_item_instance_id AS item_instance_id,
           v_item_data_id AS item_data_id,
           v_quantity AS quantity,
           v_item_data AS item_data,
           v_currency_id AS currency_id,
           v_buyout_price AS buyout_price,
           v_additional_debit AS additional_debit,
           IF(v_highest_bid_id <> COALESCE(v_buyer_bid_id,0),COALESCE(v_highest_bid_id,0),0)
               AS previous_highest_bid_id,
           IF(v_highest_bid_id <> COALESCE(v_buyer_bid_id,0),COALESCE(v_highest_bidder_user_id,0),0)
               AS previous_highest_bidder_user_id,
           IF(v_highest_bid_id <> COALESCE(v_buyer_bid_id,0),v_highest_bid_amount,0)
               AS previous_highest_amount,
           p_expected_listing_version + 1 AS prepared_listing_version;
END$$

CREATE PROCEDURE sp_ad_u_buyout_complete
(
    IN p_listing_id BIGINT UNSIGNED,
    IN p_buyer_user_id BIGINT UNSIGNED,
    IN p_expected_listing_version BIGINT UNSIGNED
)
BEGIN
    UPDATE auction_listings
       SET state = 7, version = version + 1
     WHERE listing_id = p_listing_id
       AND final_buyer_user_id = p_buyer_user_id
       AND state = 4
       AND version = p_expected_listing_version;
    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'BUYOUT_COMPLETE_FAILED';
    END IF;
    SELECT version FROM auction_listings WHERE listing_id = p_listing_id;
END$$

CREATE PROCEDURE sp_ad_u_cancel_prepare
(
    IN p_listing_id BIGINT UNSIGNED,
    IN p_seller_user_id BIGINT UNSIGNED,
    IN p_expected_listing_version BIGINT UNSIGNED
)
BEGIN
    DECLARE v_found BOOLEAN DEFAULT TRUE;
    DECLARE v_seller_user_id BIGINT UNSIGNED;
    DECLARE v_item_instance_id BIGINT UNSIGNED;
    DECLARE v_item_data_id INT UNSIGNED;
    DECLARE v_quantity INT UNSIGNED;
    DECLARE v_item_data JSON;
    DECLARE v_highest_bid_id BIGINT UNSIGNED;
    DECLARE v_state TINYINT UNSIGNED;
    DECLARE v_version BIGINT UNSIGNED;
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_found = FALSE;

    SELECT seller_user_id,item_instance_id,item_data_id,quantity,item_data,
           highest_bid_id,state,version
      INTO v_seller_user_id,v_item_instance_id,v_item_data_id,v_quantity,v_item_data,
           v_highest_bid_id,v_state,v_version
      FROM auction_listings
     WHERE listing_id=p_listing_id
     FOR UPDATE;

    IF NOT v_found THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'LISTING_NOT_FOUND';
    END IF;
    IF v_seller_user_id <> p_seller_user_id THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'NOT_LISTING_OWNER';
    END IF;
    IF v_version <> p_expected_listing_version THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'LISTING_VERSION_MISMATCH';
    END IF;
    IF v_state <> 2 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CANCEL_NOT_AVAILABLE';
    END IF;
    IF v_highest_bid_id IS NOT NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'HIGHEST_BID_EXISTS';
    END IF;

    UPDATE auction_listings SET state=5,version=version+1
     WHERE listing_id=p_listing_id AND state=2 AND version=p_expected_listing_version;
    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CONCURRENT_MODIFICATION';
    END IF;

    SELECT v_item_instance_id AS item_instance_id,
           v_item_data_id AS item_data_id,
           v_quantity AS quantity,
           v_item_data AS item_data,
           p_expected_listing_version + 1 AS prepared_listing_version;
END$$

CREATE PROCEDURE sp_ad_u_cancel_complete
(
    IN p_listing_id BIGINT UNSIGNED,
    IN p_seller_user_id BIGINT UNSIGNED,
    IN p_expected_listing_version BIGINT UNSIGNED
)
BEGIN
    UPDATE auction_listings SET state=8,version=version+1
     WHERE listing_id=p_listing_id AND seller_user_id=p_seller_user_id
       AND state=5 AND version=p_expected_listing_version;
    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CANCEL_COMPLETE_FAILED';
    END IF;
    SELECT version FROM auction_listings WHERE listing_id=p_listing_id;
END$$

CREATE PROCEDURE sp_ad_r_expired_listing_candidates(IN p_limit INT UNSIGNED)
BEGIN
    IF p_limit=0 OR p_limit>1000 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='INVALID_EXPIRE_LIMIT';
    END IF;
    SELECT listing_id FROM auction_listings
     WHERE state=2 AND expires_at<=UTC_TIMESTAMP(6)
     ORDER BY expires_at,listing_id
     LIMIT p_limit;
END$$

CREATE PROCEDURE sp_ad_u_expire_prepare(IN p_listing_id BIGINT UNSIGNED)
BEGIN
    DECLARE v_found BOOLEAN DEFAULT TRUE;
    DECLARE v_seller_user_id BIGINT UNSIGNED;
    DECLARE v_item_instance_id BIGINT UNSIGNED;
    DECLARE v_item_data_id INT UNSIGNED;
    DECLARE v_quantity INT UNSIGNED;
    DECLARE v_item_data JSON;
    DECLARE v_currency_id SMALLINT UNSIGNED;
    DECLARE v_highest_bid_id BIGINT UNSIGNED;
    DECLARE v_highest_bidder_user_id BIGINT UNSIGNED;
    DECLARE v_current_bid_price BIGINT UNSIGNED;
    DECLARE v_version BIGINT UNSIGNED;
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_found=FALSE;

    SELECT seller_user_id,item_instance_id,item_data_id,quantity,item_data,currency_id,
           highest_bid_id,highest_bidder_user_id,current_bid_price,version
      INTO v_seller_user_id,v_item_instance_id,v_item_data_id,v_quantity,v_item_data,v_currency_id,
           v_highest_bid_id,v_highest_bidder_user_id,v_current_bid_price,v_version
      FROM auction_listings
     WHERE listing_id=p_listing_id AND state=2 AND expires_at<=UTC_TIMESTAMP(6)
     FOR UPDATE;
    IF NOT v_found THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='EXPIRE_NOT_AVAILABLE';
    END IF;

    IF v_highest_bid_id IS NOT NULL THEN
        UPDATE auction_bids SET state=6,version=version+1
         WHERE bid_id=v_highest_bid_id AND bidder_user_id=v_highest_bidder_user_id AND state=2;
        IF ROW_COUNT()<>1 THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='HIGHEST_BID_STATE_INVALID';
        END IF;
    END IF;
    UPDATE auction_listings SET state=6,version=version+1
     WHERE listing_id=p_listing_id AND state=2 AND version=v_version;
    IF ROW_COUNT()<>1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='CONCURRENT_MODIFICATION';
    END IF;

    SELECT v_seller_user_id AS seller_user_id,v_item_instance_id AS item_instance_id,
           v_item_data_id AS item_data_id,v_quantity AS quantity,v_item_data AS item_data,
           v_currency_id AS currency_id,COALESCE(v_highest_bid_id,0) AS highest_bid_id,
           COALESCE(v_highest_bidder_user_id,0) AS winner_user_id,
           v_current_bid_price AS final_price,v_version+1 AS prepared_listing_version;
END$$

CREATE PROCEDURE sp_ad_u_expire_complete
(
    IN p_listing_id BIGINT UNSIGNED,
    IN p_winner_user_id BIGINT UNSIGNED,
    IN p_final_price BIGINT UNSIGNED,
    IN p_expected_listing_version BIGINT UNSIGNED
)
BEGIN
    IF p_winner_user_id=0 THEN
        UPDATE auction_listings
           SET state=9,current_bid_price=0,highest_bid_id=NULL,highest_bidder_user_id=NULL,
               version=version+1
         WHERE listing_id=p_listing_id AND state=6 AND version=p_expected_listing_version;
    ELSE
        UPDATE auction_listings
           SET state=7,current_bid_price=0,highest_bid_id=NULL,highest_bidder_user_id=NULL,
               final_buyer_user_id=p_winner_user_id,final_price=p_final_price,sale_type=1,
               version=version+1
         WHERE listing_id=p_listing_id AND state=6 AND version=p_expected_listing_version;
    END IF;
    IF ROW_COUNT()<>1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='EXPIRE_COMPLETE_FAILED';
    END IF;
    SELECT version FROM auction_listings WHERE listing_id=p_listing_id;
END$$

CREATE PROCEDURE sp_ad_u_bid_refund_prepare
(
    IN p_listing_id BIGINT UNSIGNED,
    IN p_bid_id BIGINT UNSIGNED,
    IN p_bidder_user_id BIGINT UNSIGNED,
    IN p_expected_version BIGINT UNSIGNED
)
BEGIN
    DECLARE v_listing_found BOOLEAN DEFAULT TRUE;
    DECLARE v_locked_listing_id BIGINT UNSIGNED;
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_listing_found = FALSE;

    -- All auction_bids state transitions use auction_listings as their aggregate lock.
    SELECT listing_id
      INTO v_locked_listing_id
      FROM auction_listings
     WHERE listing_id = p_listing_id
     FOR UPDATE;

    IF NOT v_listing_found THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'BID_NOT_CLAIMABLE';
    END IF;

    UPDATE auction_bids
       SET state = 4,
           version = version + 1
     WHERE bid_id = p_bid_id
       AND listing_id = p_listing_id
       AND bidder_user_id = p_bidder_user_id
       AND state = 3
       AND version = p_expected_version;

    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'BID_NOT_CLAIMABLE';
    END IF;

    SELECT bid_id, listing_id, bidder_user_id, currency_id, bid_amount, version
      FROM auction_bids
     WHERE bid_id = p_bid_id;
END$$

CREATE PROCEDURE sp_ad_u_bid_refund_complete
(
    IN p_listing_id BIGINT UNSIGNED,
    IN p_bid_id BIGINT UNSIGNED,
    IN p_bidder_user_id BIGINT UNSIGNED,
    IN p_expected_version BIGINT UNSIGNED
)
BEGIN
    DECLARE v_listing_found BOOLEAN DEFAULT TRUE;
    DECLARE v_locked_listing_id BIGINT UNSIGNED;
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_listing_found = FALSE;

    -- Preserve the global listing -> bid lock order during the completion phase.
    SELECT listing_id
      INTO v_locked_listing_id
      FROM auction_listings
     WHERE listing_id = p_listing_id
     FOR UPDATE;

    IF NOT v_listing_found THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CONCURRENT_MODIFICATION';
    END IF;

    UPDATE auction_bids
       SET state = 5,
           version = version + 1
     WHERE bid_id = p_bid_id
       AND listing_id = p_listing_id
       AND bidder_user_id = p_bidder_user_id
       AND state = 4
       AND version = p_expected_version;

    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CONCURRENT_MODIFICATION';
    END IF;
END$$

CREATE PROCEDURE sp_ad_u_bid_refund_revert
(
    IN p_listing_id BIGINT UNSIGNED,
    IN p_bid_id BIGINT UNSIGNED,
    IN p_bidder_user_id BIGINT UNSIGNED,
    IN p_expected_version BIGINT UNSIGNED
)
BEGIN
    DECLARE v_listing_found BOOLEAN DEFAULT TRUE;
    DECLARE v_locked_listing_id BIGINT UNSIGNED;
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_listing_found = FALSE;

    -- Preserve the global listing -> bid lock order during compensation.
    SELECT listing_id
      INTO v_locked_listing_id
      FROM auction_listings
     WHERE listing_id = p_listing_id
     FOR UPDATE;

    IF NOT v_listing_found THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CONCURRENT_MODIFICATION';
    END IF;

    UPDATE auction_bids
       SET state = 3,
           version = version + 1
     WHERE bid_id = p_bid_id
       AND listing_id = p_listing_id
       AND bidder_user_id = p_bidder_user_id
       AND state = 4
       AND version = p_expected_version;

    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CONCURRENT_MODIFICATION';
    END IF;
END$$

DELIMITER ;

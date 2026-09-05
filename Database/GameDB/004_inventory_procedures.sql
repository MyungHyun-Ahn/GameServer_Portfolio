USE gamedb;

DROP PROCEDURE IF EXISTS sp_gd_c_inventory_item;
DROP PROCEDURE IF EXISTS sp_gd_r_inventory_items;

DELIMITER $$

CREATE PROCEDURE sp_gd_c_inventory_item
(
    IN p_owner_user_id BIGINT UNSIGNED,
    IN p_item_data_id INT UNSIGNED,
    IN p_quantity INT UNSIGNED,
    IN p_max_stack INT UNSIGNED,
    IN p_max_inventory_slots INT UNSIGNED,
    IN p_item_data JSON,
    IN p_is_tradable TINYINT UNSIGNED
)
BEGIN
    DECLARE v_inventory_count INT UNSIGNED DEFAULT 0;

    IF p_owner_user_id = 0 OR p_item_data_id = 0 OR
       p_quantity = 0 OR p_max_stack = 0 OR p_quantity > p_max_stack OR
       p_max_inventory_slots = 0 OR p_item_data IS NULL OR
       p_is_tradable NOT IN (0, 1) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_INVENTORY_ITEM';
    END IF;

    -- 동일 userId 요청은 Cache Content mailbox에서 순차 실행한다. 여기서 빈 owner index
    -- 구간을 잠그면 서로 다른 사용자의 최초 지급끼리 gap-lock deadlock이 날 수 있어
    -- 최대 슬롯 확인은 consistent read로 수행한다.
    SELECT COUNT(*)
      INTO v_inventory_count
      FROM inventory_items FORCE INDEX (idx_inventory_owner)
     WHERE owner_user_id = p_owner_user_id;

    IF v_inventory_count >= p_max_inventory_slots THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVENTORY_FULL';
    END IF;

    INSERT INTO inventory_items
    (
        owner_user_id, item_data_id, quantity, item_data,
        is_equipped, is_tradable, version
    )
    VALUES
    (
        p_owner_user_id, p_item_data_id, p_quantity, p_item_data,
        0, p_is_tradable, 1
    );

    SELECT item_instance_id, owner_user_id, item_data_id, quantity,
           is_equipped, is_tradable, version
      FROM inventory_items
     WHERE item_instance_id = LAST_INSERT_ID();
END$$

CREATE PROCEDURE sp_gd_r_inventory_items
(
    IN p_owner_user_id BIGINT UNSIGNED,
    IN p_cursor_item_instance_id BIGINT UNSIGNED,
    IN p_limit INT UNSIGNED
)
BEGIN
    IF p_owner_user_id = 0 OR p_limit = 0 OR p_limit > 100 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_INVENTORY_QUERY';
    END IF;

    SELECT item_instance_id, item_data_id, quantity,
           is_equipped, is_tradable, item_data, version
      FROM inventory_items
     WHERE owner_user_id = p_owner_user_id
       AND (p_cursor_item_instance_id = 0 OR item_instance_id < p_cursor_item_instance_id)
     ORDER BY item_instance_id DESC
     LIMIT p_limit;
END$$

DELIMITER ;

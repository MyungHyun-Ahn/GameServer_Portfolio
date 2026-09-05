USE gamedb;

DROP PROCEDURE IF EXISTS sp_gd_u_player_item_equip;
DROP PROCEDURE IF EXISTS sp_gd_u_player_item_unequip;

DELIMITER $$

CREATE PROCEDURE sp_gd_u_player_item_equip
(
    IN p_user_id BIGINT UNSIGNED,
    IN p_target_item_instance_id BIGINT UNSIGNED,
    IN p_expected_target_version BIGINT UNSIGNED,
    IN p_previous_item_instance_id BIGINT UNSIGNED,
    IN p_expected_previous_version BIGINT UNSIGNED
)
BEGIN
    DECLARE v_target_found TINYINT UNSIGNED DEFAULT 1;
    DECLARE v_target_equipped TINYINT UNSIGNED;
    DECLARE v_target_version BIGINT UNSIGNED;
    DECLARE v_previous_found TINYINT UNSIGNED DEFAULT 1;
    DECLARE v_previous_equipped TINYINT UNSIGNED;
    DECLARE v_previous_version BIGINT UNSIGNED;

    IF p_user_id IS NULL OR p_user_id = 0 OR
       p_target_item_instance_id IS NULL OR p_target_item_instance_id = 0 OR
       p_expected_target_version IS NULL OR p_expected_target_version = 0 OR
       p_previous_item_instance_id IS NULL OR p_expected_previous_version IS NULL OR
       (p_previous_item_instance_id = 0 AND p_expected_previous_version <> 0) OR
       (p_previous_item_instance_id <> 0 AND p_expected_previous_version = 0) OR
       p_previous_item_instance_id = p_target_item_instance_id THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_PLAYER_EQUIPMENT';
    END IF;

    -- 교체 대상 두 row는 item_instance_id 오름차순으로 잠근다. 같은 사용자의
    -- 요청은 Cache Content mailbox에서도 순차 실행되지만 DB 잠금 순서까지
    -- 고정해 다른 관리 경로가 추가되어도 교착 가능성을 줄인다.
    IF p_previous_item_instance_id <> 0 AND p_previous_item_instance_id < p_target_item_instance_id THEN
        BEGIN
            DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_previous_found = 0;
            SELECT is_equipped, version
              INTO v_previous_equipped, v_previous_version
              FROM inventory_items
             WHERE item_instance_id = p_previous_item_instance_id
               AND owner_user_id = p_user_id
             FOR UPDATE;
        END;
        BEGIN
            DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_target_found = 0;
            SELECT is_equipped, version
              INTO v_target_equipped, v_target_version
              FROM inventory_items
             WHERE item_instance_id = p_target_item_instance_id
               AND owner_user_id = p_user_id
             FOR UPDATE;
        END;
    ELSE
        BEGIN
            DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_target_found = 0;
            SELECT is_equipped, version
              INTO v_target_equipped, v_target_version
              FROM inventory_items
             WHERE item_instance_id = p_target_item_instance_id
               AND owner_user_id = p_user_id
             FOR UPDATE;
        END;
        IF p_previous_item_instance_id <> 0 THEN
            BEGIN
                DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_previous_found = 0;
                SELECT is_equipped, version
                  INTO v_previous_equipped, v_previous_version
                  FROM inventory_items
                 WHERE item_instance_id = p_previous_item_instance_id
                   AND owner_user_id = p_user_id
                 FOR UPDATE;
            END;
        END IF;
    END IF;

    IF v_target_found = 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVENTORY_ITEM_NOT_FOUND';
    END IF;
    IF v_target_version <> p_expected_target_version THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'ITEM_VERSION_MISMATCH';
    END IF;
    IF v_target_equipped <> 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'EQUIPMENT_STATE_CONFLICT';
    END IF;
    IF v_target_version = 18446744073709551615 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'ITEM_VERSION_OVERFLOW';
    END IF;

    IF p_previous_item_instance_id <> 0 THEN
        IF v_previous_found = 0 OR v_previous_version <> p_expected_previous_version OR v_previous_equipped <> 1 THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'PREVIOUS_EQUIPMENT_CONFLICT';
        END IF;
        IF v_previous_version = 18446744073709551615 THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'ITEM_VERSION_OVERFLOW';
        END IF;

        UPDATE inventory_items
           SET is_equipped = 0,
               version = version + 1
         WHERE item_instance_id = p_previous_item_instance_id
           AND owner_user_id = p_user_id
           AND version = p_expected_previous_version
           AND is_equipped = 1;
        IF ROW_COUNT() <> 1 THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'PREVIOUS_EQUIPMENT_CONFLICT';
        END IF;
    END IF;

    UPDATE inventory_items
       SET is_equipped = 1,
           version = version + 1
     WHERE item_instance_id = p_target_item_instance_id
       AND owner_user_id = p_user_id
       AND version = p_expected_target_version
       AND is_equipped = 0;
    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'EQUIPMENT_STATE_CONFLICT';
    END IF;

    SELECT item_instance_id AS item_instance_id,
           item_data_id AS item_data_id,
           quantity AS quantity,
           CAST(item_data AS CHAR CHARACTER SET utf8mb4) AS item_data_json,
           is_equipped AS is_equipped,
           is_tradable AS is_tradable,
           version AS version
      FROM inventory_items
     WHERE item_instance_id = p_target_item_instance_id
        OR (p_previous_item_instance_id <> 0 AND item_instance_id = p_previous_item_instance_id)
     ORDER BY item_instance_id;
END$$

CREATE PROCEDURE sp_gd_u_player_item_unequip
(
    IN p_user_id BIGINT UNSIGNED,
    IN p_item_instance_id BIGINT UNSIGNED,
    IN p_expected_item_version BIGINT UNSIGNED
)
BEGIN
    DECLARE v_found TINYINT UNSIGNED DEFAULT 1;
    DECLARE v_is_equipped TINYINT UNSIGNED;
    DECLARE v_version BIGINT UNSIGNED;

    IF p_user_id IS NULL OR p_user_id = 0 OR
       p_item_instance_id IS NULL OR p_item_instance_id = 0 OR
       p_expected_item_version IS NULL OR p_expected_item_version = 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_PLAYER_EQUIPMENT';
    END IF;

    BEGIN
        DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_found = 0;
        SELECT is_equipped, version
          INTO v_is_equipped, v_version
          FROM inventory_items
         WHERE item_instance_id = p_item_instance_id
           AND owner_user_id = p_user_id
         FOR UPDATE;
    END;

    IF v_found = 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVENTORY_ITEM_NOT_FOUND';
    END IF;
    IF v_version <> p_expected_item_version THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'ITEM_VERSION_MISMATCH';
    END IF;
    IF v_is_equipped <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'EQUIPMENT_STATE_CONFLICT';
    END IF;
    IF v_version = 18446744073709551615 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'ITEM_VERSION_OVERFLOW';
    END IF;

    UPDATE inventory_items
       SET is_equipped = 0,
           version = version + 1
     WHERE item_instance_id = p_item_instance_id
       AND owner_user_id = p_user_id
       AND version = p_expected_item_version
       AND is_equipped = 1;
    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'EQUIPMENT_STATE_CONFLICT';
    END IF;

    SELECT item_instance_id AS item_instance_id,
           item_data_id AS item_data_id,
           quantity AS quantity,
           CAST(item_data AS CHAR CHARACTER SET utf8mb4) AS item_data_json,
           is_equipped AS is_equipped,
           is_tradable AS is_tradable,
           version AS version
      FROM inventory_items
     WHERE item_instance_id = p_item_instance_id;
END$$

DELIMITER ;

USE gamedb;

DROP PROCEDURE IF EXISTS sp_gd_migrate_remove_inventory_lock;
DELIMITER $$
CREATE PROCEDURE sp_gd_migrate_remove_inventory_lock()
BEGIN
    IF EXISTS (SELECT 1 FROM information_schema.columns
               WHERE table_schema='gamedb' AND table_name='inventory_items' AND column_name='is_locked') THEN
        ALTER TABLE inventory_items DROP COLUMN is_locked;
    END IF;
END$$
DELIMITER ;

CALL sp_gd_migrate_remove_inventory_lock();
DROP PROCEDURE sp_gd_migrate_remove_inventory_lock;

DROP PROCEDURE IF EXISTS sp_gd_d_inventory_item;
DELIMITER $$
CREATE PROCEDURE sp_gd_d_inventory_item
(
    IN p_owner_user_id BIGINT UNSIGNED,
    IN p_item_instance_id BIGINT UNSIGNED,
    IN p_expected_version BIGINT UNSIGNED
)
BEGIN
    DECLARE v_found BOOLEAN DEFAULT TRUE;
    DECLARE v_item_data_id INT UNSIGNED;
    DECLARE v_quantity INT UNSIGNED;
    DECLARE v_item_data JSON;
    DECLARE v_is_equipped TINYINT UNSIGNED;
    DECLARE v_is_tradable TINYINT UNSIGNED;
    DECLARE v_version BIGINT UNSIGNED;
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_found = FALSE;

    SELECT item_data_id, quantity, item_data, is_equipped, is_tradable, version
      INTO v_item_data_id, v_quantity, v_item_data, v_is_equipped, v_is_tradable, v_version
      FROM inventory_items
     WHERE item_instance_id = p_item_instance_id
       AND owner_user_id = p_owner_user_id
     FOR UPDATE;

    IF NOT v_found THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVENTORY_ITEM_NOT_FOUND';
    END IF;
    IF v_version <> p_expected_version THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'ITEM_VERSION_MISMATCH';
    END IF;
    IF v_is_equipped <> 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'ITEM_EQUIPPED';
    END IF;

    DELETE FROM inventory_items
     WHERE item_instance_id = p_item_instance_id
       AND owner_user_id = p_owner_user_id
       AND version = p_expected_version;

    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CONCURRENT_MODIFICATION';
    END IF;

    SELECT p_item_instance_id AS item_instance_id,
           v_item_data_id AS item_data_id,
           v_quantity AS quantity,
           v_item_data AS item_data,
           v_is_equipped AS is_equipped,
           v_is_tradable AS is_tradable,
           v_version AS version,
           COALESCE(CAST(JSON_UNQUOTE(JSON_EXTRACT(v_item_data,'$.str')) AS UNSIGNED),0) AS str_stat,
           COALESCE(CAST(JSON_UNQUOTE(JSON_EXTRACT(v_item_data,'$.dex')) AS UNSIGNED),0) AS dex_stat,
           COALESCE(CAST(JSON_UNQUOTE(JSON_EXTRACT(v_item_data,'$.int')) AS UNSIGNED),0) AS int_stat,
           COALESCE(CAST(JSON_UNQUOTE(JSON_EXTRACT(v_item_data,'$.luk')) AS UNSIGNED),0) AS luk_stat;
END$$
DELIMITER ;

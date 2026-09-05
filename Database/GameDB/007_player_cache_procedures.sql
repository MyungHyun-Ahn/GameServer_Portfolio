USE gamedb;

DROP PROCEDURE IF EXISTS sp_gd_r_player_cache;

DELIMITER $$

CREATE PROCEDURE sp_gd_r_player_cache
(
    IN p_user_id BIGINT UNSIGNED
)
BEGIN
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;

    IF p_user_id = 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_USER_ID';
    END IF;

    SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
    START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY;

    SELECT currency_id AS currency_id,
           amount AS amount,
           version AS version
      FROM player_currencies
     WHERE user_id = p_user_id
     ORDER BY currency_id;

    SELECT item_instance_id AS item_instance_id,
           item_data_id AS item_data_id,
           quantity AS quantity,
           CAST(item_data AS CHAR CHARACTER SET utf8mb4) AS item_data_json,
           is_equipped AS is_equipped,
           is_tradable AS is_tradable,
           version AS version
      FROM inventory_items
     WHERE owner_user_id = p_user_id
     ORDER BY item_instance_id;

    COMMIT;
END$$

DELIMITER ;

USE gamedb;

DROP PROCEDURE IF EXISTS sp_gd_migrate_mail_auto_increment;
DROP PROCEDURE IF EXISTS sp_gd_c_mail_item;
DROP PROCEDURE IF EXISTS sp_gd_c_mail_currency;
DROP PROCEDURE IF EXISTS sp_gd_c_mail_item_return;
DROP PROCEDURE IF EXISTS sp_gd_c_mail_item_expired;
DROP PROCEDURE IF EXISTS sp_gd_r_mail_list;
DROP PROCEDURE IF EXISTS sp_gd_r_mail_detail;
DROP PROCEDURE IF EXISTS sp_gd_cu_mail_claim_attachment;

DELIMITER $$
CREATE PROCEDURE sp_gd_migrate_mail_auto_increment()
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_schema='gamedb' AND table_name='mails'
                     AND column_name='mail_id' AND extra LIKE '%auto_increment%') THEN
        ALTER TABLE mails MODIFY COLUMN mail_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_schema='gamedb' AND table_name='mail_attachments'
                     AND column_name='attachment_id' AND extra LIKE '%auto_increment%') THEN
        ALTER TABLE mail_attachments
            MODIFY COLUMN attachment_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT;
    END IF;
END$$

CREATE PROCEDURE sp_gd_c_mail_item
(
    IN p_receiver_user_id BIGINT UNSIGNED,
    IN p_item_instance_id BIGINT UNSIGNED,
    IN p_item_data_id INT UNSIGNED,
    IN p_quantity INT UNSIGNED,
    IN p_item_data JSON
)
BEGIN
    DECLARE v_mail_id BIGINT UNSIGNED;
    DECLARE v_attachment_id BIGINT UNSIGNED;
    IF p_receiver_user_id = 0 OR p_item_instance_id = 0 OR p_item_data_id = 0 OR
       p_quantity = 0 OR p_item_data IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_MAIL_ITEM';
    END IF;

    INSERT INTO mails(receiver_user_id,mail_type,subject,body,state,expires_at)
    VALUES(p_receiver_user_id,2,'Auction purchase','Purchased item',1,
           DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 30 DAY));
    SET v_mail_id = LAST_INSERT_ID();
    INSERT INTO mail_attachments
        (mail_id,attachment_type,item_instance_id,item_data_id,quantity,item_data,state)
    VALUES(v_mail_id,1,p_item_instance_id,p_item_data_id,p_quantity,p_item_data,1);
    SET v_attachment_id = LAST_INSERT_ID();
    SELECT v_mail_id AS mail_id, v_attachment_id AS attachment_id;
END$$

CREATE PROCEDURE sp_gd_c_mail_currency
(
    IN p_receiver_user_id BIGINT UNSIGNED,
    IN p_currency_id SMALLINT UNSIGNED,
    IN p_currency_amount BIGINT UNSIGNED
)
BEGIN
    DECLARE v_mail_id BIGINT UNSIGNED;
    DECLARE v_attachment_id BIGINT UNSIGNED;
    IF p_receiver_user_id = 0 OR p_currency_id = 0 OR p_currency_amount = 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_MAIL_CURRENCY';
    END IF;

    INSERT INTO mails(receiver_user_id,mail_type,subject,body,state,expires_at)
    VALUES(p_receiver_user_id,2,'Auction sold','Sale proceeds',1,
           DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 30 DAY));
    SET v_mail_id = LAST_INSERT_ID();
    INSERT INTO mail_attachments
        (mail_id,attachment_type,currency_id,currency_amount,state)
    VALUES(v_mail_id,2,p_currency_id,p_currency_amount,1);
    SET v_attachment_id = LAST_INSERT_ID();
    SELECT v_mail_id AS mail_id, v_attachment_id AS attachment_id;
END$$

CREATE PROCEDURE sp_gd_c_mail_item_return
(
    IN p_receiver_user_id BIGINT UNSIGNED,
    IN p_item_instance_id BIGINT UNSIGNED,
    IN p_item_data_id INT UNSIGNED,
    IN p_quantity INT UNSIGNED,
    IN p_item_data JSON
)
BEGIN
    DECLARE v_mail_id BIGINT UNSIGNED;
    DECLARE v_attachment_id BIGINT UNSIGNED;
    IF p_receiver_user_id=0 OR p_item_instance_id=0 OR p_item_data_id=0 OR
       p_quantity=0 OR p_item_data IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='INVALID_MAIL_ITEM';
    END IF;
    INSERT INTO mails(receiver_user_id,mail_type,subject,body,state,expires_at)
    VALUES(p_receiver_user_id,3,'Auction cancelled','Returned listing item',1,
           DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 30 DAY));
    SET v_mail_id=LAST_INSERT_ID();
    INSERT INTO mail_attachments
        (mail_id,attachment_type,item_instance_id,item_data_id,quantity,item_data,state)
    VALUES(v_mail_id,1,p_item_instance_id,p_item_data_id,p_quantity,p_item_data,1);
    SET v_attachment_id=LAST_INSERT_ID();
    SELECT v_mail_id AS mail_id,v_attachment_id AS attachment_id;
END$$

CREATE PROCEDURE sp_gd_c_mail_item_expired
(
    IN p_receiver_user_id BIGINT UNSIGNED,IN p_item_instance_id BIGINT UNSIGNED,
    IN p_item_data_id INT UNSIGNED,IN p_quantity INT UNSIGNED,IN p_item_data JSON
)
BEGIN
    DECLARE v_mail_id BIGINT UNSIGNED;
    DECLARE v_attachment_id BIGINT UNSIGNED;
    IF p_receiver_user_id=0 OR p_item_instance_id=0 OR p_item_data_id=0 OR
       p_quantity=0 OR p_item_data IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='INVALID_MAIL_ITEM';
    END IF;
    INSERT INTO mails(receiver_user_id,mail_type,subject,body,state,expires_at)
    VALUES(p_receiver_user_id,4,'Auction expired','Returned unsold item',1,
           DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 30 DAY));
    SET v_mail_id=LAST_INSERT_ID();
    INSERT INTO mail_attachments
        (mail_id,attachment_type,item_instance_id,item_data_id,quantity,item_data,state)
    VALUES(v_mail_id,1,p_item_instance_id,p_item_data_id,p_quantity,p_item_data,1);
    SET v_attachment_id=LAST_INSERT_ID();
    SELECT v_mail_id AS mail_id,v_attachment_id AS attachment_id;
END$$

CREATE PROCEDURE sp_gd_r_mail_list
(
    IN p_receiver_user_id BIGINT UNSIGNED,
    IN p_cursor_mail_id BIGINT UNSIGNED,
    IN p_limit INT UNSIGNED
)
BEGIN
    IF p_receiver_user_id = 0 OR p_limit = 0 OR p_limit > 100 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_MAIL_QUERY';
    END IF;
    SELECT mail_id,mail_type,subject,state,
           CAST(UNIX_TIMESTAMP(expires_at) * 1000 AS UNSIGNED),
           CAST(UNIX_TIMESTAMP(created_at) * 1000 AS UNSIGNED)
      FROM mails
     WHERE receiver_user_id = p_receiver_user_id
       AND (p_cursor_mail_id = 0 OR mail_id < p_cursor_mail_id)
       AND expires_at > UTC_TIMESTAMP(6)
     ORDER BY mail_id DESC
     LIMIT p_limit;
END$$

CREATE PROCEDURE sp_gd_r_mail_detail
(
    IN p_receiver_user_id BIGINT UNSIGNED,
    IN p_mail_id BIGINT UNSIGNED
)
BEGIN
    SELECT m.mail_id,m.mail_type,m.subject,m.body,m.state,
           CAST(UNIX_TIMESTAMP(m.expires_at) * 1000 AS UNSIGNED),
           a.attachment_id,a.attachment_type,
           COALESCE(a.item_instance_id,0),COALESCE(a.item_data_id,0),
           COALESCE(a.quantity,0),COALESCE(CAST(a.item_data AS CHAR),'{}'),
           COALESCE(a.currency_id,0),COALESCE(a.currency_amount,0),a.state
      FROM mails AS m
      LEFT JOIN mail_attachments AS a ON a.mail_id = m.mail_id
     WHERE m.mail_id = p_mail_id AND m.receiver_user_id = p_receiver_user_id
     ORDER BY a.attachment_id;
END$$

CREATE PROCEDURE sp_gd_cu_mail_claim_attachment
(
    IN p_receiver_user_id BIGINT UNSIGNED,
    IN p_mail_id BIGINT UNSIGNED,
    IN p_attachment_id BIGINT UNSIGNED,
    IN p_max_inventory_slots INT UNSIGNED,
    IN p_max_currency_amount BIGINT UNSIGNED
)
BEGIN
    DECLARE v_attachment_type TINYINT UNSIGNED;
    DECLARE v_item_instance_id BIGINT UNSIGNED;
    DECLARE v_item_data_id INT UNSIGNED;
    DECLARE v_quantity INT UNSIGNED;
    DECLARE v_item_data JSON;
    DECLARE v_currency_id SMALLINT UNSIGNED;
    DECLARE v_currency_amount BIGINT UNSIGNED;
    DECLARE v_balance BIGINT UNSIGNED DEFAULT 0;
    DECLARE v_wallet_exists INT UNSIGNED DEFAULT 0;
    DECLARE v_mail_state TINYINT UNSIGNED;

    SELECT a.attachment_type,a.item_instance_id,a.item_data_id,a.quantity,a.item_data,
           a.currency_id,a.currency_amount
      INTO v_attachment_type,v_item_instance_id,v_item_data_id,v_quantity,v_item_data,
           v_currency_id,v_currency_amount
      FROM mails AS m
      JOIN mail_attachments AS a ON a.mail_id = m.mail_id
     WHERE m.mail_id = p_mail_id
       AND m.receiver_user_id = p_receiver_user_id
       AND m.expires_at > UTC_TIMESTAMP(6)
       AND a.attachment_id = p_attachment_id
       AND a.state = 1
     FOR UPDATE;

    IF v_attachment_type IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'MAIL_ATTACHMENT_NOT_CLAIMABLE';
    END IF;

    IF v_attachment_type = 1 THEN
        IF (SELECT COUNT(*) FROM inventory_items WHERE owner_user_id=p_receiver_user_id) >= p_max_inventory_slots THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVENTORY_FULL';
        END IF;
        IF EXISTS (SELECT 1 FROM inventory_items WHERE item_instance_id=v_item_instance_id) THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'ITEM_INSTANCE_CONFLICT';
        END IF;
        INSERT INTO inventory_items
            (item_instance_id,owner_user_id,item_data_id,quantity,item_data,is_equipped,is_tradable,version)
        VALUES(v_item_instance_id,p_receiver_user_id,v_item_data_id,v_quantity,v_item_data,0,1,1);
    ELSEIF v_attachment_type = 2 THEN
        IF v_currency_amount > p_max_currency_amount THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CURRENCY_LIMIT_EXCEEDED';
        END IF;
        SELECT COUNT(*),COALESCE(MAX(amount),0) INTO v_wallet_exists,v_balance
          FROM player_currencies
         WHERE user_id=p_receiver_user_id AND currency_id=v_currency_id
         FOR UPDATE;
        IF v_wallet_exists = 0 THEN
            INSERT INTO player_currencies(user_id,currency_id,amount,version)
            VALUES(p_receiver_user_id,v_currency_id,v_currency_amount,1);
            SET v_balance = v_currency_amount;
        ELSE
            IF v_balance > p_max_currency_amount - v_currency_amount THEN
                SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CURRENCY_LIMIT_EXCEEDED';
            END IF;
            UPDATE player_currencies SET amount=amount+v_currency_amount,version=version+1
             WHERE user_id=p_receiver_user_id AND currency_id=v_currency_id;
            SET v_balance = v_balance + v_currency_amount;
        END IF;
    ELSE
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_ATTACHMENT_TYPE';
    END IF;

    UPDATE mail_attachments SET state=2,claimed_at=UTC_TIMESTAMP(6)
     WHERE attachment_id=p_attachment_id AND mail_id=p_mail_id AND state=1;
    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'MAIL_ATTACHMENT_NOT_CLAIMABLE';
    END IF;
    SET v_mail_state = IF(EXISTS(SELECT 1 FROM mail_attachments WHERE mail_id=p_mail_id AND state=1),2,3);
    UPDATE mails SET state=v_mail_state WHERE mail_id=p_mail_id;

    SELECT v_attachment_type AS attachment_type,
           COALESCE(v_item_instance_id,0) AS item_instance_id,
           COALESCE(v_item_data_id,0) AS item_data_id,
           COALESCE(v_quantity,0) AS quantity,
           COALESCE(v_currency_id,0) AS currency_id,
           COALESCE(v_currency_amount,0) AS currency_amount,
           v_balance AS currency_balance,
           v_mail_state AS mail_state;
END$$
DELIMITER ;

CALL sp_gd_migrate_mail_auto_increment();
DROP PROCEDURE sp_gd_migrate_mail_auto_increment;

USE gamedb;

DROP PROCEDURE IF EXISTS sp_gd_u_currency_debit;
DROP PROCEDURE IF EXISTS sp_gd_cu_currency_credit;

DELIMITER $$

CREATE PROCEDURE sp_gd_u_currency_debit
(
    IN p_user_id BIGINT UNSIGNED,
    IN p_currency_id SMALLINT UNSIGNED,
    IN p_amount BIGINT UNSIGNED
)
BEGIN
    IF p_amount = 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_AMOUNT';
    END IF;

    UPDATE player_currencies
       SET amount = amount - p_amount,
           version = version + 1
     WHERE user_id = p_user_id
       AND currency_id = p_currency_id
       AND amount >= p_amount;

    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INSUFFICIENT_CURRENCY';
    END IF;

    SELECT amount, version
      FROM player_currencies
     WHERE user_id = p_user_id
       AND currency_id = p_currency_id;
END$$

CREATE PROCEDURE sp_gd_cu_currency_credit
(
    IN p_user_id BIGINT UNSIGNED,
    IN p_currency_id SMALLINT UNSIGNED,
    IN p_amount BIGINT UNSIGNED,
    IN p_max_amount BIGINT UNSIGNED
)
BEGIN
    IF p_amount = 0 OR p_amount > p_max_amount THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_AMOUNT';
    END IF;

    INSERT INTO player_currencies(user_id, currency_id, amount, version)
    VALUES(p_user_id, p_currency_id, 0, 0)
    ON DUPLICATE KEY UPDATE user_id = user_id;

    UPDATE player_currencies
       SET amount = amount + p_amount,
           version = version + 1
     WHERE user_id = p_user_id
       AND currency_id = p_currency_id
       AND amount <= p_max_amount - p_amount;

    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'CURRENCY_LIMIT_EXCEEDED';
    END IF;

    SELECT amount, version
      FROM player_currencies
     WHERE user_id = p_user_id
       AND currency_id = p_currency_id;
END$$

DELIMITER ;

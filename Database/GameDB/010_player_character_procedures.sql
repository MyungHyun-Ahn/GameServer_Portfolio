USE gamedb;

DROP PROCEDURE IF EXISTS sp_gd_c_player_character;
DROP PROCEDURE IF EXISTS sp_gd_r_player_character;
DROP PROCEDURE IF EXISTS sp_gd_u_player_experience;
DROP PROCEDURE IF EXISTS sp_gd_u_player_stat_allocation;

DELIMITER $$

CREATE PROCEDURE sp_gd_c_player_character
(
    IN p_user_id BIGINT UNSIGNED,
    IN p_character_data_id INT UNSIGNED,
    IN p_initial_level INT UNSIGNED,
    IN p_initial_exp BIGINT UNSIGNED,
    IN p_initial_str INT UNSIGNED,
    IN p_initial_dex INT UNSIGNED,
    IN p_initial_int INT UNSIGNED,
    IN p_initial_luk INT UNSIGNED,
    IN p_initial_unspent_stat_points INT UNSIGNED
)
BEGIN
    DECLARE EXIT HANDLER FOR 1062
    BEGIN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'PLAYER_CHARACTER_ALREADY_EXISTS';
    END;

    IF p_user_id IS NULL OR p_user_id = 0 OR
       p_character_data_id IS NULL OR p_character_data_id = 0 OR
       p_initial_level IS NULL OR p_initial_level = 0 OR
       p_initial_exp IS NULL OR
       p_initial_str IS NULL OR p_initial_dex IS NULL OR
       p_initial_int IS NULL OR p_initial_luk IS NULL OR
       p_initial_unspent_stat_points IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_PLAYER_CHARACTER';
    END IF;

    -- 초기 레벨, 스탯과 포인트는 Character GameData에서 읽은 값을
    -- CacheServer가 명시적으로 전달한다. DB에는 밸런스 기본값을 두지 않는다.
    INSERT INTO player_characters
    (
        user_id, character_data_id, level, exp,
        stat_str, stat_dex, stat_int, stat_luk,
        unspent_stat_points, progress_version, stat_version
    )
    VALUES
    (
        p_user_id, p_character_data_id, p_initial_level, p_initial_exp,
        p_initial_str, p_initial_dex, p_initial_int, p_initial_luk,
        p_initial_unspent_stat_points, 1, 1
    );

    SELECT character_id, user_id, character_data_id, level, exp,
           stat_str, stat_dex, stat_int, stat_luk, unspent_stat_points,
           progress_version, stat_version, created_at, updated_at
      FROM player_characters
     WHERE character_id = LAST_INSERT_ID();
END$$

CREATE PROCEDURE sp_gd_r_player_character
(
    IN p_user_id BIGINT UNSIGNED
)
BEGIN
    IF p_user_id IS NULL OR p_user_id = 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_USER_ID';
    END IF;

    SELECT character_id, user_id, character_data_id, level, exp,
           stat_str, stat_dex, stat_int, stat_luk, unspent_stat_points,
           progress_version, stat_version, created_at, updated_at
      FROM player_characters
     WHERE user_id = p_user_id;
END$$

CREATE PROCEDURE sp_gd_u_player_experience
(
    IN p_user_id BIGINT UNSIGNED,
    IN p_expected_progress_version BIGINT UNSIGNED,
    IN p_new_level INT UNSIGNED,
    IN p_new_exp BIGINT UNSIGNED,
    IN p_stat_point_reward INT UNSIGNED
)
BEGIN
    IF p_user_id IS NULL OR p_user_id = 0 OR
       p_expected_progress_version IS NULL OR p_expected_progress_version = 0 OR
       p_new_level IS NULL OR p_new_level = 0 OR
       p_new_exp IS NULL OR p_stat_point_reward IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_PLAYER_EXPERIENCE';
    END IF;

    -- CacheServer가 CharacterLevel GameData를 이용해 새 레벨/잔여 EXP와
    -- 여러 레벨의 StatPointReward 합계를 계산한다. 이 조건부 UPDATE가
    -- 진행 상태와 포인트 지급을 같은 row 변경으로 원자화한다.
    UPDATE player_characters
       SET level = p_new_level,
           exp = p_new_exp,
           unspent_stat_points = unspent_stat_points + p_stat_point_reward,
           progress_version = progress_version + 1,
           stat_version = stat_version + IF(p_stat_point_reward > 0, 1, 0)
     WHERE user_id = p_user_id
       AND progress_version = p_expected_progress_version
       AND p_new_level >= level
       AND (p_new_level > level OR p_new_exp >= exp)
       AND (p_new_level > level OR p_stat_point_reward = 0)
       AND unspent_stat_points <= 4294967295 - p_stat_point_reward;

    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'PLAYER_EXPERIENCE_CONFLICT';
    END IF;

    SELECT character_id, user_id, character_data_id, level, exp,
           stat_str, stat_dex, stat_int, stat_luk, unspent_stat_points,
           progress_version, stat_version, created_at, updated_at
      FROM player_characters
     WHERE user_id = p_user_id;
END$$

CREATE PROCEDURE sp_gd_u_player_stat_allocation
(
    IN p_user_id BIGINT UNSIGNED,
    IN p_expected_stat_version BIGINT UNSIGNED,
    IN p_add_str INT UNSIGNED,
    IN p_add_dex INT UNSIGNED,
    IN p_add_int INT UNSIGNED,
    IN p_add_luk INT UNSIGNED
)
BEGIN
    DECLARE v_required_points BIGINT UNSIGNED;

    IF p_user_id IS NULL OR p_user_id = 0 OR
       p_expected_stat_version IS NULL OR p_expected_stat_version = 0 OR
       p_add_str IS NULL OR p_add_dex IS NULL OR
       p_add_int IS NULL OR p_add_luk IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_PLAYER_STAT_ALLOCATION';
    END IF;

    SET v_required_points = CAST(p_add_str AS UNSIGNED) +
                            CAST(p_add_dex AS UNSIGNED) +
                            CAST(p_add_int AS UNSIGNED) +
                            CAST(p_add_luk AS UNSIGNED);

    IF v_required_points = 0 OR v_required_points > 4294967295 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'INVALID_PLAYER_STAT_ALLOCATION';
    END IF;

    -- 보유 포인트 검사, 네 스탯 증가, 포인트 차감과 Version 증가는
    -- 하나의 조건부 UPDATE로 실행되어 동시 요청 중 하나만 성공한다.
    UPDATE player_characters
       SET stat_str = stat_str + p_add_str,
           stat_dex = stat_dex + p_add_dex,
           stat_int = stat_int + p_add_int,
           stat_luk = stat_luk + p_add_luk,
           unspent_stat_points = unspent_stat_points - v_required_points,
           stat_version = stat_version + 1
     WHERE user_id = p_user_id
       AND stat_version = p_expected_stat_version
       AND unspent_stat_points >= v_required_points
       AND stat_str <= 4294967295 - p_add_str
       AND stat_dex <= 4294967295 - p_add_dex
       AND stat_int <= 4294967295 - p_add_int
       AND stat_luk <= 4294967295 - p_add_luk;

    IF ROW_COUNT() <> 1 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'PLAYER_STAT_ALLOCATION_CONFLICT';
    END IF;

    SELECT character_id, user_id, character_data_id, level, exp,
           stat_str, stat_dex, stat_int, stat_luk, unspent_stat_points,
           progress_version, stat_version, created_at, updated_at
      FROM player_characters
     WHERE user_id = p_user_id;
END$$

DELIMITER ;

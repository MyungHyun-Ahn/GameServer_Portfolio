USE gamedb;

-- 현 단계에서는 한 사용자에게 하나의 캐릭터만 허용한다.
-- 추후 캐릭터 슬롯을 도입할 때는 uq_player_character_user를
-- (user_id, slot_index) UNIQUE로 변경한다.
CREATE TABLE IF NOT EXISTS player_characters
(
    character_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    user_id BIGINT UNSIGNED NOT NULL,
    character_data_id INT UNSIGNED NOT NULL,
    level INT UNSIGNED NOT NULL,
    exp BIGINT UNSIGNED NOT NULL,
    stat_str INT UNSIGNED NOT NULL,
    stat_dex INT UNSIGNED NOT NULL,
    stat_int INT UNSIGNED NOT NULL,
    stat_luk INT UNSIGNED NOT NULL,
    unspent_stat_points INT UNSIGNED NOT NULL,
    progress_version BIGINT UNSIGNED NOT NULL,
    stat_version BIGINT UNSIGNED NOT NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (character_id),
    UNIQUE INDEX uq_player_character_user (user_id),
    INDEX idx_player_character_data (character_data_id, character_id),
    CONSTRAINT chk_player_character_user CHECK (user_id > 0),
    CONSTRAINT chk_player_character_data CHECK (character_data_id > 0),
    CONSTRAINT chk_player_character_level CHECK (level > 0),
    CONSTRAINT chk_player_character_progress_version CHECK (progress_version > 0),
    CONSTRAINT chk_player_character_stat_version CHECK (stat_version > 0)
) ENGINE = InnoDB;

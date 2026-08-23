CREATE DATABASE IF NOT EXISTS gamedb
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE gamedb;

CREATE TABLE IF NOT EXISTS inventory_items
(
    item_instance_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    owner_user_id BIGINT UNSIGNED NOT NULL,
    item_data_id INT UNSIGNED NOT NULL,
    quantity INT UNSIGNED NOT NULL,
    item_data JSON NOT NULL,
    is_equipped TINYINT(1) NOT NULL DEFAULT 0,
    is_tradable TINYINT(1) NOT NULL DEFAULT 1,
    version BIGINT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (item_instance_id),
    INDEX idx_inventory_owner (owner_user_id, item_instance_id),
    INDEX idx_inventory_item_data (owner_user_id, item_data_id, item_instance_id),
    CONSTRAINT chk_inventory_quantity CHECK (quantity > 0)
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS player_currencies
(
    user_id BIGINT UNSIGNED NOT NULL,
    currency_id SMALLINT UNSIGNED NOT NULL,
    amount BIGINT UNSIGNED NOT NULL DEFAULT 0,
    version BIGINT UNSIGNED NOT NULL DEFAULT 1,
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (user_id, currency_id)
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS mails
(
    mail_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    receiver_user_id BIGINT UNSIGNED NOT NULL,
    mail_type TINYINT UNSIGNED NOT NULL,
    subject VARCHAR(200) NOT NULL,
    body TEXT NOT NULL,
    state TINYINT UNSIGNED NOT NULL,
    expires_at DATETIME(6) NOT NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (mail_id),
    INDEX idx_mail_receiver_list (receiver_user_id, state, created_at DESC, mail_id DESC),
    INDEX idx_mail_expiration (state, expires_at)
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS mail_attachments
(
    attachment_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    mail_id BIGINT UNSIGNED NOT NULL,
    attachment_type TINYINT UNSIGNED NOT NULL,
    item_instance_id BIGINT UNSIGNED NULL,
    item_data_id INT UNSIGNED NULL,
    quantity INT UNSIGNED NULL,
    item_data JSON NULL,
    currency_id SMALLINT UNSIGNED NULL,
    currency_amount BIGINT UNSIGNED NULL,
    state TINYINT UNSIGNED NOT NULL,
    claimed_at DATETIME(6) NULL,
    PRIMARY KEY (attachment_id),
    INDEX idx_attachment_mail (mail_id, state, attachment_id),
    UNIQUE INDEX uq_attachment_item (item_instance_id),
    CONSTRAINT chk_attachment_payload CHECK
    (
        (attachment_type = 1 AND item_instance_id IS NOT NULL AND item_data_id IS NOT NULL
            AND quantity > 0 AND item_data IS NOT NULL
            AND currency_id IS NULL AND currency_amount IS NULL)
        OR
        (attachment_type = 2 AND item_instance_id IS NULL AND item_data_id IS NULL
            AND quantity IS NULL AND item_data IS NULL
            AND currency_id IS NOT NULL AND currency_amount > 0)
    )
) ENGINE = InnoDB;

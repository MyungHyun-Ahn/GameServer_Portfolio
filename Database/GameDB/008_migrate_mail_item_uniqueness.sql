USE gamedb;

DROP PROCEDURE IF EXISTS sp_gd_migrate_mail_item_uniqueness;

DELIMITER $$
CREATE PROCEDURE sp_gd_migrate_mail_item_uniqueness()
BEGIN
    IF EXISTS
    (
        SELECT 1
          FROM information_schema.statistics
         WHERE table_schema = 'gamedb'
           AND table_name = 'mail_attachments'
           AND index_name = 'uq_attachment_item'
    ) THEN
        ALTER TABLE mail_attachments DROP INDEX uq_attachment_item;
    END IF;

    IF NOT EXISTS
    (
        SELECT 1
          FROM information_schema.columns
         WHERE table_schema = 'gamedb'
           AND table_name = 'mail_attachments'
           AND column_name = 'claimable_item_instance_id'
    ) THEN
        ALTER TABLE mail_attachments
            ADD COLUMN claimable_item_instance_id BIGINT UNSIGNED
                GENERATED ALWAYS AS
                    (CASE WHEN attachment_type = 1 AND state = 1 THEN item_instance_id ELSE NULL END) STORED;
    END IF;

    IF NOT EXISTS
    (
        SELECT 1
          FROM information_schema.statistics
         WHERE table_schema = 'gamedb'
           AND table_name = 'mail_attachments'
           AND index_name = 'idx_attachment_item_history'
    ) THEN
        ALTER TABLE mail_attachments
            ADD INDEX idx_attachment_item_history (item_instance_id, attachment_id);
    END IF;

    IF NOT EXISTS
    (
        SELECT 1
          FROM information_schema.statistics
         WHERE table_schema = 'gamedb'
           AND table_name = 'mail_attachments'
           AND index_name = 'uq_claimable_attachment_item'
    ) THEN
        ALTER TABLE mail_attachments
            ADD UNIQUE INDEX uq_claimable_attachment_item (claimable_item_instance_id);
    END IF;
END$$
DELIMITER ;

CALL sp_gd_migrate_mail_item_uniqueness();
DROP PROCEDURE sp_gd_migrate_mail_item_uniqueness;

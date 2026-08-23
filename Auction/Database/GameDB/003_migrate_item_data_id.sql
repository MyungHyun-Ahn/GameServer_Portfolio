USE gamedb;

DROP PROCEDURE IF EXISTS sp_gd_migrate_item_data_id;
DELIMITER $$
CREATE PROCEDURE sp_gd_migrate_item_data_id()
BEGIN
    IF EXISTS (SELECT 1 FROM information_schema.columns
               WHERE table_schema='gamedb' AND table_name='inventory_items' AND column_name='item_template_id')
       AND NOT EXISTS (SELECT 1 FROM information_schema.columns
                       WHERE table_schema='gamedb' AND table_name='inventory_items' AND column_name='item_data_id') THEN
        ALTER TABLE inventory_items RENAME COLUMN item_template_id TO item_data_id;
    END IF;

    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_schema='gamedb' AND table_name='inventory_items'
                     AND column_name='item_instance_id' AND extra LIKE '%auto_increment%') THEN
        ALTER TABLE inventory_items
            MODIFY COLUMN item_instance_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT;
    END IF;

    IF EXISTS (SELECT 1 FROM information_schema.statistics
               WHERE table_schema='gamedb' AND table_name='inventory_items' AND index_name='idx_inventory_template')
       AND NOT EXISTS (SELECT 1 FROM information_schema.statistics
                       WHERE table_schema='gamedb' AND table_name='inventory_items' AND index_name='idx_inventory_item_data') THEN
        ALTER TABLE inventory_items RENAME INDEX idx_inventory_template TO idx_inventory_item_data;
    END IF;

    IF EXISTS (SELECT 1 FROM information_schema.columns
               WHERE table_schema='gamedb' AND table_name='mail_attachments' AND column_name='item_template_id')
       AND NOT EXISTS (SELECT 1 FROM information_schema.columns
                       WHERE table_schema='gamedb' AND table_name='mail_attachments' AND column_name='item_data_id') THEN
        ALTER TABLE mail_attachments DROP CHECK chk_attachment_payload;
        ALTER TABLE mail_attachments RENAME COLUMN item_template_id TO item_data_id;
        ALTER TABLE mail_attachments ADD CONSTRAINT chk_attachment_payload CHECK
        (
            (attachment_type = 1 AND item_instance_id IS NOT NULL AND item_data_id IS NOT NULL
                AND quantity > 0 AND item_data IS NOT NULL
                AND currency_id IS NULL AND currency_amount IS NULL)
            OR
            (attachment_type = 2 AND item_instance_id IS NULL AND item_data_id IS NULL
                AND quantity IS NULL AND item_data IS NULL
                AND currency_id IS NOT NULL AND currency_amount > 0)
        );
    END IF;
END$$
DELIMITER ;

CALL sp_gd_migrate_item_data_id();
DROP PROCEDURE sp_gd_migrate_item_data_id;

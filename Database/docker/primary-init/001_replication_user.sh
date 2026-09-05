#!/bin/bash
set -e

mysql --protocol=socket -uroot -p"${MYSQL_ROOT_PASSWORD}" <<EOSQL
CREATE USER IF NOT EXISTS 'auction_replicator'@'%' IDENTIFIED BY '${MYSQL_PASSWORD}';
GRANT REPLICATION SLAVE, REPLICATION CLIENT ON *.* TO 'auction_replicator'@'%';
FLUSH PRIVILEGES;
EOSQL

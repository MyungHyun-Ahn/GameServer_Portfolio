import mysql from "mysql2/promise";

import { appEnv } from "../config/env";

let pool: mysql.Pool | null = null;

export function getMySqlPool(): mysql.Pool {
  if (pool !== null) {
    return pool;
  }

  pool = mysql.createPool({
    host: appEnv.mysql.host,
    port: appEnv.mysql.port,
    user: appEnv.mysql.user,
    password: appEnv.mysql.password,
    database: appEnv.mysql.database,
    connectionLimit: appEnv.mysql.connectionLimit,
    charset: "utf8mb4",
    namedPlaceholders: true,
  });

  return pool;
}

export async function verifyMySqlConnection(): Promise<void> {
  const connection = await getMySqlPool().getConnection();
  try {
    await connection.ping();
  } finally {
    connection.release();
  }
}

export async function closeMySqlPool(): Promise<void> {
  if (pool === null) {
    return;
  }

  const currentPool = pool;
  pool = null;
  await currentPool.end();
}

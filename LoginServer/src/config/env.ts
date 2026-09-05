import dotenv from "dotenv";

dotenv.config();

function getRequiredString(name: string, defaultValue?: string): string {
  const raw = process.env[name] ?? defaultValue;
  if (raw === undefined) {
    throw new Error(`Missing required environment variable: ${name}`);
  }

  const value = raw.trim();
  if (value.length === 0) {
    throw new Error(`Environment variable is empty: ${name}`);
  }

  return value;
}

function getOptionalString(name: string, defaultValue = ""): string {
  return (process.env[name] ?? defaultValue).trim();
}

function getNumber(name: string, defaultValue: number): number {
  const raw = process.env[name];
  if (raw === undefined || raw.trim().length === 0) {
    return defaultValue;
  }

  const value = Number.parseInt(raw, 10);
  if (Number.isNaN(value)) {
    throw new Error(`Environment variable is not a valid number: ${name}`);
  }

  return value;
}

function getUInt32(name: string, defaultValue: number): number {
  const value = getNumber(name, defaultValue);
  if (value < 1 || value > 0xffff_ffff) {
    throw new Error(`Environment variable must be in the uint32 range: ${name}`);
  }

  return value;
}

export const appEnv = {
  server: {
    host: getRequiredString("LOGIN_SERVER_HOST", "127.0.0.1"),
    port: getNumber("LOGIN_SERVER_PORT", 18080),
  },
  mysql: {
    host: getRequiredString("MYSQL_HOST", "127.0.0.1"),
    port: getNumber("MYSQL_PORT", 3306),
    user: getRequiredString("MYSQL_USER", "appuser"),
    password: getRequiredString("MYSQL_PASSWORD"),
    database: getRequiredString("MYSQL_DATABASE", "accountdb"),
    connectionLimit: getNumber("MYSQL_CONNECTION_LIMIT", 10),
  },
  redis: {
    host: getRequiredString("REDIS_HOST", "127.0.0.1"),
    port: getNumber("REDIS_PORT", 6379),
    password: getOptionalString("REDIS_PASSWORD"),
    database: getNumber("REDIS_DATABASE", 0),
    connectTimeoutMs: getNumber("REDIS_CONNECT_TIMEOUT_MS", 3000),
  },
  chatServer: {
    ip: getRequiredString("CHAT_SERVER_IP", "127.0.0.1"),
    port: getNumber("CHAT_SERVER_PORT", 19100),
  },
  auctionServer: {
    ip: getRequiredString("AUCTION_SERVER_IP", "127.0.0.1"),
    port: getNumber("AUCTION_SERVER_PORT", 19102),
  },
  worldServer: {
    ip: getRequiredString("WORLD_SERVER_IP", "127.0.0.1"),
    port: getNumber("WORLD_SERVER_PORT", 19200),
    instanceId: getUInt32("WORLD_SERVER_INSTANCE_ID", 1),
  },
  ticket: {
    keyPrefix: getRequiredString("CHAT_TICKET_KEY_PREFIX", "chat:ticket:"),
    auctionKeyPrefix: getRequiredString("AUCTION_TICKET_KEY_PREFIX", "auction:ticket:"),
    worldKeyPrefix: getRequiredString("WORLD_TICKET_KEY_PREFIX", "world:ticket:"),
    ttlSeconds: getNumber("CHAT_TICKET_TTL_SECONDS", 60),
  },
  argon2: {
    memoryCostKiB: getNumber("ARGON2_MEMORY_COST_KIB", 19456),
    timeCost: getNumber("ARGON2_TIME_COST", 2),
    parallelism: getNumber("ARGON2_PARALLELISM", 1),
    hashLength: getNumber("ARGON2_HASH_LENGTH", 32),
  },
} as const;

export type AppEnv = typeof appEnv;

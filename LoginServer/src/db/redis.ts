import { createClient } from "redis";

import { appEnv } from "../config/env";

type AppRedisClient = ReturnType<typeof createClient>;

let client: AppRedisClient | null = null;
let connectPromise: Promise<AppRedisClient> | null = null;

function createRedisClient(): AppRedisClient {
  const redisClient = createClient({
    socket: {
      host: appEnv.redis.host,
      port: appEnv.redis.port,
      connectTimeout: appEnv.redis.connectTimeoutMs,
    },
    password: appEnv.redis.password.length > 0 ? appEnv.redis.password : undefined,
    database: appEnv.redis.database,
  });

  redisClient.on("error", (error) => {
    console.error("[LoginServer] Redis error:", error);
  });

  return redisClient;
}

export async function getRedisClient(): Promise<AppRedisClient> {
  if (client !== null && client.isOpen) {
    return client;
  }

  if (connectPromise !== null) {
    return connectPromise;
  }

  if (client === null) {
    client = createRedisClient();
  }

  connectPromise = client.connect().then(() => {
    const connectedClient = client;
    connectPromise = null;
    if (connectedClient === null) {
      throw new Error("Redis client initialization failed.");
    }

    return connectedClient;
  }).catch((error) => {
    connectPromise = null;
    throw error;
  });

  return connectPromise;
}

export async function verifyRedisConnection(): Promise<void> {
  const redisClient = await getRedisClient();
  await redisClient.ping();
}

export async function closeRedisClient(): Promise<void> {
  if (client === null) {
    return;
  }

  const currentClient = client;
  client = null;
  connectPromise = null;

  if (currentClient.isOpen) {
    await currentClient.quit();
  }
}

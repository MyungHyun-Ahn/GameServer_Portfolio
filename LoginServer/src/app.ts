import express, { type Request, type Response } from "express";
import swaggerUi from "swagger-ui-express";

import { appEnv } from "./config/env";
import { AuthController } from "./controllers/auth-controller";
import { closeMySqlPool, verifyMySqlConnection } from "./db/mysql";
import { closeRedisClient, verifyRedisConnection } from "./db/redis";
import { createAuthRouter } from "./routes/auth";
import { AccountService } from "./services/account-service";
import { LoginTicketService } from "./services/login-ticket-service";
import { getOpenApiYamlPath, loadOpenApiDocument } from "./utils/openapi";

function createApp(): express.Express {
  const app = express();
  const authController = new AuthController(
    new AccountService(),
    new LoginTicketService(),
  );

  app.use(express.json({ limit: "16kb" }));
  app.use("/docs", swaggerUi.serve, swaggerUi.setup(loadOpenApiDocument(), {
    explorer: true,
    customSiteTitle: "Game Server Portfolio LoginServer API",
  }));

  app.use("/auth", createAuthRouter(authController));
  app.get("/openapi.yaml", (_request: Request, response: Response) => {
    response.sendFile(getOpenApiYamlPath());
  });
  app.get("/openapi.json", (_request: Request, response: Response) => {
    response.json(loadOpenApiDocument());
  });
  app.get("/healthz", (request: Request, response: Response) => {
    authController.healthz(request, response).catch((error: unknown) => {
      AuthController.writeError(response, error);
    });
  });

  app.use((_request, response) => {
    response.status(404).json({
      success: false,
      message: "Route not found.",
    });
  });

  return app;
}

async function bootstrap(): Promise<void> {
  await verifyMySqlConnection();
  await verifyRedisConnection();

  const app = createApp();
  const server = app.listen(appEnv.server.port, appEnv.server.host, () => {
    console.log(
      `[LoginServer] listening on http://${appEnv.server.host}:${appEnv.server.port}`,
    );
  });

  const shutdown = async (signal: string): Promise<void> => {
    console.log(`[LoginServer] shutdown signal received: ${signal}`);

    await new Promise<void>((resolve, reject) => {
      server.close((error) => {
        if (error) {
          reject(error);
          return;
        }

        resolve();
      });
    });

    await closeRedisClient();
    await closeMySqlPool();
    process.exit(0);
  };

  process.on("SIGINT", () => {
    void shutdown("SIGINT");
  });

  process.on("SIGTERM", () => {
    void shutdown("SIGTERM");
  });
}

bootstrap().catch(async (error: unknown) => {
  console.error("[LoginServer] bootstrap failed:", error);
  await closeRedisClient();
  await closeMySqlPool();
  process.exit(1);
});

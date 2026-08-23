import { Router } from "express";

import { AuthController } from "../controllers/auth-controller";

export function createAuthRouter(authController: AuthController): Router {
  const router = Router();

  router.post("/register", (request, response) => {
    authController.register(request, response).catch((error: unknown) => {
      AuthController.writeError(response, error);
    });
  });

  router.post("/login", (request, response) => {
    authController.login(request, response).catch((error: unknown) => {
      AuthController.writeError(response, error);
    });
  });

  return router;
}

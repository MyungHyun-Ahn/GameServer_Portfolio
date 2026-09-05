import type { Request, Response } from "express";

import { verifyMySqlConnection } from "../db/mysql";
import { verifyRedisConnection } from "../db/redis";
import type { ErrorResponse, LoginRequest, RegisterRequest } from "../models/auth-types";
import { AccountService, getErrorCode, getErrorStatusCode } from "../services/account-service";
import { LoginTicketService } from "../services/login-ticket-service";

export class AuthController {
  public constructor(
    private readonly accountService: AccountService,
    private readonly loginTicketService: LoginTicketService,
  ) {}

  public register = async (request: Request, response: Response): Promise<void> => {
    const registeredAccount = await this.accountService.registerAccount(request.body as RegisterRequest);
    response.status(201).json({
      success: true,
      userId: registeredAccount.userId,
      nickname: registeredAccount.nickname,
    });
  };

  public login = async (request: Request, response: Response): Promise<void> => {
    const authenticatedAccount = await this.accountService.authenticate(request.body as LoginRequest);
    const issuedTicket = await this.loginTicketService.issueTicket(authenticatedAccount);

    response.status(200).json({
      success: true,
      userId: authenticatedAccount.userId,
      nickname: authenticatedAccount.nickname,
      ticket: issuedTicket.ticket,
      auctionTicket: issuedTicket.auctionTicket,
      worldTicket: issuedTicket.worldTicket,
      ticketExpiresInSeconds: issuedTicket.ttlSeconds,
      chatServer: issuedTicket.chatServer,
      auctionServer: issuedTicket.auctionServer,
      worldServer: issuedTicket.worldServer,
    });
  };

  public healthz = async (_request: Request, response: Response): Promise<void> => {
    let mysql = "ok";
    let redis = "ok";
    const errors: string[] = [];

    try {
      await verifyMySqlConnection();
    } catch (error) {
      mysql = "error";
      errors.push(`mysql: ${getErrorMessage(error)}`);
    }

    try {
      await verifyRedisConnection();
    } catch (error) {
      redis = "error";
      errors.push(`redis: ${getErrorMessage(error)}`);
    }

    const isHealthy = errors.length === 0;
    response.status(isHealthy ? 200 : 503).json({
      success: isHealthy,
      mysql,
      redis,
      errors,
    });
  };

  public static writeError(response: Response, error: unknown): void {
    const statusCode = getErrorStatusCode(error) ?? 500;
    const body: ErrorResponse = {
      success: false,
      code: getErrorCode(error) ?? "INTERNAL_SERVER_ERROR",
      message: getErrorMessage(error),
    };

    response.status(statusCode).json(body);
  }
}

function getErrorMessage(error: unknown): string {
  if (error instanceof Error) {
    return error.message;
  }

  if (typeof error === "string") {
    return error;
  }

  return "Unexpected error.";
}

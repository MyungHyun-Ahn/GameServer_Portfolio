import { randomUUID } from "crypto";

import { appEnv } from "../config/env";
import { getRedisClient } from "../db/redis";
import type { AuthenticatedAccount, ChatServerEndpoint } from "../models/auth-types";

export interface IssuedChatTicket {
  ticket: string;
  auctionTicket: string;
  ttlSeconds: number;
  chatServer: ChatServerEndpoint;
  auctionServer: ChatServerEndpoint;
}

const kActiveLoginKeyPrefix = "chat:active-login:";

export class ChatTicketService {
  public async issueTicket(account: AuthenticatedAccount): Promise<IssuedChatTicket> {
    const ticket = randomUUID();
    const auctionTicket = randomUUID();
    const redisClient = await getRedisClient();
    const loginVersion = await redisClient.incr(this.buildActiveLoginKey(account.userId));

    const payload = this.buildTicketPayload(account.userId, loginVersion, account.loginId);
    await Promise.all([
      redisClient.set(this.buildTicketKey(ticket), payload, { EX: appEnv.ticket.ttlSeconds }),
      redisClient.set(this.buildAuctionTicketKey(auctionTicket), payload, { EX: appEnv.ticket.ttlSeconds }),
    ]);

    return {
      ticket,
      auctionTicket,
      ttlSeconds: appEnv.ticket.ttlSeconds,
      chatServer: {
        ip: appEnv.chatServer.ip,
        port: appEnv.chatServer.port,
      },
      auctionServer: {
        ip: appEnv.auctionServer.ip,
        port: appEnv.auctionServer.port,
      },
    };
  }

  private buildTicketKey(ticket: string): string {
    return `${appEnv.ticket.keyPrefix}${ticket}`;
  }

  private buildAuctionTicketKey(ticket: string): string {
    return `${appEnv.ticket.auctionKeyPrefix}${ticket}`;
  }

  private buildActiveLoginKey(userId: number): string {
    return `${kActiveLoginKeyPrefix}${userId}`;
  }

  private buildTicketPayload(userId: number, loginVersion: number, loginId: string): string {
    const encodedLoginId = Buffer.from(loginId, "utf8").toString("hex");
    return `${userId}:${loginVersion}:${encodedLoginId}`;
  }
}

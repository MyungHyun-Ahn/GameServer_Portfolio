import { randomUUID } from "crypto";

import { appEnv } from "../config/env";
import { getRedisClient } from "../db/redis";
import type {
  AuthenticatedAccount,
  ChatServerEndpoint,
  WorldServerEndpoint,
} from "../models/auth-types";
import { buildLoginTicketPayload, buildWorldTicketPayload } from "./ticket-payload";

export interface IssuedLoginTickets {
  ticket: string;
  auctionTicket: string;
  worldTicket: string;
  ttlSeconds: number;
  chatServer: ChatServerEndpoint;
  auctionServer: ChatServerEndpoint;
  worldServer: WorldServerEndpoint;
}

const kActiveLoginKeyPrefix = "chat:active-login:";

export class LoginTicketService {
  public async issueTicket(account: AuthenticatedAccount): Promise<IssuedLoginTickets> {
    const ticket = randomUUID();
    const auctionTicket = randomUUID();
    const worldTicket = randomUUID();
    const redisClient = await getRedisClient();
    const loginVersion = await redisClient.incr(this.buildActiveLoginKey(account.userId));

    const payload = buildLoginTicketPayload(account.userId, loginVersion, account.loginId);
    const worldPayload = buildWorldTicketPayload(
      account.userId,
      loginVersion,
      appEnv.worldServer.instanceId,
      account.loginId,
    );
    await Promise.all([
      redisClient.set(this.buildTicketKey(ticket), payload, { EX: appEnv.ticket.ttlSeconds }),
      redisClient.set(this.buildAuctionTicketKey(auctionTicket), payload, {
        EX: appEnv.ticket.ttlSeconds,
      }),
      redisClient.set(this.buildWorldTicketKey(worldTicket), worldPayload, {
        EX: appEnv.ticket.ttlSeconds,
      }),
    ]);

    return {
      ticket,
      auctionTicket,
      worldTicket,
      ttlSeconds: appEnv.ticket.ttlSeconds,
      chatServer: {
        ip: appEnv.chatServer.ip,
        port: appEnv.chatServer.port,
      },
      auctionServer: {
        ip: appEnv.auctionServer.ip,
        port: appEnv.auctionServer.port,
      },
      worldServer: {
        ip: appEnv.worldServer.ip,
        port: appEnv.worldServer.port,
        instanceId: appEnv.worldServer.instanceId,
      },
    };
  }

  private buildTicketKey(ticket: string): string {
    return `${appEnv.ticket.keyPrefix}${ticket}`;
  }

  private buildAuctionTicketKey(ticket: string): string {
    return `${appEnv.ticket.auctionKeyPrefix}${ticket}`;
  }

  private buildWorldTicketKey(ticket: string): string {
    return `${appEnv.ticket.worldKeyPrefix}${ticket}`;
  }

  private buildActiveLoginKey(userId: number): string {
    return `${kActiveLoginKeyPrefix}${userId}`;
  }

}

export interface RegisterRequest {
  loginId: string;
  password: string;
  nickname: string;
}

export interface LoginRequest {
  loginId: string;
  password: string;
}

export interface ChatServerEndpoint {
  ip: string;
  port: number;
}

export type AuctionServerEndpoint = ChatServerEndpoint;

export interface WorldServerEndpoint extends ChatServerEndpoint {
  instanceId: number;
}

export interface RegisterResponse {
  success: true;
  userId: number;
  nickname: string;
}

export interface LoginResponse {
  success: true;
  userId: number;
  nickname: string;
  ticket: string;
  auctionTicket: string;
  worldTicket: string;
  ticketExpiresInSeconds: number;
  chatServer: ChatServerEndpoint;
  auctionServer: AuctionServerEndpoint;
  worldServer: WorldServerEndpoint;
}

export interface ErrorResponse {
  success: false;
  code: string;
  message: string;
}

export interface AccountRecord {
  userId: number;
  loginId: string;
  passwordHash: string;
  nickname: string;
  status: number;
}

export interface AuthenticatedAccount {
  userId: number;
  loginId: string;
  nickname: string;
}

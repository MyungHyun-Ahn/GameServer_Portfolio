import argon2 from "argon2";
import type { ResultSetHeader, RowDataPacket } from "mysql2";

import { appEnv } from "../config/env";
import { getMySqlPool } from "../db/mysql";
import type {
  AccountRecord,
  AuthenticatedAccount,
  LoginRequest,
  RegisterRequest,
} from "../models/auth-types";

class AuthServiceError extends Error {
  public constructor(
    public readonly statusCode: number,
    public readonly code: string,
    message: string,
  ) {
    super(message);
  }
}

class ValidationError extends AuthServiceError {
  public constructor(code: string, message: string) {
    super(400, code, message);
  }
}

class ConflictError extends AuthServiceError {
  public constructor(code: string, message: string) {
    super(409, code, message);
  }
}

class AuthenticationError extends AuthServiceError {
  public constructor(code: string, message: string) {
    super(401, code, message);
  }
}

interface AccountRow extends RowDataPacket {
  account_id: number;
  login_id: string;
  password_hash: string;
  nickname: string;
  status: number;
}

function normalizeRequiredString(value: unknown, fieldName: string, maxLength: number): string {
  if (typeof value !== "string") {
    throw new ValidationError(`${getFieldErrorCodePrefix(fieldName)}_REQUIRED`, `${fieldName} must be a string.`);
  }

  const normalized = value.trim();
  if (normalized.length === 0) {
    throw new ValidationError(`${getFieldErrorCodePrefix(fieldName)}_REQUIRED`, `${fieldName} is required.`);
  }

  if (normalized.length > maxLength) {
    throw new ValidationError(`${getFieldErrorCodePrefix(fieldName)}_TOO_LONG`, `${fieldName} is too long.`);
  }

  return normalized;
}

function getFieldErrorCodePrefix(fieldName: string): string {
  switch (fieldName) {
  case "loginId":
    return "LOGIN_ID";
  case "password":
    return "PASSWORD";
  case "nickname":
    return "NICKNAME";
  default:
    return "FIELD";
  }
}

function createPasswordHash(password: string): Promise<string> {
  return argon2.hash(password, {
    type: argon2.argon2id,
    memoryCost: appEnv.argon2.memoryCostKiB,
    timeCost: appEnv.argon2.timeCost,
    parallelism: appEnv.argon2.parallelism,
    hashLength: appEnv.argon2.hashLength,
  });
}

function mapAccountRow(row: AccountRow): AccountRecord {
  return {
    userId: row.account_id,
    loginId: row.login_id,
    passwordHash: row.password_hash,
    nickname: row.nickname,
    status: row.status,
  };
}

export class AccountService {
  public async registerAccount(input: RegisterRequest): Promise<AuthenticatedAccount> {
    const loginId = normalizeRequiredString(input.loginId, "loginId", 64);
    const password = normalizeRequiredString(input.password, "password", 128);
    const nickname = normalizeRequiredString(input.nickname, "nickname", 64);

    const passwordHash = await createPasswordHash(password);

    try {
      const [result] = await getMySqlPool().execute<ResultSetHeader>(
        `INSERT INTO accounts (login_id, password_hash, nickname)
         VALUES (:loginId, :passwordHash, :nickname)`,
        {
          loginId,
          passwordHash,
          nickname,
        },
      );

      return {
        userId: Number(result.insertId),
        loginId,
        nickname,
      };
    } catch (error) {
      if (isDuplicateLoginIdError(error)) {
        throw new ConflictError("LOGIN_ID_ALREADY_EXISTS", "loginId already exists.");
      }

      throw error;
    }
  }

  public async authenticate(input: LoginRequest): Promise<AuthenticatedAccount> {
    const loginId = normalizeRequiredString(input.loginId, "loginId", 64);
    const password = normalizeRequiredString(input.password, "password", 128);

    const [rows] = await getMySqlPool().execute<AccountRow[]>(
      `SELECT account_id, login_id, password_hash, nickname, status
       FROM accounts
       WHERE login_id = :loginId
       LIMIT 1`,
      { loginId },
    );

    if (rows.length === 0) {
      throw new AuthenticationError("LOGIN_ID_NOT_FOUND", "loginId does not exist.");
    }

    const account = mapAccountRow(rows[0]);
    if (account.status !== 1) {
      throw new AuthenticationError("ACCOUNT_NOT_ACTIVE", "account is not active.");
    }

    const isVerified = await argon2.verify(account.passwordHash, password);
    if (!isVerified) {
      throw new AuthenticationError("PASSWORD_MISMATCH", "password is invalid.");
    }

    return {
      userId: account.userId,
      loginId: account.loginId,
      nickname: account.nickname,
    };
  }
}

function isDuplicateLoginIdError(error: unknown): boolean {
  if (typeof error !== "object" || error === null) {
    return false;
  }

  const mysqlError = error as { code?: string };
  return mysqlError.code === "ER_DUP_ENTRY";
}

export function getErrorStatusCode(error: unknown): number | null {
  if (typeof error !== "object" || error === null) {
    return null;
  }

  const maybeStatusCode = error as { statusCode?: number };
  return typeof maybeStatusCode.statusCode === "number" ? maybeStatusCode.statusCode : null;
}

export function getErrorCode(error: unknown): string | null {
  if (typeof error !== "object" || error === null) {
    return null;
  }

  const maybeCode = error as { code?: string };
  return typeof maybeCode.code === "string" && maybeCode.code.length > 0 ? maybeCode.code : null;
}

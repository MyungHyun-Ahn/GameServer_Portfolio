function encodeLoginId(loginId: string): string {
  return Buffer.from(loginId, "utf8").toString("hex");
}

export function buildLoginTicketPayload(
  userId: number,
  loginVersion: number,
  loginId: string,
): string {
  return `${userId}:${loginVersion}:${encodeLoginId(loginId)}`;
}

export function buildWorldTicketPayload(
  userId: number,
  loginVersion: number,
  targetServerInstanceId: number,
  loginId: string,
): string {
  return `${userId}:${loginVersion}:${targetServerInstanceId}:${encodeLoginId(loginId)}`;
}

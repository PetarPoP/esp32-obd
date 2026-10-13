import type { ObdInboundMessage } from "../types/protocol";

export function parseInboundMessage(payload: string): ObdInboundMessage | null {
  try {
    const candidate = JSON.parse(payload) as { type?: string };
    if (!candidate.type) {
      return null;
    }
    return candidate as ObdInboundMessage;
  } catch {
    return null;
  }
}

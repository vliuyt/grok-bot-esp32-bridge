import { randomUUID } from 'node:crypto';

export class BridgeError extends Error {
  constructor(code, message, status) { super(message); this.code = code; this.status = status; }
}
function allowedGateway(value) {
  const url = new URL(value);
  if (url.protocol !== 'https:' || url.username || url.password || url.port ||
      !['cursor.sh', 'cursor.com', 'cursorvm.com'].some(domain => url.hostname.endsWith(`.${domain}`))) {
    throw new BridgeError('ORIGIN_REFUSED', 'Unexpected gateway origin');
  }
  if (url.search || url.hash) throw new BridgeError('ORIGIN_REFUSED', 'Unexpected gateway origin');
  return url.toString().replace(/\/$/, '');
}
export class Backend {
  constructor(auth, { fetch = globalThis.fetch, version = '0.71.2' } = {}) {
    Object.defineProperties(this, { auth: { value: auth }, fetch: { value: fetch } });
    this.version = version;
  }
  async request(url, body, headers, uncertain = false, signal) {
    let response;
    try {
      response = await this.fetch(url, { method: 'POST', redirect: 'error',
        signal: signal ? AbortSignal.any([signal,AbortSignal.timeout(20000)]) : AbortSignal.timeout(20000), headers: { 'content-type': 'application/json', ...headers }, body: JSON.stringify(body) });
    } catch {
      throw new BridgeError(uncertain ? 'DELIVERY_UNKNOWN' : 'NETWORK', uncertain
        ? 'Task delivery is unknown; inspect the Bot chat before trying again' : 'Service connection failed; check networking');
    }
    if (!response.ok) {
      throw new BridgeError([401, 403].includes(response.status) ? 'AUTH_REQUIRED' : 'SERVICE_REFUSED',
        `Service answered HTTP ${response.status}`, response.status);
    }
    let data;
    try { data = await response.json(); } catch { throw new BridgeError('CONTRACT', 'Service returned an unreadable response'); }
    return data;
  }
  rpc(method, body, signal) {
    if (!['EnsureSandBox', 'MintSandVoiceCallSecret', 'VoiceCallHarnessSession', 'VoiceCallHarnessTool'].includes(method)) {
      throw new BridgeError('CONTRACT', 'Unsupported native RPC');
    }
    return this.request(`https://api2.cursor.sh/aiserver.v1.GrokBotService/${method}`, body, {
      authorization: `Bearer ${this.auth.accessToken}`, 'connect-protocol-version': '1',
      'x-cursor-client-type': 'sand', 'x-cursor-client-version': this.version,
      'x-sand-box-namespace': 'prod', 'x-ghost-mode': 'true', 'x-request-id': randomUUID(),
      'x-cursor-timezone': 'Asia/Shanghai',
      ...(this.auth.teamId ? { 'x-cursor-team-id': this.auth.teamId } : {})
    }, method === 'VoiceCallHarnessTool', signal);
  }
  async connectGateway(signal) {
    const reply = await this.rpc('EnsureSandBox', {}, signal);
    const url = allowedGateway(reply.gatewayUrl);
    if (typeof reply.gatewayToken !== 'string' || !reply.gatewayToken) throw new BridgeError('CONTRACT', 'Gateway credential missing');
    Object.defineProperty(this, 'gateway', { configurable: true, writable: true,
      value: { url, token: reply.gatewayToken, networkToken: reply.networkToken ?? reply.anyrunNetworkToken ?? reply.gatewayHeaders?.['x-anyrun-network-token'] } });
  }
  async gatewayCall(method, body = {}, signal) {
    if (!this.gateway) await this.connectGateway(signal);
    if (!['listAgents', 'getAgentTranscriptTail', 'readMainAgentContext', 'sendPrompt', 'nudgeVoiceCall', 'recordVoiceCall', 'setVoiceCallPresence'].includes(method)) {
      throw new BridgeError('CONTRACT', 'Unsupported gateway operation');
    }
    const request = () => this.request(`${allowedGateway(this.gateway.url)}/api/${method}`, body, {
      authorization: `Bearer ${this.gateway.token}`,
      ...(this.gateway.networkToken ? { 'x-anyrun-network-token': this.gateway.networkToken } : {})
    }, ['sendPrompt', 'nudgeVoiceCall'].includes(method), signal);
    try { return await request(); }
    catch (error) {
      // Only read operations and idempotent presence updates may renew routing.
      // Prompt delivery and call recording are never replayed automatically.
      if (!['listAgents','getAgentTranscriptTail','readMainAgentContext','setVoiceCallPresence'].includes(method) ||
          ![401,403,417].includes(error.status)) throw error;
      await this.connectGateway(signal);
      return request();
    }
  }
  async listBots() {
    const data = await this.gatewayCall('listAgents');
    const bots = Array.isArray(data) ? data : data.agents;
    if (!Array.isArray(bots)) throw new BridgeError('CONTRACT', 'Bot roster format changed');
    return bots.filter(bot => !bot.isGroup).map(bot => ({ id: bot.id ?? bot.agentId, name: bot.name ?? bot.title ?? '', description: bot.description ?? '' }));
  }
  async openEvents(signal) {
    try { return await this.openEventsOnce(signal); }
    catch (error) {
      if (signal.aborted || ![401,403,417].includes(error.status)) throw error;
      await this.connectGateway(signal);
      return this.openEventsOnce(signal);
    }
  }
  async openEventsOnce(signal) {
    if (!this.gateway) await this.connectGateway(signal);
    const deadline = new AbortController(), timeout = setTimeout(() => deadline.abort(), 20000);
    try {
      const response = await this.fetch(`${allowedGateway(this.gateway.url)}/events`, {
        method: 'GET', redirect: 'error', signal: AbortSignal.any([signal, deadline.signal]), headers: {
          accept: 'text/event-stream', authorization: `Bearer ${this.gateway.token}`,
          ...(this.gateway.networkToken ? { 'x-anyrun-network-token': this.gateway.networkToken } : {})
        }
      });
      if (!response.ok || !response.headers.get('content-type')?.startsWith('text/event-stream')) {
        await response.body?.cancel(); throw new BridgeError('TASK_RETURN_UNAVAILABLE', 'Native event stream unavailable', response.status);
      }
      return response.body;
    } finally { clearTimeout(timeout); }
  }
}

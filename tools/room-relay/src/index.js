'use strict';

const { createRelay } = require('./server');
const fs = require('node:fs');
const { LifecycleLog } = require('./logging');
const { createLifecycleSink } = require('./analytics');
const { assertAllowedOrigins } = require('./admission');

// Entry point. The relay never terminates TLS itself: in production it listens on loopback
// behind a reverse proxy that holds the certificate, and `RELAY_OBSERVED_TRANSPORT=wss` records
// what the proxy actually served. It needs no deploy keys; optional lifecycle analytics
// are the only configured outbound connection.

function parseOrigins(raw) {
  if (!raw) return [];
  // Validated here as well as in createRelay, so a typo in the unit file is a clean startup
  // failure rather than a browser that is quietly refused at the door.
  return assertAllowedOrigins(raw
    .split(',')
    .map((s) => s.trim())
    .filter((s) => s.length > 0));
}

function boolEnv(name, fallback) {
  const raw = process.env[name];
  if (raw === undefined) return fallback;
  return raw === '1' || raw.toLowerCase() === 'true';
}

function intEnv(name, fallback) {
  const raw = process.env[name];
  if (raw === undefined || !/^[0-9]{1,6}$/.test(raw)) return fallback;
  return Number.parseInt(raw, 10);
}

function configFromEnv(argv) {
  const dev = argv.includes('--dev');
  const host = process.env.RELAY_HOST || '127.0.0.1';
  const port = intEnv('RELAY_PORT', dev ? 8787 : 8787);
  const pollingEnabled = boolEnv('RELAY_HTTP_POLLING', false);
  const observedTransport = process.env.RELAY_OBSERVED_TRANSPORT || (pollingEnabled ? (dev ? 'http-poll' : 'https-poll') : dev ? 'ws' : 'wss');
  const publicSocketUrl = process.env.RELAY_PUBLIC_URL
    || (dev ? (pollingEnabled ? `http://127.0.0.1:${port}/v1/poll` : `ws://127.0.0.1:${port}/v1/socket`) : '');

  if (!dev && publicSocketUrl === '') {
    throw new Error('RELAY_PUBLIC_URL must be set (the wss:// URL the reverse proxy publishes)');
  }
  if (!dev && !publicSocketUrl.startsWith(pollingEnabled ? 'https://' : 'wss://')) {
    throw new Error('RELAY_PUBLIC_URL must use the secure scheme for the selected transport');
  }
  const gatewayKey = process.env.RELAY_GATEWAY_KEY_FILE
    ? fs.readFileSync(process.env.RELAY_GATEWAY_KEY_FILE, 'utf8').trim() : '';
  if (pollingEnabled && !dev && !/^[0-9a-f]{64}$/.test(gatewayKey)) {
    throw new Error('Production HTTP polling requires RELAY_GATEWAY_KEY_FILE');
  }
  if (gatewayKey && (!/^[0-9a-f]{64}$/.test(gatewayKey) || host !== '127.0.0.1')) {
    throw new Error('Gateway requires a 64-hex key and loopback binding');
  }

  const allowedOrigins = parseOrigins(process.env.RELAY_ALLOWED_ORIGINS);
  if (!dev && allowedOrigins.length === 0) {
    throw new Error('RELAY_ALLOWED_ORIGINS must name the production browser origins');
  }
  if (!dev && !['0', '1'].includes(process.env.RELAY_TRUST_FORWARDED_FOR)) {
    throw new Error('Set RELAY_TRUST_FORWARDED_FOR explicitly to 1 behind a trusted proxy or 0 for direct connections');
  }
  return {
    host,
    port,
    pollingEnabled,
    gatewayKey,
    maxPollingSessions: intEnv('RELAY_MAX_POLLING_SESSIONS', 16),
    app: process.env.RELAY_APP || 'dunecity',
    socketPath: process.env.RELAY_SOCKET_PATH || '/v1/socket',
    publicSocketUrl,
    observedTransport,
    allowedOrigins,
    trustForwardedFor: boolEnv('RELAY_TRUST_FORWARDED_FOR', false),
    requiredGameProtocol: intEnv('RELAY_GAME_PROTOCOL', 0),
    maxConnections: intEnv('RELAY_MAX_CONNECTIONS', undefined),
    maxRooms: intEnv('RELAY_MAX_ROOMS', undefined),
  };
}

async function main(argv) {
  const raw = configFromEnv(argv);
  const config = Object.fromEntries(Object.entries(raw).filter(([, v]) => v !== undefined));
  config.log = new LifecycleLog({});

  // Optional and off by default. A present but wrong configuration throws here, before the
  // socket is bound, and the message names the variable without quoting its value.
  const analytics = createLifecycleSink({
    env: process.env,
    observedTransport: config.observedTransport,
    log: config.log,
  });
  config.lifecycle = analytics.sink;
  config.log.emit('analytics_status', { state: analytics.state });

  const relay = createRelay(config);
  await relay.start();

  const shutdown = () => {
    relay.stop().then(() => process.exit(0), () => process.exit(1));
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

if (require.main === module) {
  main(process.argv.slice(2)).catch((err) => {
    process.stderr.write(`room-relay: ${err.message}\n`);
    process.exit(1);
  });
}

module.exports = { main, configFromEnv };

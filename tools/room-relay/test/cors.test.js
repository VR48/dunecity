'use strict';

const assert = require('node:assert/strict');
const { describe, it } = require('node:test');

const { createRelay } = require('../src/server');
const { isCanonicalOrigin, assertAllowedOrigins } = require('../src/admission');
const { configFromEnv } = require('../src/index');
const { startRelay, admitHost, admitJoin, parseKeyValue } = require('./helpers');

// A browser can send an admission POST to another origin without any of this - it is a simple
// request - but it cannot *read* the response, which is where the grant is. Without
// Access-Control-Allow-Origin the game gets an opaque failure for every outcome, success and
// error alike. What is echoed is only ever an exact allowlisted origin.

const PAGE = 'https://dunecity.example';
const OTHER = 'https://evil.example';

function corsOf(headers) {
  return {
    allowOrigin: headers.get('access-control-allow-origin'),
    vary: headers.get('vary'),
    credentials: headers.get('access-control-allow-credentials'),
  };
}

async function startBrowserRelay(extra = {}) {
  return startRelay({ allowedOrigins: [PAGE], ...extra });
}

describe('CORS for allowlisted browser origins', () => {
  it('lets an allowlisted page read a host admission', async () => {
    const relay = await startBrowserRelay();
    try {
      const res = await fetch(`${relay.baseUrl}/v1/admission/host`, {
        method: 'POST',
        headers: {
          'content-type': 'application/x-www-form-urlencoded',
          origin: PAGE,
        },
        body: 'app=dunecity&appVersion=1.0.655&gameProtocol=5&runtime=browser&maxPeers=2&mode=coop',
      });
      const cors = corsOf(res.headers);
      assert.equal(res.status, 200);
      assert.equal(cors.allowOrigin, PAGE, 'the exact origin, never a wildcard');
      assert.equal(cors.vary, 'Origin');
      assert.equal(cors.credentials, null, 'a grant is never handed out on an ambient cookie');
      const fields = parseKeyValue(await res.text());
      assert.equal(fields.status, 'ok');
      assert.match(fields.grant, /^[0-9a-f]{64}$/);
    } finally {
      await relay.stop();
    }
  });

  it('lets an allowlisted page read a join and its errors', async () => {
    const relay = await startBrowserRelay();
    try {
      const host = await admitHost(relay);
      const ok = await admitJoin(relay, host.fields.room, { __headers: { origin: PAGE } });
      assert.equal(ok.status, 200);
      assert.equal(ok.headers['access-control-allow-origin'], PAGE);

      // The interesting half: a browser has to be able to read *why* it was refused.
      const missing = await admitJoin(relay, 'ZZZZ-ZZZZ-ZZZZ', { __headers: { origin: PAGE } });
      assert.equal(missing.status, 404);
      assert.equal(missing.fields.code, 'room_not_found');
      assert.equal(missing.headers['access-control-allow-origin'], PAGE);
      assert.equal(missing.headers.vary, 'Origin');
      assert.equal(missing.headers['access-control-allow-credentials'], undefined);
    } finally {
      await relay.stop();
    }
  });

  it('never reflects a foreign or null origin', async () => {
    const relay = await startBrowserRelay();
    try {
      for (const origin of [OTHER, 'null', 'http://dunecity.example', `${PAGE}.evil.example`]) {
        const res = await admitHost(relay, { __headers: { origin } });
        assert.equal(res.status, 403, `${origin} must be refused`);
        assert.equal(res.fields.code, 'forbidden_origin');
        assert.equal(res.headers['access-control-allow-origin'], undefined,
          `${origin} must not be reflected`);
        assert.equal(res.headers.vary, 'Origin');
      }
    } finally {
      await relay.stop();
    }
  });

  it('serves a native client with no Origin and no allow-origin header', async () => {
    const relay = await startBrowserRelay();
    try {
      const res = await admitHost(relay);
      assert.equal(res.status, 200);
      assert.equal(res.headers['access-control-allow-origin'], undefined);
      assert.equal(res.headers.vary, 'Origin', 'a cache must not reuse this for a browser');
    } finally {
      await relay.stop();
    }
  });

  it('answers a preflight for an allowlisted origin and nothing more', async () => {
    const relay = await startBrowserRelay();
    try {
      const res = await fetch(`${relay.baseUrl}/v1/admission/join`, {
        method: 'OPTIONS',
        headers: {
          origin: PAGE,
          'access-control-request-method': 'POST',
          'access-control-request-headers': 'content-type',
        },
      });
      assert.equal(res.status, 204);
      assert.equal(res.headers.get('access-control-allow-origin'), PAGE);
      assert.equal(res.headers.get('access-control-allow-methods'), 'POST');
      assert.equal(res.headers.get('access-control-allow-headers'), 'content-type');
      assert.equal(res.headers.get('access-control-max-age'), '600');
      assert.equal(res.headers.get('vary'), 'Origin');
      assert.equal(res.headers.get('access-control-allow-credentials'), null);
      assert.equal(await res.text(), '');
    } finally {
      await relay.stop();
    }
  });

  it('refuses a preflight from a foreign origin, for another method, or with none', async () => {
    const relay = await startBrowserRelay();
    try {
      const foreign = await fetch(`${relay.baseUrl}/v1/admission/join`, {
        method: 'OPTIONS',
        headers: { origin: OTHER, 'access-control-request-method': 'POST' },
      });
      assert.equal(foreign.status, 403);
      assert.equal(foreign.headers.get('access-control-allow-origin'), null);

      const wrongMethod = await fetch(`${relay.baseUrl}/v1/admission/join`, {
        method: 'OPTIONS',
        headers: { origin: PAGE, 'access-control-request-method': 'DELETE' },
      });
      assert.equal(wrongMethod.status, 403);

      const noOrigin = await fetch(`${relay.baseUrl}/v1/admission/join`, { method: 'OPTIONS' });
      assert.equal(noOrigin.status, 403);
      assert.equal(noOrigin.headers.get('access-control-allow-origin'), null);
    } finally {
      await relay.stop();
    }
  });

  it('does not turn OPTIONS into a second way in', async () => {
    const relay = await startBrowserRelay();
    try {
      const res = await fetch(`${relay.baseUrl}/v1/health`, {
        method: 'OPTIONS',
        headers: { origin: PAGE, 'access-control-request-method': 'POST' },
      });
      assert.equal(res.status, 404, 'only the admission endpoints answer a preflight');
      assert.equal(relay.store.roomCount, 0);
    } finally {
      await relay.stop();
    }
  });

  it('keeps the ingress bounds in place for a browser request', async () => {
    const relay = await startBrowserRelay({ maxHttpSockets: 4, httpBodyTimeoutMs: 150 });
    try {
      const oversize = await fetch(`${relay.baseUrl}/v1/admission/host`, {
        method: 'POST',
        headers: { 'content-type': 'application/x-www-form-urlencoded', origin: PAGE },
        body: `app=dunecity&pad=${'x'.repeat(5000)}`,
      });
      assert.equal(oversize.status, 413);
      assert.equal(parseKeyValue(await oversize.text()).code, 'bad_request');
      // Still bounded, and still readable by the page that asked.
      assert.equal(oversize.headers.get('access-control-allow-origin'), PAGE);
    } finally {
      await relay.stop();
    }
  });
});

describe('allowed origin configuration', () => {
  it('accepts only canonical http(s) origins', () => {
    for (const good of ['https://dunecity.example', 'http://127.0.0.1:8080',
      'https://play.dunecity.example:8443']) {
      assert.equal(isCanonicalOrigin(good), true, good);
    }
    for (const bad of [
      'null',                              // the literal null origin
      '*',
      'https://dunecity.example/',         // trailing slash never appears in a real header
      'https://dunecity.example/lobby',    // path
      'https://dunecity.example?a=1',      // query
      'https://dunecity.example#f',        // fragment
      'https://user:pw@dunecity.example',  // credentials
      'https://dunecity.example:443',      // default port is not sent by a browser
      'http://dunecity.example:80',
      'ws://dunecity.example',
      'file://',
      'dunecity.example',
      '',
      `https://${'a'.repeat(300)}.example`,
    ]) {
      assert.equal(isCanonicalOrigin(bad), false, bad);
    }
  });

  it('fails startup on a bad allowlist rather than ignoring it', () => {
    assert.throws(() => assertAllowedOrigins(['null']), /'null' is not an acceptable Origin/);
    assert.throws(() => assertAllowedOrigins(['https://dunecity.example/']),
      /not a canonical http\(s\) origin/);
    assert.throws(() => createRelay({ allowedOrigins: ['https://a.example', '*'] }),
      /not a canonical http\(s\) origin/);
    assert.deepEqual(assertAllowedOrigins(['https://a.example', 'http://127.0.0.1:8787']),
      ['https://a.example', 'http://127.0.0.1:8787']);
  });

  it('validates RELAY_ALLOWED_ORIGINS at startup', () => {
    const saved = process.env.RELAY_ALLOWED_ORIGINS;
    try {
      process.env.RELAY_ALLOWED_ORIGINS = ' https://a.example , https://b.example ';
      assert.deepEqual(configFromEnv(['--dev']).allowedOrigins,
        ['https://a.example', 'https://b.example']);
      process.env.RELAY_ALLOWED_ORIGINS = 'https://a.example,null';
      assert.throws(() => configFromEnv(['--dev']), /'null' is not an acceptable Origin/);
      process.env.RELAY_ALLOWED_ORIGINS = 'https://a.example/';
      assert.throws(() => configFromEnv(['--dev']), /not a canonical http\(s\) origin/);
    } finally {
      if (saved === undefined) delete process.env.RELAY_ALLOWED_ORIGINS;
      else process.env.RELAY_ALLOWED_ORIGINS = saved;
    }
  });

  it('requires explicit browser origins and proxy trust in production', () => {
    const names = ['RELAY_PUBLIC_URL', 'RELAY_ALLOWED_ORIGINS', 'RELAY_TRUST_FORWARDED_FOR'];
    const saved = Object.fromEntries(names.map((name) => [name, process.env[name]]));
    try {
      process.env.RELAY_PUBLIC_URL = 'wss://dunelegacy.com/relay/v1/socket';
      delete process.env.RELAY_ALLOWED_ORIGINS;
      delete process.env.RELAY_TRUST_FORWARDED_FOR;
      assert.throws(() => configFromEnv([]), /RELAY_ALLOWED_ORIGINS/);
      process.env.RELAY_ALLOWED_ORIGINS = 'https://dunelegacy.com';
      assert.throws(() => configFromEnv([]), /RELAY_TRUST_FORWARDED_FOR/);
      process.env.RELAY_TRUST_FORWARDED_FOR = 'true';
      assert.throws(() => configFromEnv([]), /RELAY_TRUST_FORWARDED_FOR/);
      process.env.RELAY_TRUST_FORWARDED_FOR = '0';
      assert.equal(configFromEnv([]).trustForwardedFor, false);
      process.env.RELAY_TRUST_FORWARDED_FOR = '1';
      assert.equal(configFromEnv([]).trustForwardedFor, true);
    } finally {
      for (const name of names) {
        if (saved[name] === undefined) delete process.env[name];
        else process.env[name] = saved[name];
      }
    }
  });
});

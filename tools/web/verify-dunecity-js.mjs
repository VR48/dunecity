#!/usr/bin/env node
/**
 * Post-build verification for dunecity.js.
 * Fails if the emitted runtime requires a pthread worker; the browser build
 * must stay single-threaded (ASYNCIFY, no USE_PTHREADS).
 *
 * Usage:
 *   node tools/web/verify-dunecity-js.mjs --built path/to/dunecity.js
 */
import fs from 'node:fs';
import path from 'node:path';

const args = process.argv.slice(2);
if (args.length !== 2 || args[0] !== '--built') {
  console.error('usage: verify-dunecity-js.mjs --built <path>');
  process.exit(2);
}

const filePath = path.resolve(args[1]);
const text = fs.readFileSync(filePath, 'utf8');

function fail(msg) {
  console.error(`ERROR: ${msg}`);
  process.exit(1);
}

if (/dunecity\.worker\.js|ENVIRONMENT_IS_PTHREAD=true|USE_PTHREADS/.test(text)) {
  fail('dunecity.js appears to require pthread worker (expected single-threaded build)');
}

console.log(`OK: built dunecity.js checks passed for ${filePath}`);

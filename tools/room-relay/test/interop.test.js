'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');

const protocol = require('../src/protocol');
const { S2C, C2S, GAME, ROLE, PHASE, DIAGNOSTIC_KIND } = require('../src/constants');

// Byte-for-byte fixtures shared with the C++ client.
//
// The same hex strings appear in tests/wasm/RelayWireHarness.cpp. Two implementations agreeing
// with a prose specification is not the same as agreeing with each other: a field written in the
// wrong order, or a length prefix of the wrong width, reads perfectly well to whichever side
// wrote it. These fixtures are the actual contract, and both sides are checked against them.
//
// Derived by hand from docs/room-relay-protocol.md §4. If one of these has to change, the
// document, this file and the C++ harness all change together, and the protocol version changes
// with them.

const FIXTURES = {
  hello:
    '01'                                  // HELLO
    + '0001'                              // relay protocol version 1
    + '0005'                              // game protocol version 5
    + '10' + '30313233343536373839616263646566'   // grant "0123456789abcdef"
    + '06' + '6e6174697665'                       // runtime "native"
    + '07' + '312e302e363535'                     // appVersion "1.0.655"
    + '08' + '6465616462656566'                   // contentHash "deadbeef"
    + '06' + '73746566616e',                      // displayName "stefan"

  welcome:
    '81'                                  // WELCOME
    + '0001'                              // relay protocol version
    + '0005'                              // game protocol version
    + '00000007'                          // peer id 7
    + '01'                                // role: host
    + '0e' + '483450512d3754324d2d39584b42' // room code "H4PQ-7T2M-9XKB"
    + '02'                                // max peers
    + '01'                                // phase: lobby
    + '0003fff0'                          // max payload 262128
    + '1388'                              // heartbeat interval 5000 ms
    + '4e20',                             // liveness timeout 20000 ms

  peerJoined:
    '82'                                  // PEER_JOINED
    + '0000000b'                          // peer id 11
    + '02'                                // role: client
    + '05' + '6775657374'                 // "guest"
    + '07' + '62726f77736572',            // "browser"

  serverRelay:
    '85'                                  // RELAY
    + '00000003'                          // sender peer id 3
    + '01'                                // channel 1
    + '01'                                // flags: reliable
    + '0009'                              // declared game message type: COMMANDLIST
    + '00000006'                          // payload length
    + '09000000aabb',                     // payload: little-endian 9, then two bytes

  clientRelay:
    '02'                                  // RELAY
    + '00000000'                          // recipient: everybody
    + '01'                                // channel 1
    + '01'                                // flags: reliable
    + '0009'                              // declared game message type
    + '00000006'                          // payload length
    + '09000000aabb',

  clientDiagnostic:
    '06'                                  // DIAGNOSTIC
    + '01'                                // kind: state digest
    + '00000004'                          // payload length
    + '01020304',
};

function bytes(hex) {
  return Buffer.from(hex, 'hex');
}

test('the handshake this relay accepts is the documented byte sequence', () => {
  const frame = bytes(FIXTURES.hello);
  const msg = protocol.decodeClientMessage(frame);

  assert.equal(msg.type, C2S.HELLO);
  assert.equal(msg.relayVersion, 1);
  assert.equal(msg.gameProtocol, 5);
  assert.equal(msg.grant, '0123456789abcdef');
  assert.equal(msg.runtime, 'native');
  assert.equal(msg.appVersion, '1.0.655');
  assert.equal(msg.contentHash, 'deadbeef');
  assert.equal(msg.displayName, 'stefan');

  // And the encoder produces exactly those bytes again.
  const encoded = protocol.encodeHello({
    grant: '0123456789abcdef',
    gameProtocol: 5,
    runtime: 'native',
    appVersion: '1.0.655',
    contentHash: 'deadbeef',
    displayName: 'stefan',
  });
  assert.equal(encoded.toString('hex'), FIXTURES.hello);
});

test('WELCOME is the documented byte sequence', () => {
  const encoded = Buffer.from(protocol.encodeWelcome({
    gameProtocol: 5,
    peerId: 7,
    role: ROLE.HOST,
    roomCode: 'H4PQ-7T2M-9XKB',
    maxPeers: 2,
    phase: PHASE.LOBBY,
  }));
  assert.equal(encoded.toString('hex'), FIXTURES.welcome);

  const msg = protocol.decodeServerMessage(bytes(FIXTURES.welcome));
  assert.equal(msg.type, S2C.WELCOME);
  assert.equal(msg.peerId, 7);
  assert.equal(msg.roomCode, 'H4PQ-7T2M-9XKB');
  assert.equal(msg.maxPayload, 262128);
  assert.equal(msg.heartbeatIntervalMs, 5000);
  assert.equal(msg.livenessTimeoutMs, 20000);
});

test('PEER_JOINED is the documented byte sequence', () => {
  const encoded = Buffer.from(protocol.encodePeerJoined({
    peerId: 11, role: ROLE.CLIENT, displayName: 'guest', runtime: 'browser',
  }));
  assert.equal(encoded.toString('hex'), FIXTURES.peerJoined);
});

test('a routed game payload is the documented byte sequence', () => {
  const payload = Buffer.from([0x09, 0x00, 0x00, 0x00, 0xaa, 0xbb]);

  const clientFrame = protocol.encodeClientRelay({
    recipient: 0, channel: 1, flags: 1, gameMessageType: GAME.COMMANDLIST, payload,
  });
  assert.equal(clientFrame.toString('hex'), FIXTURES.clientRelay);

  const decoded = protocol.decodeClientMessage(bytes(FIXTURES.clientRelay));
  const serverFrame = Buffer.from(protocol.encodeRelay(3, decoded));
  assert.equal(serverFrame.toString('hex'), FIXTURES.serverRelay);

  // The game payload itself is untouched by the relay, byte for byte.
  const routed = protocol.decodeServerMessage(bytes(FIXTURES.serverRelay));
  assert.deepEqual(routed.payload, payload);
});

test('a diagnostic is the documented byte sequence', () => {
  const encoded = protocol.encodeClientDiagnostic(DIAGNOSTIC_KIND.STATE_DIGEST,
    Buffer.from([1, 2, 3, 4]));
  assert.equal(encoded.toString('hex'), FIXTURES.clientDiagnostic);

  const msg = protocol.decodeClientMessage(bytes(FIXTURES.clientDiagnostic));
  assert.equal(msg.kind, DIAGNOSTIC_KIND.STATE_DIGEST);
  assert.deepEqual(msg.payload, Buffer.from([1, 2, 3, 4]));
});

test('the fixtures use the field widths the document specifies', () => {
  // A guard against the most likely silent drift: a length prefix changing width. The handshake
  // uses uint8 prefixes; the payload and diagnostic lengths are uint32; the envelope integers
  // are big-endian while the game payload keeps its own little-endian header.
  const hello = bytes(FIXTURES.hello);
  assert.equal(hello[3], 0x00);
  assert.equal(hello[4], 0x05, 'game protocol version is big-endian uint16');
  assert.equal(hello[5], 16, 'grant length is a uint8 prefix');

  const relay = bytes(FIXTURES.clientRelay);
  assert.equal(relay.readUInt16BE(7), GAME.COMMANDLIST, 'declared type is big-endian uint16');
  assert.equal(relay.readUInt32BE(9), 6, 'payload length is big-endian uint32');
  assert.equal(relay.readUInt32LE(13), GAME.COMMANDLIST,
    'the game payload keeps its little-endian header');
});

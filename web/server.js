#!/usr/bin/env node
// Serves the web build and relays network games between browsers.
//
//   node web/server.js [--port 8080] [--host 127.0.0.1] [--build build-web]
//
// A page cannot listen for connections, so a game's host and the players who
// join it all connect out to this server, which pairs them. It needs nothing
// beyond Node. Static files are limited to the page, the build and the data
// the game reads (assets/ and the CD's fonts).
//
// The relay, at /relay:
//   ?host=PORT   opens the room PORT (0: any free number). The server answers
//                with the text "room N" and, for each player joining, "peer ID".
//   ?join=PORT   joins the room PORT; refused when there is none.
//   ?peer=ID     the host's own connection for player ID, as it answers "peer
//                ID". What the player sends comes out of it, and what it sends
//                goes to the player; both carry the game's bytes unchanged.
//   ?dgram=PORT  a datagram socket bound to PORT (0: any free number), for
//                finding games: it is answered "room N" with the number it
//                got. A message to it is two bytes of destination port and the
//                datagram; what arrives is two bytes of source port and the
//                datagram. A message to a port nobody holds is dropped.
'use strict';

const crypto = require('crypto');
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const MAX_MESSAGE = 70 * 1024 * 1024; // a restored game's welcome carries the saved game
const MAX_ROOMS = 64;
const MAX_JOINERS = 8;
const MAX_DGRAMS = 256;
const MAX_DGRAM = 64 * 1024;
const MAX_QUEUED = 64 * 1024 * 1024; // what a player sends before the host's end is ready

function options() {
  const found = { port: 8080, host: '127.0.0.1', build: path.join(ROOT, 'build-web') };
  const args = process.argv.slice(2);
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--port') found.port = parseInt(args[++i], 10);
    else if (args[i] === '--host') found.host = args[++i];
    else if (args[i] === '--build') found.build = path.resolve(args[++i]);
    else {
      console.error('unknown option ' + args[i]);
      process.exit(2);
    }
  }
  return found;
}
const opts = options();

// --- Static files ---------------------------------------------------------

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.json': 'application/json',
};
const DATA_TREES = ['/assets/', '/original_game_1999/SYSTEM/FONTS/'];

// The file a request path names, or null for any path not served.
function resolve(urlPath) {
  let p;
  try {
    p = decodeURIComponent(urlPath.split('?')[0]);
  } catch (e) {
    return null;
  }
  if (p === '/' || p === '/index.html') return path.join(ROOT, 'web', 'index.html');
  if (p === '/data.json') return path.join(ROOT, 'web', 'data.json');
  if (p === '/OpenAoK.js' || p === '/OpenAoK.wasm') return path.join(opts.build, p.slice(1));
  for (const tree of DATA_TREES) {
    if (!p.startsWith(tree)) continue;
    const base = path.join(ROOT, tree);
    const full = path.join(ROOT, p);
    if (full.startsWith(base)) return full;
  }
  return null;
}

function serve(req, res) {
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    res.writeHead(405).end();
    return;
  }
  const file = resolve(req.url);
  if (!file) {
    res.writeHead(404).end('not found');
    return;
  }
  fs.stat(file, (error, info) => {
    if (error || !info.isFile()) {
      res.writeHead(404).end('not found');
      return;
    }
    res.writeHead(200, {
      'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream',
      'Content-Length': info.size,
      'Cache-Control': 'no-cache',
    });
    if (req.method === 'HEAD') res.end();
    else fs.createReadStream(file).pipe(res);
  });
}

// --- WebSocket (RFC 6455), the parts a relay needs -----------------------

const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

// One frame, as a server sends it: unmasked.
function frame(opcode, payload) {
  const n = payload.length;
  let head;
  if (n < 126) {
    head = Buffer.from([0x80 | opcode, n]);
  } else if (n < 65536) {
    head = Buffer.alloc(4);
    head[0] = 0x80 | opcode;
    head[1] = 126;
    head.writeUInt16BE(n, 2);
  } else {
    head = Buffer.alloc(10);
    head[0] = 0x80 | opcode;
    head[1] = 127;
    head.writeBigUInt64BE(BigInt(n), 2);
  }
  return Buffer.concat([head, payload]);
}

class Connection {
  constructor(socket) {
    this.socket = socket;
    this.buffer = Buffer.alloc(0);
    this.parts = [];
    this.partsText = false;
    this.closed = false;
    this.onmessage = () => {};
    this.onclose = () => {};
    socket.setNoDelay(true);
    socket.on('data', (data) => this.read(data));
    socket.on('close', () => this.end());
    socket.on('error', () => socket.destroy());
  }

  send(data, text) {
    if (this.closed) return;
    this.socket.write(frame(text ? 1 : 2, text ? Buffer.from(data) : data));
  }

  close() {
    if (this.closed) return;
    this.socket.write(frame(8, Buffer.alloc(0)));
    this.socket.end();
    this.end();
  }

  end() {
    if (this.closed) return;
    this.closed = true;
    this.onclose();
  }

  read(data) {
    this.buffer = Buffer.concat([this.buffer, data]);
    for (;;) {
      const b = this.buffer;
      if (b.length < 2) return;
      const fin = (b[0] & 0x80) !== 0;
      const opcode = b[0] & 0x0f;
      const masked = (b[1] & 0x80) !== 0;
      let length = b[1] & 0x7f;
      let at = 2;
      if (length === 126) {
        if (b.length < 4) return;
        length = b.readUInt16BE(2);
        at = 4;
      } else if (length === 127) {
        if (b.length < 10) return;
        const big = b.readBigUInt64BE(2);
        if (big > BigInt(MAX_MESSAGE)) return this.socket.destroy();
        length = Number(big);
        at = 10;
      }
      if (length > MAX_MESSAGE) return this.socket.destroy();
      const end = at + (masked ? 4 : 0) + length;
      if (b.length < end) return;
      let payload = b.subarray(at + (masked ? 4 : 0), end);
      if (masked) {
        const mask = b.subarray(at, at + 4);
        payload = Buffer.from(payload);
        for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
      }
      this.buffer = b.subarray(end);
      this.frame(fin, opcode, payload);
      if (this.closed) return;
    }
  }

  frame(fin, opcode, payload) {
    if (opcode === 8) return this.close();
    if (opcode === 9) {
      this.socket.write(frame(10, payload));
      return;
    }
    if (opcode === 10) return;
    if (opcode === 1 || opcode === 2) {
      this.parts = [];
      this.partsText = opcode === 1;
    } else if (opcode !== 0) {
      return this.socket.destroy();
    }
    this.parts.push(payload);
    if (fin) {
      const whole = this.parts.length === 1 ? this.parts[0] : Buffer.concat(this.parts);
      this.parts = [];
      this.onmessage(whole, this.partsText);
    }
  }
}

// Completes the handshake; null (and the socket dropped) when it is not one.
function upgrade(req, socket) {
  const key = req.headers['sec-websocket-key'];
  if (!key || (req.headers.upgrade || '').toLowerCase() !== 'websocket') {
    socket.destroy();
    return null;
  }
  const accept = crypto.createHash('sha1').update(key + GUID).digest('base64');
  socket.write(
    'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
      'Sec-WebSocket-Accept: ' + accept + '\r\n\r\n');
  return new Connection(socket);
}

function refuse(socket, status) {
  socket.end('HTTP/1.1 ' + status + '\r\nConnection: close\r\nContent-Length: 0\r\n\r\n');
}

// --- The relay -----------------------------------------------------------

const rooms = new Map();   // room number -> { host, joiners: Map(id -> joiner) }
const joiners = new Map(); // id -> joiner, to find the one a host's "peer" connection serves
let lastId = 0;

function freeRoom() {
  for (let tries = 0; tries < 1000; tries++) {
    const number = 20000 + Math.floor(Math.random() * 40000);
    if (!rooms.has(number)) return number;
  }
  return 0;
}

function host(req, socket, want) {
  const number = want === 0 ? freeRoom() : want;
  if (!Number.isInteger(number) || number < 1 || number > 65535 || rooms.has(number) ||
      rooms.size >= MAX_ROOMS) {
    return refuse(socket, '409 Conflict');
  }
  const connection = upgrade(req, socket);
  if (!connection) return;
  const room = { host: connection, joiners: new Map() };
  rooms.set(number, room);
  connection.onclose = () => {
    rooms.delete(number);
    for (const joiner of room.joiners.values()) joiner.connection.close();
  };
  connection.send('room ' + number, true);
  console.log('room ' + number + ' opened');
}

function join(req, socket, number) {
  const room = rooms.get(number);
  if (!room || room.joiners.size >= MAX_JOINERS) return refuse(socket, '404 Not Found');
  const connection = upgrade(req, socket);
  if (!connection) return;
  const id = ++lastId;
  const joiner = { id, connection, pipe: null, queue: [], queued: 0, room };
  room.joiners.set(id, joiner);
  joiners.set(id, joiner);
  connection.onmessage = (data) => {
    if (joiner.pipe) {
      joiner.pipe.send(data, false);
      return;
    }
    joiner.queued += data.length;
    if (joiner.queued > MAX_QUEUED) return connection.close();
    joiner.queue.push(data);
  };
  connection.onclose = () => {
    room.joiners.delete(id);
    joiners.delete(id);
    if (joiner.pipe) joiner.pipe.close();
  };
  room.host.send('peer ' + id, true);
}

function pipe(req, socket, id) {
  const joiner = joiners.get(id);
  if (!joiner || joiner.pipe) return refuse(socket, '404 Not Found');
  const connection = upgrade(req, socket);
  if (!connection) return;
  joiner.pipe = connection;
  connection.onmessage = (data) => joiner.connection.send(data, false);
  connection.onclose = () => joiner.connection.close();
  for (const data of joiner.queue) connection.send(data, false);
  joiner.queue = [];
  joiner.queued = 0;
}

const dgrams = new Map();  // port -> connection

function freeDgram() {
  for (let tries = 0; tries < 1000; tries++) {
    const number = 50000 + Math.floor(Math.random() * 15000);
    if (!dgrams.has(number)) return number;
  }
  return 0;
}

function datagram(req, socket, want) {
  const number = want === 0 ? freeDgram() : want;
  if (!Number.isInteger(number) || number < 1 || number > 65535 || dgrams.has(number) ||
      dgrams.size >= MAX_DGRAMS) {
    return refuse(socket, '409 Conflict');
  }
  const connection = upgrade(req, socket);
  if (!connection) return;
  dgrams.set(number, connection);
  connection.onmessage = (data) => {
    if (data.length < 2 || data.length > MAX_DGRAM) return;
    const target = dgrams.get(data.readUInt16LE(0));
    if (!target) return;
    const from = Buffer.alloc(2);
    from.writeUInt16LE(number, 0);
    target.send(Buffer.concat([from, data.subarray(2)]), false);
  };
  connection.onclose = () => {
    if (dgrams.get(number) === connection) dgrams.delete(number);
  };
  connection.send('room ' + number, true);
}

function relay(req, socket) {
  const url = new URL(req.url, 'http://relay');
  if (url.pathname !== '/relay') return refuse(socket, '404 Not Found');
  const q = url.searchParams;
  if (q.has('host')) return host(req, socket, parseInt(q.get('host'), 10));
  if (q.has('join')) return join(req, socket, parseInt(q.get('join'), 10));
  if (q.has('peer')) return pipe(req, socket, parseInt(q.get('peer'), 10));
  if (q.has('dgram')) return datagram(req, socket, parseInt(q.get('dgram'), 10));
  return refuse(socket, '400 Bad Request');
}

// --- Start ---------------------------------------------------------------

const server = http.createServer(serve);
server.on('upgrade', relay);
server.listen(opts.port, opts.host, () => {
  console.log('OpenAoK web: http://' + (opts.host === '0.0.0.0' ? 'localhost' : opts.host) + ':' + opts.port + '/');
  if (opts.host !== '127.0.0.1' && opts.host !== 'localhost' && opts.host !== '::1') {
    for (const list of Object.values(os.networkInterfaces())) {
      for (const a of list || []) {
        if (a.family === 'IPv4' && !a.internal) console.log('  on the network: http://' + a.address + ':' + opts.port + '/');
      }
    }
    console.log('Warning: this serves your game data to everyone who can reach it.');
    console.log('Share it only with people who own the game.');
  }
});

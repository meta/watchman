/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

'use strict';

const assert = require('node:assert/strict');
const {EventEmitter} = require('node:events');
const {test} = require('node:test');
const childProcess = require('child_process');
const net = require('net');
const bser = require('bser');
const {Client} = require('../index');

function setup(t) {
  const previousSocket = process.env.WATCHMAN_SOCK;
  delete process.env.WATCHMAN_SOCK;
  t.after(() => {
    if (previousSocket == null) {
      delete process.env.WATCHMAN_SOCK;
    } else {
      process.env.WATCHMAN_SOCK = previousSocket;
    }
  });
  const children = [];
  const sockets = [];
  t.mock.method(childProcess, 'spawn', () => {
    const child = new EventEmitter();
    child.stdout = new EventEmitter();
    child.stderr = new EventEmitter();
    children.push(child);
    return child;
  });
  t.mock.method(net, 'createConnection', () => {
    const socket = new EventEmitter();
    socket.writes = [];
    socket.endCalls = 0;
    socket.write = data => socket.writes.push(data);
    socket.end = () => {
      socket.endCalls++;
      socket.emit('end');
    };
    sockets.push(socket);
    return socket;
  });
  return {client: new Client(), children, sockets};
}

function finishDiscovery(child) {
  child.stdout.emit(
    'data',
    JSON.stringify({sockname: '/tmp/watchman-test.sock'}),
  );
  child.emit('close', 0, null);
}

test('end prevents pending discovery from opening a socket', t => {
  const {client, children, sockets} = setup(t);
  const errors = [];
  client.command(['version'], error => errors.push(error));
  client.end();
  assert.equal(errors.length, 1);
  assert.match(errors[0].message, /client was ended/);
  assert.doesNotThrow(() => finishDiscovery(children[0]));
  assert.equal(sockets.length, 0);
});

test('end ignores a late discovery failure', t => {
  const {client, children} = setup(t);
  client.command(['version'], () => {});
  client.end();
  assert.doesNotThrow(() =>
    children[0].emit('error', new Error('late failure')),
  );
  assert.doesNotThrow(() => children[0].emit('close', 1, null));
});

test('a new command can reconnect after pending discovery is cancelled', async t => {
  const {client, children, sockets} = setup(t);
  client.command(['old'], () => {});
  client.end();
  const responsePromise = new Promise(resolve => {
    client.command(['new'], (error, response) => resolve({error, response}));
  });
  assert.equal(children.length, 2);
  finishDiscovery(children[0]);
  assert.equal(sockets.length, 0);
  finishDiscovery(children[1]);
  assert.equal(sockets.length, 1);
  sockets[0].emit('connect');
  assert.deepEqual(bser.loadFromBuffer(sockets[0].writes[0]), ['new']);
  sockets[0].emit('data', bser.dumpToBuffer({ok: true}));
  assert.deepEqual(await responsePromise, {error: null, response: {ok: true}});
  client.end();
});

test('current connection errors still reach the client', t => {
  const {client, children, sockets} = setup(t);
  const errors = [];
  client.on('error', error => errors.push(error));
  client.command(['version'], () => {});
  finishDiscovery(children[0]);
  const error = new Error('connection failed');
  sockets[0].emit('error', error);
  assert.deepEqual(errors, [error]);
  client.end();
});

test('old socket events cannot affect a replacement connection', t => {
  const {client, children, sockets} = setup(t);
  client.command(['old'], () => {});
  finishDiscovery(children[0]);
  const oldSocket = sockets[0];
  oldSocket.emit('connect');
  client.end();

  client.command(['new'], () => {});
  finishDiscovery(children[1]);
  const newSocket = sockets[1];
  newSocket.emit('connect');
  assert.doesNotThrow(() => oldSocket.emit('error', new Error('late failure')));
  oldSocket.emit('end');
  assert.equal(client.socket, newSocket);
  client.end();
});

test('a queued old response cannot complete a new command', async t => {
  const {client, children, sockets} = setup(t);
  client.command(['old'], () => {});
  finishDiscovery(children[0]);
  sockets[0].emit('connect');
  sockets[0].emit('data', bser.dumpToBuffer({old: true}));
  client.end();

  const responses = [];
  const responsePromise = new Promise(resolve => {
    client.command(['new'], (error, response) => {
      responses.push({error, response});
      resolve();
    });
  });
  finishDiscovery(children[1]);
  sockets[1].emit('connect');
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(responses, []);
  sockets[1].emit('data', bser.dumpToBuffer({new: true}));
  await responsePromise;
  assert.deepEqual(responses, [{error: null, response: {new: true}}]);
  client.end();
});

test('a command scheduled from cancellation survives the old connection ending', t => {
  const {client, children, sockets} = setup(t);
  client.command(['old'], () => client.command(['new'], () => {}));
  finishDiscovery(children[0]);
  sockets[0].emit('connect');
  client.end();
  assert.equal(children.length, 2);
  finishDiscovery(children[1]);
  assert.equal(client.socket, sockets[1]);
  assert.equal(sockets[1].endCalls, 0);
  client.end();
});

test('explicit close still emits end once', t => {
  const {client, children, sockets} = setup(t);
  let ends = 0;
  client.on('end', () => ends++);
  client.command(['version'], () => {});
  finishDiscovery(children[0]);
  sockets[0].emit('connect');
  client.end();
  sockets[0].emit('end');
  assert.equal(ends, 1);
});

test('reconnecting from connect waits for the new socket', t => {
  const {client, children, sockets} = setup(t);
  client.command(['old'], () => {});
  finishDiscovery(children[0]);
  client.once('connect', () => {
    client.end();
    client.command(['new'], () => {});
  });
  assert.doesNotThrow(() => sockets[0].emit('connect'));
  assert.ok(!client.currentCommand);
  finishDiscovery(children[1]);
  sockets[1].emit('connect');
  assert.deepEqual(bser.loadFromBuffer(sockets[1].writes[0]), ['new']);
  client.end();
});

test('reconnecting from a response waits for the new socket', async t => {
  const {client, children, sockets} = setup(t);
  const oldResponse = new Promise(resolve => {
    client.command(['old'], () => {
      client.end();
      client.command(['new'], () => {});
      resolve();
    });
  });
  finishDiscovery(children[0]);
  sockets[0].emit('connect');
  sockets[0].emit('data', bser.dumpToBuffer({old: true}));
  await oldResponse;
  assert.ok(!client.currentCommand);
  finishDiscovery(children[1]);
  sockets[1].emit('connect');
  assert.deepEqual(bser.loadFromBuffer(sockets[1].writes[0]), ['new']);
  client.end();
});

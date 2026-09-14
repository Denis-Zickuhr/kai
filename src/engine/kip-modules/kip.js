// kip - Kai Interface Protocol for Node (injected by Kai into KIP commands).
//
//   const values = await kip.prompt([{ name: 'env', type: 'select', options: ['dev', 'prod'] }],
//                                   { id: 'where', title: 'Where to?' });
//   kip.progress(50, 'Deploying...');
//   kip.done({ title: 'Deployed ' + values.env });
//
// `kip` is a global and also require('kip'). The code runs inside an async function, so
// top-level await works. `hello` is sent before the first message. Cancel exits the process
// with code 130; the Back button rejects with kip.Back. Full protocol: docs/kip.md.
'use strict';

const VERSION = 1;

class Back extends Error {
  constructor() {
    super('back');
    this.name = 'Back';
  }
}

let started = false;
let reader = null;
let closed = false;
const lines = [];
const waiters = [];

function write(msg) {
  process.stdout.write(JSON.stringify(msg) + '\n');
}

function send(type, fields = {}) {
  if (type !== 'hello' && !started) hello();
  if (type === 'hello') started = true;
  const msg = { kip: VERSION, type };
  for (const [key, value] of Object.entries(fields)) {
    if (value !== undefined && value !== null) msg[key] = value;
  }
  write(msg);
}

function hello(title, version) {
  send('hello', { title, version });
}

function log(...parts) {
  process.stderr.write(parts.join(' ') + '\n');
}

// One shared stdin reader. While nobody waits it is paused AND unref'd (pause alone still
// keeps the process alive), so the process exits after `done`; waiting re-refs it.
function idle() {
  reader.pause();
  if (process.stdin.unref) process.stdin.unref();
}

function read() {
  if (!reader) {
    reader = require('readline').createInterface({ input: process.stdin, crlfDelay: Infinity });
    reader.on('line', (line) => {
      const waiter = waiters.shift();
      if (waiter) waiter(line);
      else lines.push(line);
      if (!waiters.length) idle();
    });
    reader.on('close', () => {
      closed = true;
      while (waiters.length) waiters.shift()(null);
    });
  }
  return new Promise((resolve) => {
    if (lines.length) return resolve(lines.shift());
    if (closed) return resolve(null);
    waiters.push(resolve);
    if (process.stdin.ref) process.stdin.ref();
    reader.resume();
  });
}

async function nextMessage() {
  for (;;) {
    const line = await read();
    if (line === null) process.exit(130); // Kai closed our stdin: the run is over
    let msg;
    try {
      msg = JSON.parse(line);
    } catch {
      continue;
    }
    if (!msg || typeof msg !== 'object') continue;
    if (msg.type === 'cancel') process.exit(130);
    return msg;
  }
}

async function answer(id, { onChange, onChip, validate } = {}) {
  for (;;) {
    const msg = await nextMessage();
    if (msg.id !== id) continue;
    if (msg.type === 'response') {
      const values = msg.values || {};
      const errors = validate ? await validate(values) : null;
      if (errors && Object.keys(errors).length) {
        send('invalid', { id, errors });
        continue;
      }
      return values;
    }
    if (msg.type === 'back') throw new Back();
    if (msg.type === 'change') {
      const reply = onChange ? await onChange(msg.field, msg.values || {}) : null;
      const patch = Array.isArray(reply) || !reply ? { fields: reply || [] } : reply;
      send('patch', { id, seq: msg.seq, ...patch });
    } else if (msg.type === 'chip' && onChip) {
      let result;
      try {
        result = await onChip(msg.chip, msg.values || {});
        result = typeof result === 'string' ? { text: result } : { ...result };
        result.state = result.state || 'success';
      } catch (error) {
        result = { state: 'error', text: String(error && error.message || error) };
      }
      send('chip_result', { chip: msg.chip, id, ...result });
    }
  }
}

// Show a form and resolve with its values.
// options: id, title, description, submit_label, back, cancellable, remember, chips,
//          onChange(field, values) -> fields to patch, onChip(chip, values) -> text | {state, title, text},
//          validate(values) -> { field: message } to keep the form open.
async function prompt(fields, options = {}) {
  const { id = 'prompt', onChange, onChip, validate, ...rest } = options;
  send('prompt', { id, fields, ...rest });
  return answer(id, { onChange, onChip, validate });
}

// Yes/no screen; resolves true/false. options: id, title, danger, confirm_label, cancel_label, back.
async function confirm(text, options = {}) {
  const { id = 'confirm', ...rest } = options;
  send('confirm', { id, text, ...rest });
  return Boolean((await answer(id)).confirmed);
}

function message(text, level = 'info') {
  send('message', { level, text });
}

function markdown(text) {
  send('markdown', { text });
}

// value 0-100, or null for an indeterminate bar.
function progress(value, label, options = {}) {
  if (!started) hello();
  const msg = { kip: VERSION, type: 'progress', value };
  for (const [key, v] of Object.entries({ ...options, label })) {
    if (v !== undefined && v !== null) msg[key] = v;
  }
  write(msg);
}

function steps(id, items, title) {
  send('steps', { id, title, items });
}

// state: pending | running | success | error | skipped
function step(stepsId, id, state, detail) {
  send('step', { steps: stepsId, id, state, detail });
}

// columns: ['name', ...] or [{ key: 'name', label: 'Name' }, ...]
function table(columns, rows, options = {}) {
  const cols = columns.map((c) => (typeof c === 'string' ? { key: c, label: c } : c));
  send('table', { ...options, columns: cols, rows });
}

function notify(title, text, level) {
  send('notify', { title, text, level });
}

// Export a dynamic variable (the name must be in the command's Exportable variables).
function setEnv(name, value) {
  send('set_env', { name, value });
}

// Spontaneous patch of the open prompt `id` (no `seq`): replace fields (by name), remove fields and/or
// replace the whole chip set. For what the program decides on its own, e.g. a chip that repaints a
// table; answers to `change` are sent by `onChange`.
function patch(id, { fields = [], remove, chips } = {}) {
  send('patch', { id, fields, remove, chips });
}

function chipResult(chip, state = 'success', text, title, id) {
  send('chip_result', { chip, state, text, title, id });
}

// Result screen. actions: [{ type: 'open_url', label: 'Open', url: 'https://...' }]
function done(options = {}) {
  send('done', options);
}

const kip = {
  VERSION, Back, send, hello, log, prompt, confirm, message, markdown, progress,
  steps, step, table, notify, setEnv, patch, chipResult, done,
};

module.exports = kip;

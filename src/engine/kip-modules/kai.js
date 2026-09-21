// kai - talk to the running Kai app (injected by Kai into every Node command).
//
//   await kai.notify('Backup finished', { title: 'Backup', level: 'info' });
//   console.log(await kai.commands());       // names of the commands registered in Kai
//   await kai.env.use('Prod');               // switch the active environment
//   await kai.run('Deploy');                 // start another Kai command in the app
//
// `kai` is a global and also require('kai'); every call returns a promise. Kai passes its
// socket in KAI_IPC_SOCKET. When the app can't be reached directly (for example a script
// inside WSL under a Windows Kai) it falls back to the `kai` CLI (KAI_EXE, `kai` or
// `kai.exe` on PATH) for notify/show/run/env.use/kill. Failures reject with kai.Error.
'use strict';

const net = require('net');
const path = require('path');
const { execFile } = require('child_process');

const TIMEOUT_MS = 10000;

class KaiError extends Error {
  constructor(message) {
    super(message);
    this.name = 'KaiError';
  }
}

function endpoint() {
  return process.env.KAI_IPC_SOCKET || path.join(process.env.TMPDIR || '/tmp', 'kai-ipc-v1');
}

function roundtrip(line) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection(endpoint());
    let data = '';
    socket.setEncoding('utf8');
    socket.setTimeout(TIMEOUT_MS, () => socket.destroy(new Error('timeout')));
    socket.on('connect', () => socket.write(line));
    socket.on('data', (chunk) => {
      data += chunk;
      if (data.endsWith('\n')) socket.end();
    });
    socket.on('error', reject);
    socket.on('close', () => resolve(data));
  });
}

function findCli() {
  if (process.env.KAI_EXE) return process.env.KAI_EXE;
  const dirs = (process.env.PATH || '').split(path.delimiter);
  const fs = require('fs');
  for (const name of ['kai', 'kai.exe']) {
    for (const dir of dirs) {
      const candidate = path.join(dir, name);
      if (dir && fs.existsSync(candidate)) return candidate;
    }
  }
  return null;
}

function viaCli(cmd, fields) {
  const exe = findCli();
  let args = null;
  if (exe) {
    if (cmd === 'raise') {
      args = ['raise', '--level', fields.level || 'info'];
      if (fields.title) args.push('--title', fields.title);
      args.push(fields.message || '');
    } else if (cmd === 'show') args = ['show'];
    else if (cmd === 'run') args = ['run', fields.arg];
    else if (cmd === 'env-use') args = ['env', 'use', fields.arg];
    else if (cmd === 'kill') args = ['kill', fields.arg];
  }
  if (!args) return Promise.resolve(null);
  return new Promise((resolve) => {
    execFile(exe, args, { timeout: TIMEOUT_MS * 2 }, (error, stdout, stderr) => {
      resolve({ ok: !error, message: String(stdout || stderr || '').trim() });
    });
  });
}

// Low level: send {cmd, ...} and resolve with Kai's reply object (rejects with kai.Error if not ok).
async function request(cmd, fields = {}) {
  let reply;
  try {
    const raw = await roundtrip(JSON.stringify({ cmd, ...fields }) + '\n');
    reply = raw ? JSON.parse(raw) : { ok: false, message: 'empty reply' };
  } catch (error) {
    reply = await viaCli(cmd, fields);
    if (!reply) throw new KaiError(`Kai is not reachable (${error.message}): is the app running?`);
  }
  if (!reply.ok) throw new KaiError(reply.message || 'request failed');
  return reply;
}

// Tray notification (also kept in Kai's notification history). level: info | warning | error.
async function notify(message, { title = '', level = 'info' } = {}) {
  await request('raise', { message: String(message), title, level });
}

// Bring the Kai window to the front.
async function show() {
  await request('show');
}

// Start the Kai command called `name` in the app (fire and forget). Resolves with Kai's message.
async function run(name) {
  return (await request('run', { arg: name })).message || '';
}

// Names of the commands registered in Kai.
async function commands() {
  return (await request('list')).lines || [];
}

const env = {
  // Names of the environments.
  list: async () => (await request('env-list')).lines || [],
  // Name of the active environment.
  active: async () => (await request('env-list')).message || '',
  // Activate the environment called `name`.
  use: async (name) => (await request('env-use', { arg: name })).message || '',
};

// Processes Kai is tracking, as an array of objects (pid, name, ...).
async function ps() {
  const reply = await request('ps');
  return reply.items || reply.lines || [];
}

// Stop a tracked process by name or pid.
async function kill(target) {
  return (await request('kill', { arg: String(target) })).message || '';
}

// Import a kai.json/kai.yml (or the folder that holds it) into Kai.
async function importProject(projectPath) {
  return (await request('import', { path: projectPath })).message || '';
}

module.exports = { Error: KaiError, request, notify, show, run, commands, env, ps, kill, importProject };

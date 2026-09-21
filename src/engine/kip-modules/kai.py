"""kai - talk to the running Kai app (injected by Kai into every Python command).

    import kai
    kai.notify("Backup finished", title="Backup", level="info")
    print(kai.commands())          # names of the commands registered in Kai
    kai.env.use("Prod")            # switch the active environment
    kai.run("Deploy")              # start another Kai command in the app

Kai passes its socket in KAI_IPC_SOCKET. When the app can't be reached directly (for
example a script running inside WSL under a Windows Kai) it falls back to the `kai`
CLI (KAI_EXE, `kai` or `kai.exe` on PATH) for notify/show/run/env.use/kill.
Failures raise kai.Error.
"""
import json
import os
import shutil
import socket
import subprocess
import sys

_TIMEOUT = 10


class Error(Exception):
    """Kai refused the request or could not be reached."""


def _endpoint():
    return os.environ.get("KAI_IPC_SOCKET") or os.path.join(
        os.environ.get("TMPDIR", "/tmp"), "kai-ipc-v1")


def _roundtrip(line):
    path = _endpoint()
    if sys.platform == "win32":
        with open(path, "r+b", buffering=0) as pipe:
            pipe.write(line)
            return pipe.readline()
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(_TIMEOUT)
        sock.connect(path)
        sock.sendall(line)
        data = b""
        while not data.endswith(b"\n"):
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
        return data


def _via_cli(cmd, fields):
    exe = os.environ.get("KAI_EXE") or shutil.which("kai") or shutil.which("kai.exe")
    if not exe:
        return None
    arg = fields.get("arg")
    if cmd == "raise":
        args = ["raise", "--level", fields.get("level") or "info"]
        if fields.get("title"):
            args += ["--title", fields["title"]]
        args.append(fields.get("message", ""))
    elif cmd == "show":
        args = ["show"]
    elif cmd == "run":
        args = ["run", arg]
    elif cmd == "env-use":
        args = ["env", "use", arg]
    elif cmd == "kill":
        args = ["kill", arg]
    else:
        return None
    done = subprocess.run([exe] + args, capture_output=True, text=True, timeout=_TIMEOUT * 2)
    return {"ok": done.returncode == 0, "message": (done.stdout or done.stderr).strip()}


def request(cmd, **fields):
    """Low level: send {"cmd": cmd, ...} and return Kai's reply dict (raises kai.Error if not ok)."""
    line = (json.dumps({"cmd": cmd, **fields}) + "\n").encode()
    try:
        raw = _roundtrip(line)
        reply = json.loads(raw.decode()) if raw else {"ok": False, "message": "empty reply"}
    except (OSError, ValueError) as error:
        reply = _via_cli(cmd, fields)
        if reply is None:
            raise Error("Kai is not reachable (%s): is the app running?" % error) from None
    if not reply.get("ok"):
        raise Error(reply.get("message") or "request failed")
    return reply


def notify(message, title=None, level="info"):
    """Tray notification (also kept in Kai's notification history). level: info | warning | error."""
    request("raise", message=str(message), title=title or "", level=level)


def show():
    """Bring the Kai window to the front."""
    request("show")


def run(name):
    """Start the Kai command called `name` in the app (fire and forget). Returns Kai's message."""
    return request("run", arg=name).get("message", "")


def commands():
    """Names of the commands registered in Kai."""
    return request("list").get("lines", [])


class _Env:
    def list(self):
        """Names of the environments."""
        return request("env-list").get("lines", [])

    def active(self):
        """Name of the active environment."""
        return request("env-list").get("message", "")

    def use(self, name):
        """Activate the environment called `name`."""
        return request("env-use", arg=name).get("message", "")


env = _Env()


def ps():
    """Processes Kai is tracking, as a list of dicts (pid, name, ...)."""
    reply = request("ps")
    return reply.get("items") or reply.get("lines", [])


def kill(target):
    """Stop a tracked process by name or pid."""
    return request("kill", arg=str(target)).get("message", "")


def import_project(path):
    """Import a kai.json/kai.yml (or the folder that holds it) into Kai."""
    return request("import", path=path).get("message", "")

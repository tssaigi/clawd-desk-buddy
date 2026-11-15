"""Clawd status bridge: mirrors what Claude Code is doing onto the Core2.

Reads the live Claude Code session transcripts (~/.claude/projects/*/*.jsonl,
one file per session), works out the current state of
the most recently active session, and sends it to the Core2 over its USB
serial port as `status <state>`:

    thinking  coding  reading  searching  delegating  asking  done  idle

The firmware performs the matching official Clawd scene (clawd_director.cpp,
director_status). It also sends the list of live sessions for the sessions
page (sessions_view.cpp): `sess clear`, one `sess add <state> <ctx%> <title>`
per session, `sess done`. And the colony page (colony_view.cpp): every live
session with its agents (the main Claude plus each helper agent), from the
transcript watcher in colony_watcher.py: `col clear`,
`col s <sid> <working> <title>`, `col a <sid> <state> <name>` per agent, `col done`. Context fill is the last request's input tokens over
the window (1M once a session is past 200k, else 200k), so it's approximate. The state is repeated every few seconds; if the bridge
stops, the device lets the mirror lapse after a minute.

Run:  python3 clawd_status_bridge.py        (needs pyserial: pip install pyserial)
Stop: kill the pid in bridge.pid, or use uninstall_macos.sh if it runs at login.
Port: found by the Core2's USB chip (CH9102 or CP2104); set CLAWD_PORT to force one.
Privacy: everything stays on this computer. Session titles, agent names and
states go to the device over the USB cable and nowhere else.
Note: opening the Core2's serial port resets it once (the USB bridge toggles
the reset line); the bridge then keeps the port open. Stop the bridge before
flashing, it holds the port.
"""
import glob, json, os, sys, time
from datetime import datetime

import serial  # pyserial (bundled with PlatformIO's Python)

from serial.tools import list_ports

# The hub + watcher for the colony page (sessions, subagents, their states).
try:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import colony_watcher as crab
except Exception as e:                      # missing or broken: the rest still works
    crab = None
    print("colony watcher unavailable: %r" % (e,), flush=True)

PROJECTS = os.path.expanduser("~/.claude/projects")
HERE = os.path.dirname(os.path.abspath(__file__))
ACTIVE_S = 15 * 60       # a session counts as live this long after its last line
DONE_HOLD_S = 8          # "done" shows this long, then idle
REPEAT_S = 15            # resend the current state this often (device TTL is 60 s)
LIST_EVERY_S = 3         # session list: at most this often, and only when it changed
LIST_REPEAT_S = 10       # ...but at least this often (the page shows "waiting" after 20 s)
MAX_LIST = 10
COL_MAX_SESS = 6
COL_MAX_AGENTS = 8
COL_ACTIVE = {"thinking", "working", "reading", "searching", "moving", "talking", "arguing", "error"}
COL_WORKING_S = 10 * 60   # an active state this recent means the agent is still at it
COL_WINDOW_S = 30 * 60    # sessions shown on the colony page
POLL_S = 0.7

FILE_TOOLS = {"Read", "Glob", "Grep", "LS", "NotebookRead", "ToolSearch",
              "ListMcpResourcesTool", "ReadMcpResourceTool"}
WEB_TOOLS = {"WebFetch", "WebSearch"}
WEB_PREFIXES = ("mcp__Claude_Browser__", "mcp__claude-in-chrome__", "mcp__plugin_brightdata")
CODE_TOOLS = {"Edit", "MultiEdit", "NotebookEdit", "Write", "Bash"}
SPAWN_TOOLS = {"Agent", "Task", "Workflow"}
ASK_TOOLS = {"AskUserQuestion"}


def line_time(d):
    """When the transcript line was written (its own timestamp), else now."""
    ts = d.get("timestamp")
    if isinstance(ts, str):
        try:
            return datetime.fromisoformat(ts.replace("Z", "+00:00")).timestamp()
        except ValueError:
            pass
    return time.time()


def log(msg):
    print(time.strftime("[%H:%M:%S] ") + msg, flush=True)


def tool_state(name):
    if name in CODE_TOOLS:
        return "coding"
    if name in FILE_TOOLS:
        return "reading"
    if name in WEB_TOOLS or name.startswith(WEB_PREFIXES):
        return "searching"
    if name in SPAWN_TOOLS:
        return "delegating"
    if name in ASK_TOOLS:
        return "asking"
    return "coding"           # any other tool: Claude is doing something


def line_state(d):
    """The state one transcript line implies, or None."""
    kind = d.get("type")
    if kind not in ("assistant", "user") or d.get("isSidechain"):
        return None
    msg = d.get("message") or {}
    content = msg.get("content")
    if kind == "assistant" and isinstance(content, list):
        tools = [b for b in content if isinstance(b, dict) and b.get("type") == "tool_use"]
        if tools:
            return tool_state(tools[-1].get("name", ""))
        if msg.get("stop_reason") == "end_turn":
            return "done"
        return "thinking"     # thinking or text mid-turn
    if kind == "user":
        if d.get("isMeta"):
            return None
        return "thinking"     # a prompt, or a tool result Claude is now reading
    return None


class Session:
    def __init__(self, path):
        self.path = path
        self.offset = max(0, os.path.getsize(path) - 300_000)
        self.fresh = True
        self.buf = b""
        self.state = None
        self.at = 0.0
        self.title = ""
        self.cwd = ""
        self.ctx_tokens = None
        self.sid = os.path.basename(path)[:-6]

    def poll(self):
        try:
            size = os.path.getsize(self.path)
        except OSError:
            return
        if size < self.offset:
            self.offset, self.buf = 0, b""
        if size == self.offset:
            return
        with open(self.path, "rb") as f:
            f.seek(self.offset)
            chunk = f.read(4 << 20)
        self.offset += len(chunk)
        lines = (self.buf + chunk).split(b"\n")
        self.buf = lines.pop()
        if self.fresh and lines:
            lines = lines[1:]          # the first line of a tail read may be cut
        self.fresh = False
        for raw in lines:
            try:
                d = json.loads(raw)
            except ValueError:
                continue
            if d.get("type") == "custom-title" and d.get("customTitle"):
                self.title = str(d["customTitle"])
            if d.get("cwd"):
                self.cwd = d["cwd"]
            if d.get("type") == "assistant" and not d.get("isSidechain"):
                u = (d.get("message") or {}).get("usage") or {}
                if u:
                    self.ctx_tokens = (u.get("input_tokens", 0) + u.get("cache_creation_input_tokens", 0)
                                       + u.get("cache_read_input_tokens", 0) + u.get("output_tokens", 0))
            st = line_state(d)
            if st:
                self.state = st
                self.at = min(line_time(d), time.time())


def display_state(s, now):
    if s.state == "done" and now - s.at > DONE_HOLD_S:
        return "idle"
    return s.state


def ascii_title(s):
    t = s.title or (os.path.basename(s.cwd.rstrip("/")) if s.cwd else "") or ("Session " + s.sid[:6])
    t = "".join(ch if 32 <= ord(ch) < 127 else "?" for ch in t.replace("\u2013", "-").replace("\u2014", "-"))
    return " ".join(t.split())[:38]


def ctx_pct(s):
    if s.ctx_tokens is None:
        return -1
    window = 1_000_000 if s.ctx_tokens > 200_000 else 200_000
    return min(100, round(100 * s.ctx_tokens / window))


def session_list(sessions):
    now = time.time()
    live = sorted((s for s in sessions.values() if s.state and now - s.at < ACTIVE_S),
                  key=lambda x: x.at, reverse=True)[:MAX_LIST]
    return [(display_state(s, now), ctx_pct(s), ascii_title(s)) for s in live]


def colony_rows(hub):
    """[(sid, working, title, [(state, name), ...]), ...], most recent first."""
    if hub is None:
        return []
    now = time.time()
    out = []
    for s in hub.snapshot()["sessions"]:
        if s.get("source") != "auto" or not s["agents"] or now - s["ts"] > COL_WINDOW_S:
            continue
        agents = sorted(s["agents"], key=lambda a: (a["agent"] != "Claude", -a["ts"]))[:COL_MAX_AGENTS]
        working = any(a["state"] in COL_ACTIVE and now - a["ts"] < COL_WORKING_S for a in agents)
        title = "".join(ch if 32 <= ord(ch) < 127 else "?" for ch in (s["title"] or s["id"][:8]))
        out.append((s["ts"], s["id"][:8], 1 if working else 0, " ".join(title.split())[:38],
                    [(a["state"], " ".join("".join(ch if 32 <= ord(ch) < 127 else "?" for ch in a["agent"]).split())[:26])
                     for a in agents]))
    out.sort(key=lambda r: r[0], reverse=True)
    return [r[1:] for r in out[:COL_MAX_SESS]]


def current_state(sessions):
    now = time.time()
    live = [s for s in sessions.values() if s.state and now - s.at < ACTIVE_S]
    if not live:
        return "idle"
    s = max(live, key=lambda x: x.at)
    if s.state == "done" and now - s.at > DONE_HOLD_S:
        return "idle"
    return s.state


def scan(sessions):
    now = time.time()
    for path in glob.glob(os.path.join(PROJECTS, "*", "*.jsonl")):
        try:
            if now - os.path.getmtime(path) > ACTIVE_S:
                continue
        except OSError:
            continue
        if path not in sessions:
            sessions[path] = Session(path)
    for path in [p for p, s in sessions.items() if not os.path.exists(p)]:
        del sessions[path]


# USB-serial chips the Core2 ships with: CH9102 (1a86:55d4) and CP2104 (10c4:ea60).
CORE2_USB = {(0x1A86, 0x55D4), (0x10C4, 0xEA60)}


def find_ports():
    if os.environ.get("CLAWD_PORT"):
        return [os.environ["CLAWD_PORT"]]
    found = [p.device for p in list_ports.comports() if (p.vid, p.pid) in CORE2_USB]
    # macOS lists each device twice (tty.* and cu.*): keep the cu.* one.
    return sorted(d for d in found if "/dev/tty." not in d)


def open_port():
    ports = find_ports()
    if not ports:
        return None
    s = serial.Serial()
    s.port, s.baudrate, s.timeout, s.write_timeout = ports[0], 115200, 0, 1
    s.open()
    log("connected to " + ports[0] + " (the Core2 restarts once)")
    time.sleep(2.5)                    # let it boot before the first command
    return s


def main():
    with open(os.path.join(HERE, "bridge.pid"), "w") as f:
        f.write(str(os.getpid()))
    hub = None
    if crab is not None:
        hub = crab.Hub()
        crab.Watcher(hub).start()           # daemon thread
    sessions, port = {}, None
    sent, sent_at, next_scan, waiting_logged = None, 0.0, 0.0, False
    list_sent, list_at = None, 0.0
    col_sent, col_at = None, 0.0
    while True:
        now = time.time()
        if now >= next_scan:
            scan(sessions)
            next_scan = now + 3
        for s in list(sessions.values()):
            s.poll()
        state = current_state(sessions)
        if port is None:
            try:
                port = open_port()
                sent, list_sent, col_sent = None, None, None
            except (OSError, serial.SerialException) as e:
                port = None
                log("port busy or gone: %s" % e)
            if port is None:
                if not waiting_logged:
                    log("waiting for the Core2's USB cable")
                    waiting_logged = True
                time.sleep(2)
                continue
            waiting_logged = False
        rows = session_list(sessions)
        if (rows != list_sent and now - list_at >= LIST_EVERY_S) or now - list_at >= LIST_REPEAT_S:
            try:
                lines = ["sess clear"] + ["sess add %s %d %s" % r for r in rows] + ["sess done"]
                port.write(("\n".join(lines) + "\n").encode())
                if rows != list_sent:
                    log("sessions: " + ", ".join("%s (%s, ctx %s%%)" % (r[2], r[0], r[1]) for r in rows))
                list_sent, list_at = rows, now
            except (OSError, serial.SerialException) as e:
                log("lost the Core2: %s" % e)
                try:
                    port.close()
                except Exception:
                    pass
                port = None
                continue
        col = colony_rows(hub)
        if (col != col_sent and now - col_at >= LIST_EVERY_S) or now - col_at >= LIST_REPEAT_S:
            try:
                lines = ["col clear"]
                for sid, working, title, agents in col:
                    lines.append("col s %s %d %s" % (sid, working, title))
                    lines += ["col a %s %s %s" % (sid, st, name) for st, name in agents]
                lines.append("col done")
                port.write(("\n".join(lines) + "\n").encode())
                if col != col_sent:
                    log("colony: " + "; ".join("%s%s [%s]" % (t, "" if w else " (resting)",
                        ", ".join("%s:%s" % (n, st) for st, n in ag)) for _, w, t, ag in col))
                col_sent, col_at = col, now
            except (OSError, serial.SerialException) as e:
                log("lost the Core2: %s" % e)
                try:
                    port.close()
                except Exception:
                    pass
                port = None
                continue
        if state != sent or now - sent_at >= REPEAT_S:
            try:
                port.write(("status %s\n" % state).encode())
                port.read(4096)            # drain the device's log so its buffer never fills
                if state != sent:
                    log("state: " + state)
                sent, sent_at = state, now
            except (OSError, serial.SerialException) as e:
                log("lost the Core2: %s" % e)
                try:
                    port.close()
                except Exception:
                    pass
                port = None
                continue
        else:
            try:
                port.read(4096)
            except (OSError, serial.SerialException):
                pass
        time.sleep(POLL_S)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass

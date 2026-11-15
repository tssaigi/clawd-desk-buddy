#!/usr/bin/env python3
"""Session watcher for the colony page: a hub plus a transcript watcher.

A watcher thread follows the transcript files Claude Code writes under
~/.claude/projects (one file per session, one per subagent) and turns tool
calls into agent states (thinking, working, reading, searching, moving,
talking, celebrating, error...). Standard library only. Nothing is sent
anywhere: clawd_status_bridge.py reads the hub and writes to the device over USB.

Environment: CRAB_PROJECTS (transcript root), CRAB_WINDOW_MIN (how recent a
session must be to appear, minutes).
"""
import json
import os
import queue
import sys
import threading
import time
from datetime import datetime

PROJECTS = os.path.expanduser(os.environ.get("CRAB_PROJECTS", "~/.claude/projects"))
WINDOW = float(os.environ.get("CRAB_WINDOW_MIN", "30")) * 60      # how recent a session must be to appear
SESSION_EXPIRE = 40 * 60                                           # session leaves the sidebar after this much silence
SUB_EXPIRE = 10 * 60                                               # a quiet subagent crab leaves after this

STATES = {"idle", "thinking", "working", "reading", "searching", "moving",
          "talking", "arguing", "celebrating", "error", "sleeping", "gone"}
ALIASES = {
    "coding": "working", "typing": "working", "busy": "working", "run": "working",
    "browsing": "reading", "files": "reading", "read": "reading", "browse": "reading",
    "web": "searching", "search": "searching", "surf": "searching",
    "carry": "moving", "move": "moving", "copy": "moving", "write": "moving",
    "chat": "talking", "say": "talking", "talk": "talking",
    "fight": "arguing", "argue": "arguing", "debate": "arguing",
    "done": "celebrating", "success": "celebrating", "celebrate": "celebrating",
    "fail": "error", "failed": "error", "oops": "error",
    "sleep": "sleeping", "nap": "sleeping", "think": "thinking",
    "remove": "gone", "leave": "gone", "exit": "gone",
}


def clip(value, limit):
    return str(value or "").strip()[:limit]


# ---------------------------------------------------------------- hub

class Hub:
    def __init__(self):
        self.lock = threading.Lock()
        self.sessions = {}   # sid -> {id,title,source,ts,agents{name->ev}}
        self.log = []
        self.subs = []

    def _session(self, sid, title="", source="manual"):
        s = self.sessions.get(sid)
        if not s:
            s = {"id": sid, "title": title or ("Manual crabs" if sid == "manual" else sid[:8]),
                 "source": source, "ts": time.time(), "agents": {}}
            self.sessions[sid] = s
        return s

    def _emit(self, kind, payload):
        with self.lock:
            subs = list(self.subs)
        for q in subs:
            q.put((kind, payload))

    def apply(self, raw):
        name = clip(raw.get("agent") or raw.get("name"), 40)
        if not name:
            return None
        state = clip(raw.get("state"), 20).lower() or "idle"
        state = ALIASES.get(state, state)
        if state not in STATES:
            state = "working"
        sid = clip(raw.get("session"), 64) or "manual"
        try:
            ts = float(raw.get("ts") or time.time())
        except (TypeError, ValueError):
            ts = time.time()
        ev = {"agent": name, "state": state, "text": clip(raw.get("text"), 240),
              "to": clip(raw.get("to"), 40), "task": clip(raw.get("task"), 100),
              "role": clip(raw.get("role"), 30), "ts": ts, "session": sid}
        with self.lock:
            s = self._session(sid, clip(raw.get("session_title"), 60), clip(raw.get("source"), 10) or "manual")
            if raw.get("session_title"):
                s["title"] = clip(raw.get("session_title"), 60)
            s["ts"] = max(s["ts"], ts)
            if state == "gone":
                s["agents"].pop(name, None)
            else:
                prev = s["agents"].get(name, {})
                ev["role"] = ev["role"] or prev.get("role", "")
                ev["task"] = ev["task"] or prev.get("task", "")
                s["agents"][name] = ev
            ev["session_title"] = s["title"]
            ev["source"] = s["source"]
            self.log.append(ev)
            del self.log[:-400]
        self._emit("event", ev)
        return ev

    def set_title(self, sid, title, source="auto"):
        with self.lock:
            s = self._session(sid, title, source)
            changed = s["title"] != title
            s["title"] = title
        if changed:
            self._emit("session", {"id": sid, "title": title, "source": source})

    def drop_session(self, sid):
        with self.lock:
            gone = self.sessions.pop(sid, None)
        if gone:
            self._emit("session_gone", {"id": sid})

    def reset(self):
        with self.lock:
            self.sessions.clear()
            self.log.clear()
        self._emit("reset", {})

    def snapshot(self):
        with self.lock:
            return {"sessions": [{"id": s["id"], "title": s["title"], "source": s["source"],
                                  "ts": s["ts"], "agents": list(s["agents"].values())}
                                 for s in self.sessions.values()],
                    "log": list(self.log[-150:])}

    def subscribe(self):
        q = queue.Queue()
        with self.lock:
            self.subs.append(q)
        return q

    def unsubscribe(self, q):
        with self.lock:
            if q in self.subs:
                self.subs.remove(q)


HUB = Hub()


# ---------------------------------------------------------------- transcript watcher

FILE_TOOLS = {"Read", "Glob", "Grep", "LS", "NotebookRead", "ToolSearch", "ListMcpResourcesTool", "ReadMcpResourceTool"}
WEB_TOOLS = {"WebFetch", "WebSearch"}
EDIT_TOOLS = {"Edit", "MultiEdit", "NotebookEdit"}
SPAWN_TOOLS = {"Agent", "Task"}
THINK_TOOLS = {"Skill", "TodoWrite", "TaskCreate", "TaskUpdate", "TaskList", "EnterPlanMode", "ExitPlanMode", "Workflow"}
MOVE_CMDS = ("mv ", "cp ", "rsync ", "ln ", "mkdir ", "cp -", "mv -")
WEB_PREFIXES = ("mcp__Claude_Browser__", "mcp__claude-in-chrome__", "mcp__plugin_brightdata")


def sub_name(desc, agent_type, agent_id):
    desc = clip(desc, 24)
    if desc:
        return desc
    return (clip(agent_type, 14) or "Subagent") + " " + agent_id[-4:]


def describe(name, inp):
    inp = inp or {}
    path = inp.get("file_path") or inp.get("path") or inp.get("notebook_path")
    if path:
        return os.path.basename(str(path))[:44]
    if inp.get("url"):
        s = str(inp["url"]).split("//")[-1].split("/")[0]
        return s[:44]
    for k in ("description", "query", "pattern", "prompt", "command", "skill"):
        if inp.get(k):
            return " ".join(str(inp[k]).split())[:56]
    return ""


def tool_event(block):
    name = block.get("name", "")
    inp = block.get("input") or {}
    what = describe(name, inp)
    if name in SPAWN_TOOLS:
        desc = clip(inp.get("description") or inp.get("subagent_type") or "a helper", 24)
        return "talking", "Delegating: " + desc, desc
    if name in FILE_TOOLS:
        return "reading", what, ""
    if name in WEB_TOOLS or name.startswith(WEB_PREFIXES):
        return "searching", what or name.split("__")[-1], ""
    if name == "Write":
        return "moving", what, ""
    if name == "Bash":
        cmd = str(inp.get("command", "")).lstrip()
        return ("moving" if cmd.startswith(MOVE_CMDS) else "working"), (clip(inp.get("description"), 56) or what), ""
    if name in EDIT_TOOLS:
        return "working", what, ""
    if name == "AskUserQuestion":
        return "talking", "Asking you a question", ""
    if name in THINK_TOOLS:
        return "thinking", what or name, ""
    return "working", what or name.split("__")[-1], ""


def parse_ts(d):
    s = d.get("timestamp")
    if isinstance(s, str):
        try:
            return datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()
        except ValueError:
            pass
    return time.time()


class Tracked:
    def __init__(self, path, sid, kind):
        self.path, self.sid, self.kind = path, sid, kind
        self.offset, self.buf, self.fresh = 0, b"", True
        self.name = "Claude" if kind == "main" else None
        self.role = "main" if kind == "main" else ""
        self.meta_tries = 0
        self.last_seen = time.time()
        self.last_state = None
        self.cwd = ""

    def load_meta(self):
        if self.kind != "sub" or self.name:
            return
        meta = self.path[:-6] + ".meta.json"
        agent_id = os.path.basename(self.path)[6:-6]
        try:
            with open(meta) as f:
                m = json.load(f)
            self.name = sub_name(m.get("description"), m.get("agentType"), agent_id)
            self.role = clip(m.get("agentType"), 20)
        except (OSError, ValueError):
            self.meta_tries += 1
            if self.meta_tries > 4:
                self.name = sub_name("", "", agent_id)


class Watcher(threading.Thread):
    def __init__(self, hub):
        super().__init__(daemon=True)
        self.hub = hub
        self.files = {}
        self.dead = {}          # path -> mtime at which it was retired
        self.titles = {}        # sid -> title shown
        self.activity = {}      # sid -> last activity wallclock
        self.next_scan = 0

    def run(self):
        while True:
            try:
                now = time.time()
                if now >= self.next_scan:
                    self.scan(now)
                    self.next_scan = now + 3
                for t in list(self.files.values()):
                    self.poll(t)
                self.gc(time.time())
            except Exception as exc:                                   # never die
                print("watcher error: %r" % (exc,), file=sys.stderr, flush=True)
            time.sleep(0.8)

    # -- discovery
    def scan(self, now):
        try:
            projects = [e for e in os.scandir(PROJECTS) if e.is_dir()]
        except OSError:
            return
        for proj in projects:
            try:
                entries = list(os.scandir(proj.path))
            except OSError:
                continue
            for e in entries:
                if e.name.endswith(".jsonl") and e.is_file():
                    sid = e.name[:-6]
                    if self.recent(e.path, now) or sid in self.activity:
                        self.track(e.path, sid, "main")
                elif e.is_dir() and e.name in self.activity or (e.is_dir() and self.session_active(proj.path, e.name, now)):
                    sub = os.path.join(e.path, "subagents")
                    try:
                        subs = [x for x in os.scandir(sub) if x.name.startswith("agent-") and x.name.endswith(".jsonl")]
                    except OSError:
                        continue
                    for x in subs:
                        if self.recent(x.path, now):
                            self.track(x.path, e.name, "sub")

    def session_active(self, proj, sid, now):
        try:
            return now - os.stat(os.path.join(proj, sid + ".jsonl")).st_mtime < WINDOW
        except OSError:
            return False

    def recent(self, path, now):
        try:
            m = os.stat(path).st_mtime
        except OSError:
            return False
        if path in self.dead and m <= self.dead[path]:
            return False
        return now - m < WINDOW

    def track(self, path, sid, kind):
        if path not in self.files:
            self.files[path] = Tracked(path, sid, kind)

    # -- reading
    def poll(self, t):
        try:
            size = os.stat(t.path).st_size
        except OSError:
            return
        if t.fresh:
            t.offset = max(0, size - 400000)
        elif size < t.offset:
            t.offset, t.buf = 0, b""
        if size == t.offset and not t.fresh:
            return
        t.load_meta()
        if t.kind == "sub" and not t.name and t.meta_tries <= 4:
            return                                                     # wait briefly for the meta file
        with open(t.path, "rb") as f:
            f.seek(t.offset)
            chunk = f.read(2 << 20)
        t.offset += len(chunk)
        lines = (t.buf + chunk).split(b"\n")
        t.buf = lines.pop()
        if t.fresh and t.offset - len(chunk) > 0 and lines:
            lines = lines[1:]                                          # first line is probably cut
        events = []
        for raw in lines:
            if not raw.strip():
                continue
            try:
                d = json.loads(raw)
            except ValueError:
                continue
            self.interpret(t, d, events)
        if events:
            self.activity[t.sid] = time.time()
            t.last_seen = time.time()
        if t.fresh:
            events = events[-1:]                                       # catch up silently: only the latest state
            t.fresh = False
            self.activity.setdefault(t.sid, time.time() - 1)
        self.publish_title(t)
        for ev in events[-12:]:
            self.hub.apply(ev)

    def publish_title(self, t):
        title = self.titles.get(t.sid) or (os.path.basename(t.cwd) if t.cwd else "")
        if title and self.hub.sessions.get(t.sid, {}).get("title") != title:
            self.hub.set_title(t.sid, title, "auto")

    def interpret(self, t, d, out):
        kind = d.get("type")
        if d.get("cwd") and t.kind == "main":
            t.cwd = d["cwd"]
        if kind == "custom-title" and d.get("customTitle"):
            self.titles[t.sid] = clip(d["customTitle"], 60)
            return
        if kind not in ("assistant", "user") or not t.name:
            return
        msg = d.get("message") or {}
        content = msg.get("content")
        base = {"agent": t.name, "role": t.role, "session": t.sid, "source": "auto",
                "session_title": self.titles.get(t.sid, ""), "ts": parse_ts(d)}
        if kind == "assistant" and isinstance(content, list):
            tool = None
            for b in content:
                if isinstance(b, dict) and b.get("type") == "tool_use":
                    tool = b
            if tool:
                state, text, to = tool_event(tool)
                out.append(dict(base, state=state, text=text, to=to))
            elif msg.get("stop_reason") == "end_turn":
                out.append(dict(base, state="celebrating", text="Finished" if t.kind == "sub" else "All done"))
            elif any(isinstance(b, dict) and b.get("type") == "thinking" for b in content):
                out.append(dict(base, state="thinking", text=""))
            elif any(isinstance(b, dict) and b.get("type") == "text" for b in content):
                out.append(dict(base, state="talking", text=""))
        elif kind == "user":
            if isinstance(content, list) and any(isinstance(b, dict) and b.get("type") == "tool_result" for b in content):
                bad = any(isinstance(b, dict) and b.get("is_error") for b in content)
                out.append(dict(base, state="error" if bad else "thinking", text="That did not work" if bad else ""))
            elif d.get("isSidechain") is not True and not d.get("isMeta"):
                out.append(dict(base, state="thinking", text="New request" if t.kind == "main" else "Starting"))

    # -- cleanup
    def gc(self, now):
        for path, t in list(self.files.items()):
            if t.kind == "sub" and now - t.last_seen > SUB_EXPIRE and not t.fresh and t.name:
                try:
                    m = os.stat(path).st_mtime
                except OSError:
                    m = now
                self.dead[path] = m
                self.hub.apply({"agent": t.name, "state": "gone", "session": t.sid, "source": "auto"})
                del self.files[path]
        for sid, last in list(self.activity.items()):
            if now - last > SESSION_EXPIRE:
                for path, t in list(self.files.items()):
                    if t.sid == sid:
                        try:
                            self.dead[path] = os.stat(path).st_mtime
                        except OSError:
                            self.dead[path] = now
                        del self.files[path]
                self.activity.pop(sid, None)
                self.titles.pop(sid, None)
                self.hub.drop_session(sid)

# Public domain. No rights reserved. Wholly AI-authored.
"""Application sessions and exact-plan leases for manager-owned startup children."""

import json
import uuid
from dataclasses import dataclass, field
from pathlib import Path


def canonical(ball):
    return str(Path(ball).resolve())


@dataclass
class Session:
    ball: str
    installer: bool
    name: str
    id: str = field(default_factory=lambda: uuid.uuid4().hex)
    ordinal: int = 0
    state: str = "preparing"
    process: object = None
    plan: dict = field(default_factory=dict)
    log: object = None
    code: object = None
    stop_sent: bool = False
    leases: list = field(default_factory=list)

    @property
    def active(self):
        return self.state in ("preparing", "running", "stopping", "cancelling", "finishing")


class Sessions:
    def __init__(self):
        self.records = {}

    def active(self, ball=None):
        return [
            session
            for session in self.records.values()
            if session.active and (ball is None or session.ball == canonical(ball))
        ]

    def can_start(self, ball, installer=False):
        peers = self.active(ball)
        return not any(
            peer.installer or peer.state in ("preparing", "cancelling", "finishing")
            for peer in peers
        ) and not (installer and peers)

    def reserve(self, ball, installer, name):
        if not self.can_start(ball, installer):
            raise ValueError("Wait for this ball’s installer or launch preparation to finish.")
        session = Session(canonical(ball), installer, name)
        session.ordinal = len(self.records) + 1
        self.records[session.id] = session
        return session

    def for_ball(self, ball):
        return [session for session in self.records.values() if session.ball == canonical(ball)]


class StartupPool:
    def __init__(self):
        self.records = {}
        self.closing = {}

    @staticmethod
    def key(ball, plan):
        program = plan.get("program")
        identity = program.replace("/", "\\").upper() if program else tuple(plan.get("command", []))
        return canonical(ball), identity

    @staticmethod
    def fingerprint(plan):
        value = {
            key: plan.get(key) for key in ("command", "cwd", "environment", "unsetEnvironment")
        }
        value["unsetEnvironment"] = sorted(value.get("unsetEnvironment") or [])
        return json.dumps(value, sort_keys=True, separators=(",", ":"))

    def acquire(self, ball, plans, session_id, spawn, alive):
        leases = []
        try:
            self.closing = {key: process for key, process in self.closing.items() if alive(process)}
            for index, plan in enumerate(plans):
                key = self.key(ball, plan)
                if key in leases:
                    if self.records[key]["fingerprint"] != self.fingerprint(plan):
                        raise ValueError(
                            "A launch requests conflicting settings for one startup service."
                        )
                    continue
                if key in self.closing:
                    raise ValueError(
                        "Startup service cleanup is still finishing. Retry the launch shortly."
                    )
                record = self.records.get(key)
                fingerprint = self.fingerprint(plan)
                if record is not None and record["fingerprint"] != fingerprint:
                    raise ValueError(
                        "Startup service settings differ from a running application. Close the affected ball’s applications before changing service settings."
                    )
                if record is not None and not alive(record["process"]) and record["users"]:
                    raise ValueError(
                        "A shared startup service exited. Close the affected ball’s running applications before restarting it."
                    )
                if record is None or not alive(record["process"]):
                    record = {
                        "process": spawn(plan, index),
                        "users": set(),
                        "fingerprint": fingerprint,
                    }
                    self.records[key] = record
                record["users"].add(session_id)
                leases.append(key)
        except Exception as error:
            return leases, error
        return leases, None

    def release(self, leases, session_id, stop):
        for key in leases:
            record = self.records.get(key)
            if record is None:
                continue
            record["users"].discard(session_id)
            if not record["users"]:
                del self.records[key]
                self.closing[key] = record["process"]
                stop(record["process"])


STARTUP_POOL = StartupPool()

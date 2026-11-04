# Public domain. No rights reserved. Wholly AI-authored.
"""Per-ball component activity and read-only file-state projections."""


def active(jobs, ball):
    return bool(ball is not None and jobs.get(str(ball), {}).get("active"))


def activity_ids(root, job):
    if not job or not job.get("active"):
        return set()
    progress = job.get("progress", {})
    sources = set(progress.get("activeSources", []))
    paths = {path.casefold() for path in progress.get("activePaths", [])}
    if not sources and not paths:
        sources = set(job.get("sources", []))
        paths = {path.casefold() for path in job.get("paths", [])}
    result = set()

    def visit(node):
        own_paths = [node.get("path", ""), *node.get("actualPaths", [])]
        own = node.get("source") in sources or (
            not sources and any(path.casefold() in paths for path in own_paths)
        )
        children = [visit(child) for child in node.get("children", [])]
        if own or any(children):
            result.add(node["id"])
            return True
        return False

    visit(root)
    return result


def file_state(record):
    present = bool(record.get("present"))
    status = record.get("status", "")
    removable = bool(record.get("managed")) and present and status == "managed"
    result = {key: record[key] for key in ("managed", "status", "reason") if key in record}
    result.update(present=present, removable=removable)
    if record.get("path"):
        result["actualPath"] = record["path"]
    if record.get("source"):
        result["installedSource"] = record["source"]
    return result


def state_selection(files, source_ids=None):
    seen, result = set(), []
    for file in files:
        source = file.get("source")
        if (
            file.get("provided")
            or not source
            or (source_ids is not None and source not in source_ids)
        ):
            continue
        pair = source, file["path"]
        if pair not in seen:
            seen.add(pair)
            result.append({"source": source, "path": file["path"]})
    return result


def source_states(records):
    grouped = {}
    for record in records:
        source = record.get("selectedSource")
        if source:
            grouped.setdefault(source, {})[record["path"].casefold()] = file_state(record)
    result = {}
    severity = {"missing": 0, "managed": 1, "unowned": 2, "modified": 3, "protected": 4}
    for source, paths in grouped.items():
        files = list(paths.values())
        status = max(
            (file.get("status", "protected") for file in files),
            key=lambda value: severity.get(value, 5),
        )
        present = [file for file in files if file["present"]]
        removable = sum(file["removable"] for file in files)
        actual_paths = [file["actualPath"] for file in present]
        result[source] = {
            "present": bool(present),
            "managed": bool(present) and status == "managed",
            "status": status,
            "removable": bool(removable),
            "removableCount": removable,
            "actualFiles": files,
            "actualPaths": actual_paths,
            "actualPath": actual_paths[0] if actual_paths else None,
        }
        reasons = list(dict.fromkeys(file["reason"] for file in files if file.get("reason")))
        result[source]["reason"] = "\n".join(reasons)
    return result


def file_tooltip(node):
    text = node.get("path") or node.get("detail") or node["title"]
    actual = node.get("actualPaths", [])
    if actual and (len(actual) > 1 or actual[0].casefold() != node.get("path", "").casefold()):
        text += "\nInstalled at:\n" + "\n".join(actual)
    if node.get("reason"):
        text += "\n" + node["reason"]
    return text


def state_label(record):
    if record.get("status") == "protected":
        return "Installed · protected" if record.get("present") else "Protected · not removable"
    if not record.get("present"):
        return "Available"
    if record.get("status") == "modified":
        return "Installed · modified, preserved"
    if record.get("managed"):
        return "Installed · managed"
    return "Installed · not managed"


class DesiredComponents:
    def __init__(self, allowed, selected):
        self.allowed = set(allowed)
        self.desired = set(selected) & self.allowed
        self.actual, self.revisions = {}, {}
        self.revision = 0
        self.initialized = False

    def edit(self, selected):
        selected = set(selected) & self.allowed
        self.revision += 1
        for source in self.desired ^ selected:
            self.revisions[source] = self.revision
        self.desired = selected
        return self.clamp_protected()

    def update_actual(self, records):
        previous = self.present()
        protected_intent = self.pending() | self.revisions.keys()
        self.actual.update(
            {source: record for source, record in records.items() if source in self.allowed}
        )
        if self.initialized:
            actual = self.present()
            self.desired.update(actual - previous - protected_intent)
            self.desired.difference_update(previous - actual - protected_intent)

    def present(self):
        return {source for source, record in self.actual.items() if record.get("present")}

    def initialize(self, *, has_history, explicit=False):
        if has_history and not explicit:
            self.desired = self.present()
        else:
            self.desired.update(self.present() - self.revisions.keys())
        self.initialized = True
        self.clamp_protected()

    def protected(self):
        return {
            source
            for source, record in self.actual.items()
            if record.get("status") == "protected"
            or (
                record.get("present")
                and (record.get("status") != "managed" or not record.get("removable"))
            )
        }

    def clamp_protected(self):
        changed = set()
        for source in self.protected():
            present = bool(self.actual[source].get("present"))
            if (source in self.desired) != present:
                changed.add(source)
                if present:
                    self.desired.add(source)
                else:
                    self.desired.discard(source)
        return changed

    def pending(self):
        return self.desired ^ self.present()

    def next_step(self):
        self.clamp_protected()
        remove = self.present() - self.desired
        remove = {
            source
            for source in remove
            if self.actual[source].get("status") == "managed"
            and self.actual[source].get("removable")
        }
        install = self.desired - self.present()
        if remove:
            return "remove", remove
        if install:
            return "install", install
        return None, set()

    def batch(self, attempted):
        return {source: self.revisions.get(source, 0) for source in attempted}

    def failed(self, batch):
        for source, revision in batch.items():
            if self.revisions.get(source, 0) != revision:
                continue
            if self.actual.get(source, {}).get("present"):
                self.desired.add(source)
            else:
                self.desired.discard(source)
        self.clamp_protected()

    def settle(self, batch):
        unfinished = {
            source
            for source, revision in batch.items()
            if self.revisions.get(source, 0) == revision
            and ((source in self.desired) != bool(self.actual.get(source, {}).get("present")))
        }
        self.failed(batch)
        return unfinished

    def cancel_queued(self):
        self.desired = self.present()

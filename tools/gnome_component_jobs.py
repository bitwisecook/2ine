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

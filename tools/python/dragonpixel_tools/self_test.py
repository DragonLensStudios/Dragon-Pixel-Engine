"""End-to-end editor automation proof launched with inherited capabilities."""

from __future__ import annotations

import json
import os
import uuid

from .client import AutomationClient, AutomationError


def required_environment(name: str) -> str:
    value = os.environ.get(name, "").strip()
    if not value:
        raise RuntimeError(f"{name} was not inherited")
    return value


def main() -> int:
    endpoint = required_environment("DPE_AUTOMATION_ENDPOINT")
    token = required_environment("DPE_AUTOMATION_TOKEN")
    entity_id = required_environment("DPE_AUTOMATION_ENTITY_ID")
    uuid.UUID(entity_id)
    correlation_id = str(uuid.uuid4())
    base = {"actor": "dragonpixel-tools-self-test", "correlationId": correlation_id}

    with AutomationClient(endpoint, token) as client:
        handshake = client.request("handshake", {**base, "protocolVersion": 1})
        if handshake.get("protocolVersion") != 1 or "execute-approved-command" not in handshake.get("capabilities", []):
            raise RuntimeError("Automation capability negotiation failed")

        before = client.request("inspectScene", {**base, "entityId": entity_id})
        original_name = before["entity"]["name"]
        dry_run = client.request("applyCommand", {
            **base,
            "dryRun": True,
            "command": {"type": "renameEntity", "entityId": entity_id, "name": "Automation Dry Run Entity"},
        })
        if dry_run.get("applied") or not dry_run.get("wouldApply"):
            raise RuntimeError("Automation dry-run result was invalid")
        after_dry_run = client.request("inspectScene", {**base, "entityId": entity_id})
        if after_dry_run["entity"]["name"] != original_name:
            raise RuntimeError("Automation dry-run mutated authoritative state")

        try:
            client.request("applyCommand", {
                **base,
                "command": {"type": "renameEntity", "entityId": entity_id, "name": ""},
            })
            raise RuntimeError("Invalid automation command was accepted")
        except AutomationError as error:
            if error.code != -32602:
                raise

        applied = client.request("applyCommand", {
            **base,
            "command": {"type": "renameEntity", "entityId": entity_id, "name": "Automation Applied Entity"},
        })
        if not applied.get("applied"):
            raise RuntimeError("Validated automation command was not applied")
        after = client.request("inspectScene", {**base, "entityId": entity_id})
        if after["entity"]["name"] != "Automation Applied Entity":
            raise RuntimeError("Applied automation command was not observable")
        cancelled = client.request("cancel", {**base, "requestId": 999})
        if not cancelled.get("cancelled"):
            raise RuntimeError("Automation cancellation contract failed")

    print(json.dumps({
        "succeeded": True,
        "entityId": entity_id,
        "originalName": original_name,
        "appliedName": "Automation Applied Entity",
        "correlationId": correlation_id,
    }, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

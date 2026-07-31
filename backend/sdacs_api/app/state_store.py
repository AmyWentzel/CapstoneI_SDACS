from threading import RLock

from .models import NodeState, TelemetryUpdate, utc_now_iso


class StateStore:
    def __init__(self, max_captures: int = 100, max_command_responses: int = 200) -> None:
        self._lock = RLock()
        self._nodes: dict[str, NodeState] = {}
        self._captures: list[TelemetryUpdate] = []
        self._command_responses: dict[str, TelemetryUpdate] = {}
        self._command_response_order: list[str] = []
        self._max_captures = max_captures
        self._max_command_responses = max_command_responses

    def update(self, telemetry: TelemetryUpdate) -> TelemetryUpdate:
        with self._lock:
            node = self._nodes.get(telemetry.node_id)
            if node is None:
                node = NodeState(node_id=telemetry.node_id)
                self._nodes[telemetry.node_id] = node

            now = telemetry.timestamp_iso or utc_now_iso()
            telemetry.timestamp_iso = now
            node.latest = telemetry
            node.last_seen_iso = now

            if telemetry.record_type == "features":
                node.latest_features = telemetry
            if telemetry.record_type in {
                "status",
                "heartbeat",
                "presence",
                "ota_status",
                "command_response",
                "capture_status",
            }:
                node.latest_status = telemetry
            if telemetry.record_type == "capture_complete":
                self._captures.insert(0, telemetry)
                del self._captures[self._max_captures :]
            if telemetry.record_type == "command_response" and telemetry.request_id:
                request_id = telemetry.request_id
                self._command_responses[request_id] = telemetry
                if request_id in self._command_response_order:
                    self._command_response_order.remove(request_id)
                self._command_response_order.append(request_id)
                while len(self._command_response_order) > self._max_command_responses:
                    stale = self._command_response_order.pop(0)
                    self._command_responses.pop(stale, None)

            return telemetry

    def list_nodes(self) -> list[NodeState]:
        with self._lock:
            return list(self._nodes.values())

    def get_node(self, node_id: str) -> NodeState | None:
        with self._lock:
            return self._nodes.get(node_id)

    def get_command_response(self, request_id: str) -> TelemetryUpdate | None:
        with self._lock:
            return self._command_responses.get(request_id)

    def list_captures(self) -> list[TelemetryUpdate]:
        with self._lock:
            return list(self._captures)

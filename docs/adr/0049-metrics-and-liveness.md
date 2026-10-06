# Metrics and Liveness: Prometheus Pull from augustad, kube-prometheus-stack via Flux

`augustad` serves Prometheus metrics and a liveness check over HTTP. A
kube-prometheus-stack in the k3s cluster scrapes it, keeps the series and draws
them in Grafana. The metrics answer two questions: is the server healthy (its
tick, its Lobby and Match, its Sessions), and how healthy is each client's
transport connection (its Connection health). Both are measured on the server
alone.

**The endpoint.** `augustad` links prometheus-cpp's `core`, which keeps the
metrics and formats them, and serves them with Boost.Beast (ADR-0035) on one
TCP port, `9464` by default and set in `augustad.yaml` (ADR-0034). Two paths:

- `/metrics`: the Prometheus text exposition of the catalogue below.
- `/livez`: `200` while the Simulation thread has finished a tick in the last 5
  seconds, `503` otherwise. The chart uses it as the Deployment's
  `livenessProbe`, so Kubernetes restarts a server whose tick loop has hung.
  There is no readiness probe: a single server replica takes no balanced
  traffic.

The endpoint is always on, both in the cluster and in a local run. The chart
exposes it through its own `ClusterIP` Service (never a NodePort) with a
`ServiceMonitor` that Prometheus scrapes every 15 seconds.

**Threads.** The HTTP server runs on its own thread, the server's third
(ADR-0005): one `io_context` that accepts each connection and answers its one
request. It only reads. The Simulation and Network I/O
threads write each metric in place: a counter or histogram is a lock-free
atomic, and a value read together with others is published whole. The
heartbeat (ADR-0029) and the metrics count the same events from the same
counters, so the log line and the series cannot disagree. The Network I/O
thread samples every connection's transport status
(`GetConnectionRealTimeStatus`) once a heartbeat interval (1 second). The
endpoint thread is not a supervised worker: if it fails, the failure is
logged and the tick loop keeps running, because an HTTP request must never
stop it. `/livez` goes down with the endpoint, though, so in the cluster the
liveness probe then restarts the pod: a dead endpoint ends the Match in
progress, as a hung tick does.

**Names.** Every metric is named `augustad_<what>_<unit>`. Units are base
units (`_seconds`, `_bytes`, `_ratio`, `_hertz`), and counters end in `_total`.
Each label takes its values from a closed set: an enum in the code, never free
text, an address or a Character's name. The domain words are CONTEXT.md's.

## Catalogue

| Family | Metric | Type | Labels |
|---|---|---|---|
| Tick | `augustad_tick_duration_seconds` | histogram, buckets 1, 2, 4, 8, 12, 16.7, 20, 33, 50, 100 ms | |
| | `augustad_ticks_total` | counter | |
| | `augustad_ticks_late_total` (started more than 1 ms after its deadline) | counter | |
| | `augustad_tick_overruns_total` (work took longer than a tick) | counter | |
| | `augustad_tick_resyncs_total` (the loop resynchronised to now, ADR-0005) | counter | |
| | `augustad_tick_rate_hertz` (configured) | gauge | |
| Lobby and Match | `augustad_lobby_players` | gauge | |
| | `augustad_match_in_progress` | gauge, 0 or 1 | |
| | `augustad_match_players_alive` | gauge | |
| | `augustad_matches_started_total` | counter | |
| | `augustad_matches_ended_total` | counter | `outcome` = `winner`, `draw`, `abandoned` |
| | `augustad_match_duration_seconds` | histogram | |
| Sessions | `augustad_sessions` | gauge | |
| | `augustad_joins_total` | counter | `result` = `admitted`, `refused`; `reason` |
| | `augustad_disconnects_total` | counter | `reason`; `phase` = `lobby`, `match` |
| Misbehaviour | `augustad_misbehaviour_total` | counter | `kind` |
| Network | `augustad_network_sent_bytes_total`, `augustad_network_received_bytes_total` | counter | |
| | `augustad_messages_sent_total`, `augustad_messages_received_total` | counter | `type` (ADR-0038's message types) |
| | `augustad_authoritative_state_update_bytes` | histogram | |
| | `augustad_commands_received_total` | counter | |
| | `augustad_commands_discarded_total` | counter | `reason` |
| Connection health | `augustad_connection_rtt_seconds` | histogram, and a gauge by `session_id` | |
| | `augustad_connection_quality_ratio` | histogram, and a gauge by `session_id` | `direction` = `local`, `remote` |
| | `augustad_connection_jitter_seconds` | histogram, and a gauge by `session_id` | |
| | `augustad_connection_in_bytes_per_second`, `augustad_connection_out_bytes_per_second` | gauge | `session_id` |
| | `augustad_connection_pending_bytes` | gauge | `session_id` |
| Combat | `augustad_shots_total` | counter | |
| | `augustad_hit_confirmations_total` | counter | `body_part` = `head`, `torso`, `limb` |
| | `augustad_shooters_delay_seconds` | histogram | |
| | `augustad_shooters_delay_capped_total` (at the 250 ms cap, ADR-0044) | counter | |
| Process | `augustad_build_info` = 1 | gauge | `version`, `commit` |
| | `augustad_start_time_seconds` | gauge | |

The values of `reason` and `kind` are the closed sets the server already
decides with: admission's refusals, the transport's end reasons, the
misbehaviour kinds and the command queue's discards.

`augustad_build_info`'s `commit` is the commit the server image was built from,
which the image's runtime stage sets as the `AUGUSTA_COMMIT` environment
variable: compiled in, it would change the build step's input on every commit
and defeat the image's build cache. A local build reports `unknown`.

Connection health is kept two ways. Histograms over every connection show its
trend and drive the alert rules. Gauges labelled by Session ID show the one
player whose connection is bad. A Session's gauges are removed when the Session
ends. Session IDs are never reused, so every Session leaves its own series
behind, but no more than the Player count are live at once, which a 15-day
retention easily holds.

CPU, memory and restarts are not `augustad`'s metrics: the stack's kubelet and
cAdvisor scrape already has them per pod.

**The stack.** kube-prometheus-stack (Prometheus Operator, Prometheus,
Alertmanager, Grafana, node-exporter, kube-state-metrics) is installed by a Flux
`HelmRelease` in its own `monitoring` namespace, under an `infrastructure`
Kustomization in `clusters/onprem/`. `apps` depends on it, so the
`ServiceMonitor` and `PrometheusRule` CRDs exist before the `augustad` chart
uses them. Prometheus keeps 15 days on a `local-path` PVC of about 10 GB.
Grafana is reached on a fixed NodePort on the LAN, the same way as the game
port. Both environments, `develop` and `staging`, are scraped the same way, told
apart by their namespace label.

Dashboards are code: JSON in ConfigMaps that Grafana's sidecar loads, kept in
the repository next to the chart. There are two, "Server" (tick, Lobby and
Match, Sessions, misbehaviour, network, combat) and "Connection health" (the
histograms and one line per Session). Alert rules are `PrometheusRule`s in the
`augustad` chart (server down, tick overruns sustained, packet loss high). They
route to no receiver yet, so they show only in Grafana and Alertmanager.

## Considered Options

- **OpenTelemetry (OTLP push to a collector)**: rejected. It needs a collector
  between `augustad` and Prometheus and a heavier C++ SDK. Its strength,
  traces and logs alongside metrics, is not what is asked for: the logs are
  ADR-0029's, and the profiler has the timing.
- **Hand-rolled exposition** (our own metric types and text format):
  rejected. Histograms, label sets and the exposition format are exactly what
  prometheus-cpp already does and tests.
- **prometheus-cpp's `pull` (its `Exposer`, on civetweb)**: rejected. The
  `Exposer` serves only metrics, always `200`, so `/livez` could not answer
  `503`, and serving `/livez` beside it would mean a second HTTP server.
  Serving both paths with Boost.Beast costs one small handler and keeps the
  HTTP server in a library family the project already uses.
- **Minimal charts** (the community `prometheus` chart with annotation scraping,
  and `grafana`): rejected. They are lighter, but have no `ServiceMonitor` or
  `PrometheusRule` CRDs, so the scrape and the alerts could not live in the
  `augustad` chart, versioned with the code they measure.
- **VictoriaMetrics**: rejected. It is lighter and speaks PromQL, but it is off
  the best-trodden path for a stack this project does not want to study, and
  the node has the memory for Prometheus.
- **Connection health reported by the client**: deferred. Only the client knows
  how large its Reconciliation corrections are, or when it had fewer than two
  Authoritative State updates to interpolate between. Reporting that needs a
  new protocol message, and the server must not trust it. The same metrics on
  `augusta-swarm`'s Scripted players raise no trust question, but a
  short-lived process outside the cluster needs a Pushgateway or a scrape of
  the developer's machine. Both wait for a phase 2.
- **Connection health aggregated only**: rejected. It cannot say which player's
  connection is bad.
- **`/healthz`**: rejected for `/livez`. The check answers liveness only, and
  `/livez` is Kubernetes' own current name for that, next to which a `/readyz`
  would fit. It also keeps "health" from meaning a third thing beside a
  player's health and Connection health.

## Consequences

- The server runs three threads, not two (ADR-0005).
- A hung tick loop is no longer only visible to whoever is watching: the
  liveness probe restarts it. ENGINEERING.md's "no watchdog for v1" no longer
  holds.
- Instrumenting a new event means a new counter in the catalogue above,
  counted where the heartbeat counts it.
- Alerts have rules but no receiver. Choosing one (mail, a webhook, ntfy) is
  left for when the server runs unattended for real.
- The cluster runs a monitoring stack of its own, about 1 to 1.5 GB of memory
  on the node.

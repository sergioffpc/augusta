# Log Aggregation: Loki and Alloy beside kube-prometheus-stack

`augustad` writes its log to the console (ADR-0027, ADR-0029), so in the k3s
cluster each line lives only in its container's log on the node: `kubectl logs`
reads it while the pod exists, and a restarted or rescheduled server takes its
previous lines with it. A metric's spike in Grafana (ADR-0049) has no lines to
explain it.

Loki keeps the cluster's logs and Grafana queries them, beside the metrics. Both
are Flux `HelmRelease`s in `clusters/onprem/infrastructure/monitoring.yaml`,
next to kube-prometheus-stack and in its `monitoring` namespace:

- **Loki** (the `grafana/loki` chart) runs as a single process (`SingleBinary`),
  its chunks and index on the node's disk (`local-path`, 10 Gi). It keeps 15
  days, as Prometheus keeps the series. Its gateway, caches, canary and tests
  are off: they spread or speed up a Loki of many processes, and the cluster is
  one node.
- **Alloy** (the `grafana/alloy` chart), one replica, tails every container's
  log through the Kubernetes API (`loki.source.kubernetes`) and pushes it to
  Loki, labelled `namespace`, `pod`, `container` and `app`.
- **Grafana** gets Loki as a second data source (uid `loki`).

The lines need no new format: ADR-0029's logfmt bodies parse with LogQL's
`logfmt` stage, so `{namespace="develop"} | logfmt | event="heartbeat"` works as
written. The level is the line's second field, not a logfmt pair, so it is
filtered with a line filter (`|= " WARN "`).

## Considered Options

- **Promtail**: rejected. Grafana deprecated it in favour of Alloy, and its
  chart is no longer released.
- **Alloy as a DaemonSet reading `/var/log/pods`**: rejected for now. It is the
  usual shape for a cluster of many nodes, but needs host mounts; on one node a
  single replica reading through the API is the same coverage with less access.
  It becomes the right shape when a second node joins.
- **Loki on object storage (MinIO)**: rejected. Object storage is what lets Loki
  run as many processes; one process on one node writes to its disk.
- **OpenTelemetry logs from `augustad`**: rejected for ADR-0049's reasons. The
  console is already the source, and Alloy reads it without code changes.
- **Elasticsearch/OpenSearch**: rejected. Full-text indexing costs memory the
  node spends on the game servers, and the logs are read by label and time.

## Consequences

- A server's log outlives its pod, for 15 days.
- The monitoring stack grows by Loki and Alloy, a few hundred MB of memory and
  10 Gi of the node's disk.
- Loki has no authentication: like Grafana, it is reached only from the cluster.
- Logs that ADR-0029 forbids (credentials, tokens) would now be kept, not only
  scrolled past; the policy matters more, not less.

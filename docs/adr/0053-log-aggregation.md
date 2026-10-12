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

**Security logs (ADR-0057).** Three more sources, each labelled with its `job`:

- **`node/auth`, `node/auditd`, `node/kube-audit`**: the Node's journal's auth
  and authpriv facilities (sshd, `sudo`, `su`, labelled with the `identifier`
  that logged each line; Debian 13 keeps no `auth.log`), auditd's log, and the
  API server's audit log, one JSON event a line. A second Alloy release,
  `alloy-node`, reads them as a DaemonSet in `monitoring-node`, from the Node's
  files mounted read-only, as root with no capability: they are root's alone. It
  keeps where it has read up to on the Node (`/var/lib/alloy-node`), so a
  restart neither repeats nor skips. Each line takes its own timestamp. An audit
  event loses its `requestObject` and `responseObject` on the way: they hold
  Secrets' contents, which stay in the Node's copy.
- **`kubernetes/events`**: every Kubernetes event, as a logfmt line (`name`,
  `kind`, `reason`, `type`, `msg`), read by the first Alloy through the API.
  Kyverno writes one for every failed policy result, and Flux's
  source-controller one for each failed signature check.
- **`kube-bench`**: a daily CronJob's report, an ordinary pod log
  (`{namespace="monitoring-node", app="kube-bench"}`).

Grafana's "Security" dashboard shows each, a panel a source, as code beside the
cluster's manifests: its anonymous Viewers have no Explore (ADR-0057). Loki's
ruler evaluates LogQL alerting rules, ConfigMaps labelled `loki_rule` in
`monitoring` that its sidecar loads, and sends what fires to the stack's
Alertmanager, beside Prometheus's alerts (ADR-0049).

## Considered Options

- **Promtail**: rejected. Grafana deprecated it in favour of Alloy, and its
  chart is no longer released.
- **Alloy as a DaemonSet reading `/var/log/pods`**: rejected for now. It is the
  usual shape for a cluster of many nodes, but needs host mounts; on one node a
  single replica reading through the API is the same coverage with less access.
  It becomes the right shape when a second node joins.
- **One Alloy for both the pods and the Node's files**: rejected. The Node's
  files need host mounts and root, which would take every pod's log, and the
  Kubernetes API access that reads them, into a `privileged` namespace.
- **Policy Reporter for Kyverno's reports**: rejected. It reads the
  PolicyReports themselves, but is one more component and image to trust, for
  failures Kyverno already writes as events.
- **auditd's and the API server's logs through the journal or syslog**:
  rejected. Neither writes there as configured; reading their files is the
  shorter path.
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
  It holds who did what to the cluster and the Node, though no Secret's
  contents.
- A Loki rule that does not parse is skipped, not refused: CI parses them
  (`lokitool rules lint`).
- Logs that ADR-0029 forbids (credentials, tokens) would now be kept, not only
  scrolled past; the policy matters more, not less.

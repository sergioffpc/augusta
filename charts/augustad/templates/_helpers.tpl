{{- define "augustad.name" -}}
{{- .Chart.Name -}}
{{- end -}}

{{- define "augustad.fullname" -}}
{{- .Release.Name -}}
{{- end -}}

{{- define "augustad.labels" -}}
app.kubernetes.io/name: {{ include "augustad.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{/* Flux's helm-controller synthesizes versions like "0.1.0+<sha>" for
     git-sourced charts, as CI versions the charts it publishes - "+" isn't valid in a label value, so sanitize it. */}}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end -}}

{{- define "augustad.selectorLabels" -}}
app.kubernetes.io/name: {{ include "augustad.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end -}}

{{/* The per-server templates below take a dict of the chart's root context
     ("root") and the server's scenario ("scenario"). */}}

{{/* A server's resources' name, augustad-<scenario>, after checking the
     scenario can name it: a DNS label short enough for the metrics Service's
     name to stay one (augusta-publish refuses the same names). */}}
{{- define "augustad.serverName" -}}
{{- if not (regexMatch "^[a-z0-9]([a-z0-9-]{0,44}[a-z0-9])?$" .scenario) -}}
{{- fail (printf "servers.%s: a scenario's name must be lowercase letters, digits and '-', starting and ending with a letter or digit, at most 46 characters" .scenario) -}}
{{- end -}}
{{- printf "%s-%s" (include "augustad.fullname" .root) .scenario -}}
{{- end -}}

{{/* Tells one server's pods and Services from another's. The scenario label
     also becomes every metric's scenario label (servicemonitor.yaml). */}}
{{- define "augustad.serverSelectorLabels" -}}
{{ include "augustad.selectorLabels" .root }}
scenario: {{ .scenario }}
{{- end -}}

{{/* A server's packVersion, after checking it is one augusta-publish names:
     12 hex characters, so its folder cannot reach outside the scenario's. */}}
{{- define "augustad.packVersion" -}}
{{- $version := (index .root.Values.servers .scenario).packVersion | default "" | toString -}}
{{- if not (regexMatch "^[0-9a-f]{12}$" $version) -}}
{{- fail (printf "servers.%s.packVersion must name the pack version augusta-publish printed, quoted (12 hex characters), not %q" .scenario $version) -}}
{{- end -}}
{{- $version -}}
{{- end -}}

{{/* The Node's folder holding a server's pack. */}}
{{- define "augustad.packFolder" -}}
{{- printf "%s/%s/%s" .root.Values.assetPacks.hostPath .scenario (include "augustad.packVersion" .) -}}
{{- end -}}

{{/* The claim on a server's pack volume, named by its version: a claim's
     volumeName cannot change, so a new version is a new claim (and volume),
     and reverting names the previous one again. */}}
{{- define "augustad.packClaim" -}}
{{- printf "%s-pack-%s" (include "augustad.serverName" .) (include "augustad.packVersion" .) -}}
{{- end -}}

{{/* The claim on a server's capture volume. */}}
{{- define "augustad.capturesClaim" -}}
{{- printf "%s-captures" (include "augustad.serverName" .) -}}
{{- end -}}

{{/* The PersistentVolume a claim ("claim") is bound to: PersistentVolumes
     are cluster-scoped, so its name carries the claim's namespace, and
     Environments sharing the Node never collide on one. */}}
{{- define "augustad.volumeForClaim" -}}
{{- printf "%s-%s" .root.Release.Namespace .claim -}}
{{- end -}}

{{/* A server's node port, empty for Kubernetes to assign one, after checking
     it is in service.nodePortRange when the environment sets one. */}}
{{- define "augustad.nodePort" -}}
{{- $port := (index .root.Values.servers .scenario).nodePort -}}
{{- with .root.Values.service.nodePortRange -}}
{{- if not (and $port (ge (int $port) (int .first)) (le (int $port) (int .last))) -}}
{{- fail (printf "servers.%s.nodePort must be pinned in this environment's service.nodePortRange, %d-%d" $.scenario (int .first) (int .last)) -}}
{{- end -}}
{{- end -}}
{{- $port | default "" -}}
{{- end -}}

{{/* Where the capture volume is mounted in every server's container. */}}
{{- define "augustad.capturesMount" -}}
/srv/augusta/captures
{{- end -}}

{{/* A server's own folder of the capture volume: <namespace>/<scenario>, so
     no two servers on the node, in one environment or two, share one. */}}
{{- define "augustad.captureFolder" -}}
{{- printf "%s/%s/%s" (include "augustad.capturesMount" .) .root.Release.Namespace .scenario -}}
{{- end -}}

{{/* A server's startup settings (ADR-0034), as config/augustad.example.yaml
     documents them. The pack and its key come from the server's own folder
     of the asset-pack volume, mounted at the config's base_dir. */}}
{{- define "augustad.config" -}}
base_dir: /srv/augusta/pack
content:
  pack: server.apak
  public_key: signing.pub
simulation:
  tick_rate_hz: {{ .root.Values.server.tickRateHz }}
  {{- if .root.Values.captures.hostPath }}
  capture: {{ include "augustad.captureFolder" . }}
  {{- end }}
{{- with (.root.Values.captures).retention }}
{{- if or .maxFiles .maxMiB }}
  capture_retention:
{{- with .maxFiles }}
    max_files: {{ . }}
{{- end }}
{{- with .maxMiB }}
    max_mib: {{ . }}
{{- end }}
{{- end }}
{{- end }}
network:
  listen_address: 0.0.0.0:{{ .root.Values.service.port }}
metrics:
  port: {{ .root.Values.metrics.port }}
logging:
  level: {{ .root.Values.server.logLevel }}
{{- end -}}

{{/* The image tag to run: image.tag if set; else, for a chart versioned
     <version>+<commit> (by Flux from Git, or by CI publishing it), the
     sha-<commit> tag CI pushed for that same commit; else the chart's
     appVersion. */}}
{{- define "augustad.imageTag" -}}
{{- if .Values.image.tag -}}
{{- .Values.image.tag -}}
{{- else if contains "+" .Chart.Version -}}
{{- printf "sha-%s" (splitList "+" .Chart.Version | last) -}}
{{- else -}}
{{- .Chart.AppVersion -}}
{{- end -}}
{{- end -}}

{{/* A server's metrics Service's name, which the alert rules match its
     targets by. */}}
{{- define "augustad.metricsName" -}}
{{- printf "%s-metrics" (include "augustad.serverName" .) -}}
{{- end -}}

{{/* Tells the metrics Service, which the ServiceMonitor selects, from the
     game one. */}}
{{- define "augustad.metricsLabels" -}}
app.kubernetes.io/component: metrics
{{- end -}}

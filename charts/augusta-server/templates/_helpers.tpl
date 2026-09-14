{{- define "augusta-server.name" -}}
{{- .Chart.Name -}}
{{- end -}}

{{- define "augusta-server.fullname" -}}
{{- .Release.Name -}}
{{- end -}}

{{- define "augusta-server.labels" -}}
app.kubernetes.io/name: {{ include "augusta-server.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{/* Flux's helm-controller synthesizes versions like "0.1.0+<sha>" for
     git-sourced charts - "+" isn't valid in a label value, so sanitize it. */}}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end -}}

{{- define "augusta-server.selectorLabels" -}}
app.kubernetes.io/name: {{ include "augusta-server.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end -}}

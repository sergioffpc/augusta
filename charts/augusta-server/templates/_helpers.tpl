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
helm.sh/chart: {{ .Chart.Name }}-{{ .Chart.Version }}
{{- end -}}

{{- define "augusta-server.selectorLabels" -}}
app.kubernetes.io/name: {{ include "augusta-server.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end -}}

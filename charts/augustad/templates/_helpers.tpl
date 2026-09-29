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
     git-sourced charts - "+" isn't valid in a label value, so sanitize it. */}}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end -}}

{{- define "augustad.selectorLabels" -}}
app.kubernetes.io/name: {{ include "augustad.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end -}}

{{/* The image tag to run: image.tag if set; else, for a chart Flux versioned
     <version>+<commit>, the sha-<commit> tag CI pushed for that same commit;
     else the chart's appVersion. */}}
{{- define "augustad.imageTag" -}}
{{- if .Values.image.tag -}}
{{- .Values.image.tag -}}
{{- else if contains "+" .Chart.Version -}}
{{- printf "sha-%s" (splitList "+" .Chart.Version | last) -}}
{{- else -}}
{{- .Chart.AppVersion -}}
{{- end -}}
{{- end -}}

#include <atomic>
#include <cstddef>

#include "augusta/capture_retention.h"
#include "augusta/capturer.h"
#include "augusta/host_metrics.h"

namespace augusta::host {

void CaptureMetrics::SetRetention(const CaptureRetention& retention) {
  metrics_.capture_retention_max_files = retention.max_files;
  metrics_.capture_retention_max_bytes = retention.max_bytes;
}

void CaptureMetrics::OnState(CaptureState state) { metrics_.capture_state.store(state, std::memory_order_relaxed); }

void CaptureMetrics::OnStarted() { metrics_.captures_started.Increment(); }

void CaptureMetrics::OnCompleted() { metrics_.captures_completed.Increment(); }

void CaptureMetrics::OnStopped(CaptureStop stop) { metrics_.capture_stops[stop].Increment(); }

void CaptureMetrics::OnWritten(std::size_t bytes) { metrics_.capture_written_bytes.Increment(bytes); }

void CaptureMetrics::OnQueued(std::size_t records) { metrics_.capture_queue_records.Set(static_cast<double>(records)); }

void CaptureMetrics::OnDirectory(CaptureDirectoryUsage usage) {
  metrics_.capture_directory_files.Set(static_cast<double>(usage.files));
  metrics_.capture_directory_bytes.Set(static_cast<double>(usage.bytes));
}

void CaptureMetrics::OnRetentionDeleted() { metrics_.capture_retention_deleted.Increment(); }

void CaptureMetrics::OnRetentionDeleteFailed() { metrics_.capture_retention_failures.Increment(); }

void CaptureMetrics::OnHealth(CaptureHealth health) {
  metrics_.capture_health.store(health, std::memory_order_relaxed);
}

}  // namespace augusta::host

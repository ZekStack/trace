#include "internal/TraceImpl.h"

void Trace::onFlush(TraceFlushCallback callback) {
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->onFlush = callback;
	}
}

void Trace::onLog(TraceLogCallback callback) {
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->onLog = callback;
	}
}

void Trace::setStream(Print *stream) {
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->stream = stream;
	}
}

Print *Trace::getStream() {
	TraceLock lock(_impl->mutex);
	if (!lock) {
		return nullptr;
	}
	return _impl->stream;
}

TraceResult Trace::attachTempo(Tempo &tempo, const TraceTempoConfig &config) {
	if (config.format == TraceTimeFormat::Custom && config.formatter == nullptr) {
		return TraceResult::failure(
		    TraceStatus::InvalidArgument,
		    "custom tempo formatter is required"
		);
	}
	TraceLock lock(_impl->mutex);
	if (!lock) {
		return TraceResult::failure(TraceStatus::InternalError, "failed to lock trace");
	}
	_impl->tempo = &tempo;
	_impl->tempoConfig = config;
	return TraceResult::success("tempo attached");
}

void Trace::detachTempo() {
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->tempo = nullptr;
		_impl->tempoConfig = TraceTempoConfig();
	}
}

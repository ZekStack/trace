#include "internal/TraceImpl.h"

void Trace::onFlush(TraceFlushCallback callback) {
	if (!_impl) {
		return;
	}
	if (!callback) {
		TraceLock lock(_impl->mutex);
		if (lock) {
			_impl->onFlush.reset();
		}
		return;
	}

	Strata::Placement placement = Strata::Placement::PreferExternal;
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return;
		}
		placement = _impl->allocationPlacement();
	}
	auto holder = Strata::makeShared<TraceFlushCallback>(placement, std::move(callback));
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->onFlush = std::move(holder);
	}
}

void Trace::onLog(TraceLogCallback callback) {
	if (!_impl) {
		return;
	}
	if (!callback) {
		TraceLock lock(_impl->mutex);
		if (lock) {
			_impl->onLog.reset();
		}
		return;
	}

	Strata::Placement placement = Strata::Placement::PreferExternal;
	{
		TraceLock lock(_impl->mutex);
		if (!lock) {
			return;
		}
		placement = _impl->realtimeAllocationPlacement();
	}
	auto holder = Strata::makeShared<TraceLogCallback>(placement, std::move(callback));
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->onLog = std::move(holder);
	}
}

void Trace::setStream(Print *stream) {
	if (!_impl) {
		return;
	}
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->stream = stream;
	}
}

Print *Trace::getStream() {
	if (!_impl) {
		return nullptr;
	}
	TraceLock lock(_impl->mutex);
	if (!lock) {
		return nullptr;
	}
	return _impl->stream;
}

TraceResult Trace::attachTempo(Tempo &tempo, const TraceTempoConfig &config) {
	if (!_impl) {
		return TraceResult::failure(TraceStatus::OutOfMemory, "failed to allocate trace state");
	}
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
	if (!_impl) {
		return;
	}
	TraceLock lock(_impl->mutex);
	if (lock) {
		_impl->tempo = nullptr;
		_impl->tempoConfig = TraceTempoConfig();
	}
}

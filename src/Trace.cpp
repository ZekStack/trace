#include "Trace.h"

#include "internal/TraceImpl.h"

TraceResult TraceResult::success(const char *message) {
	TraceResult result;
	result.result = true;
	result.status = TraceStatus::Ok;
	result.message = message != nullptr ? message : "ok";
	return result;
}

TraceResult TraceResult::failure(TraceStatus status, const char *message) {
	TraceResult result;
	result.result = false;
	result.status = status;
	result.message = message != nullptr ? message : "error";
	return result;
}

Trace::Trace()
    : _impl(Strata::makeUnique<TraceImpl>(Strata::Placement::Internal)) {
}

Trace::~Trace() {
	if (_impl) {
		(void)end(UINT32_MAX);
	}
}

const char *Trace::statusToString(TraceStatus status) const {
	switch (status) {
	case TraceStatus::Ok:
		return "Ok";
	case TraceStatus::NotInitialized:
		return "NotInitialized";
	case TraceStatus::AlreadyInitialized:
		return "AlreadyInitialized";
	case TraceStatus::InvalidArgument:
		return "InvalidArgument";
	case TraceStatus::OutOfMemory:
		return "OutOfMemory";
	case TraceStatus::TaskCreateFailed:
		return "TaskCreateFailed";
	case TraceStatus::Busy:
		return "Busy";
	case TraceStatus::Timeout:
		return "Timeout";
	case TraceStatus::FlushFailed:
		return "FlushFailed";
	case TraceStatus::InternalError:
		return "InternalError";
	default:
		return "Unknown";
	}
}

const char *Trace::levelToString(TraceLevel level) const {
	return TraceImpl::levelName(level);
}

const char *Trace::flushResultToString(TraceFlushResult result) const {
	switch (result) {
	case TraceFlushResult::Ok:
		return "Ok";
	case TraceFlushResult::Failed:
		return "Failed";
	case TraceFlushResult::Retry:
		return "Retry";
	default:
		return "Unknown";
	}
}

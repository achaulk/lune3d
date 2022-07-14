#pragma once

#include <assert.h>
#include <atomic>
#include <stdint.h>
#include <string_view>

#include "config.h"

#if LUNE_OPTICK
#include "third_party/optick/src/optick.h"
#else
#define OPTICK_THREAD(...)
#endif

#include <vector>

#define LUNE_ASSERT(condition)                  \
	do {                                        \
		if(!(condition))                        \
			::lune::details::panic(#condition); \
	} while(0)

#define LUNE_ASSERT_MSG(condition, msg)  \
	do {                                 \
		if(!(condition))                 \
			::lune::details::panic(msg); \
	} while(0)

#ifndef LUNE_IMPL_MODULE_NAME
#define LUNE_IMPL_MODULE_NAME call_LUNE_MODULE_macro_in_cpp
#endif

#define LUNE_LOG_RAW(condition, fmt, ...)                                                                  \
	do {                                                                                                   \
		if((condition)) {                                                                                  \
			static ::lune::details::LogPrint LUNE_CONCAT(lune_pr_, __LINE__) = {0, fmt "\n", __LINE__};    \
			::lune::details::Log(                                                                          \
			    &LUNE_CONCAT(lune_pr_, __LINE__), &::lune::details::LUNE_IMPL_MODULE_NAME, ##__VA_ARGS__); \
		}                                                                                                  \
	} while(0)

#define LOG(fmt, ...) LUNE_LOG_RAW(true, fmt, ##__VA_ARGS__)
#define LOGF(fmt, ...)                                                                                     \
	do {                                                                                                   \
		LUNE_LOG_RAW(::lune::details::LUNE_IMPL_MODULE_NAME.severity <= ::lune::FATAL, fmt, ##__VA_ARGS__);\
		::lune::details::PostFatalLog();                                                                   \
	} while(0)
#define LOGE(fmt, ...) LUNE_LOG_RAW(::lune::details::LUNE_IMPL_MODULE_NAME.severity <= ::lune::ERR, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LUNE_LOG_RAW(::lune::details::LUNE_IMPL_MODULE_NAME.severity <= ::lune::WARN, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LUNE_LOG_RAW(::lune::details::LUNE_IMPL_MODULE_NAME.severity <= ::lune::INFO, fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) \
	LUNE_LOG_RAW(::lune::details::LUNE_IMPL_MODULE_NAME.severity <= ::lune::DEBUG, fmt, ##__VA_ARGS__)
#define LOGV(fmt, ...) \
	LUNE_LOG_RAW(::lune::details::LUNE_IMPL_MODULE_NAME.severity <= ::lune::VERBOSE, fmt, ##__VA_ARGS__)

// Tracing is taken from the Chromium trace format
// A standalone Chromium can be downloaded at:
// https://www.chromium.org/getting-involved/download-chromium
// Duration events last for a certain amount of time
// Instant events
// Counter events
// Async events
// Object events

#if _MSC_VER
#pragma section(".traceev", read, write)
#define LUNE_PRETTY_FUNCTION __FUNCSIG__
#define TRACESECTION(x) __declspec(allocate(".traceev")) static x
#endif

#if __GNUC__
#define LUNE_PRETTY_FUNCTION __PRETTY_FUNCTION__
#define TRACESECTION(x) static x __attribute__((section(".tracev")))
#endif

namespace lune {

enum
{
	FATAL,
	ERR,
	WARN,
	INFO,
	DEBUG,
	VERBOSE,
};

static constexpr int kLuneDefaultLogSeverity = INFO;

namespace details {
struct LogModule
{
	LogModule(const char *file);

	int severity = kLuneDefaultLogSeverity;
	const char *file;
	bool tracing_allowed = true;
	const char *tracing_category = nullptr;
};

struct LogPrint
{
	uint32_t flags;
	const char *fmt;
	int line;
};

void Log(LogPrint *print, LogModule *m, ...);

LogModule *RegisterModule(const char *name);

struct LuneDurationEventInfo
{
	uint32_t enabled;
	const char *name;
	const char *category;
	const char *args;
};

struct DurationTrace
{
	DurationTrace(LuneDurationEventInfo *info);
	~DurationTrace();

	bool end;
	LuneDurationEventInfo *info;
};

extern std::atomic<uint32_t> CurrentTracingMode;

#if LUNE_NO_TRACING
#define TRACE_SCOPED(category, name)

#else
#define TRACE_SCOPED(category, name)                                                           \
	TRACESECTION(::lune::details::LuneDurationEventInfo LUNE_CONCAT(trace_evt_, __LINE__)) = { \
	    0xFEEFF00F, name, category};                                                           \
	::lune::details::DurationTrace LUNE_CONCAT(trace_evt_scoped_, __LINE__)(&LUNE_CONCAT(trace_evt_, __LINE__))

void TraceAsyncStart(LuneDurationEventInfo *info, uint64_t id);
void TraceAsyncEnd(LuneDurationEventInfo *info, uint64_t id);

void TraceObjStart(LuneDurationEventInfo *info, uint64_t id);
void TraceObjEnd(LuneDurationEventInfo *info, uint64_t id);

#define TRACE_ASYNC_START(category, name, id)                                                  \
	TRACESECTION(::lune::details::LuneDurationEventInfo LUNE_CONCAT(trace_evt_, __LINE__)) = { \
	    0xFEEFF00F, name, category};                                                           \
	::lune::details::TraceAsyncStart(&LUNE_CONCAT(trace_evt_, __LINE__), reinterpret_cast<uint64_t>(id));
#define TRACE_ASYNC_END(category, name, id)                                                    \
	TRACESECTION(::lune::details::LuneDurationEventInfo LUNE_CONCAT(trace_evt_, __LINE__)) = { \
	    0xFEEFF00F, name, category};                                                           \
	::lune::details::TraceAsyncEnd(&LUNE_CONCAT(trace_evt_, __LINE__), reinterpret_cast<uint64_t>(id));

#define TRACE_INSTANT_THREAD(category, name)
#define TRACE_INSTANT_PROCESS(category, name)
#endif

#define TRACE_FUNCTION(category) TRACE_SCOPED(category, LUNE_PRETTY_FUNCTION)

std::vector<LuneDurationEventInfo *> OsPopulateDurationEvents();

void BreakpointNow();
void PostFatalLog();
void LoggingAtExit();

void panic(const char *msg = nullptr);

void EarlyLogSetup(std::string_view log_file, std::string_view trace_file, bool enable_console_log);

} // namespace details

#if LUNE_NO_TRACING
class TracedObject
{
public:
	TracedObject(void *p) = default;
	~TracedObject() = default;
};
#else
template<typename T>
class TracedObject
{
public:
	TracedObject(void *p) : p(p)
	{
		details::TraceObjStart(&T::ObjInfo, (uint64_t)p);
	}
	~TracedObject()
	{
		details::TraceObjEnd(&T::ObjInfo, (uint64_t)p);
	}

private:
	void *p;
};
#endif

static constexpr uint32_t kTraceObjects = 2;

void SetDefaultLogLevel(int n);
void EnableTracing(uint32_t level);
void SetTracingLevel(const char *category, uint32_t levels);

void FlushAllTracing();

} // namespace lune

#define LUNE_MODULE()                          \
	namespace lune {                           \
	namespace details {                        \
	namespace {                                \
	LogModule LUNE_IMPL_MODULE_NAME(__FILE__); \
	}                                          \
	}                                          \
	}

#define LUNE_BP() ::lune::details::BreakpointNow()

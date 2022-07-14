#include "logging.h"
#include "sys/sync.h"
#include "sys/thread.h"
#include "clock.h"
#include "trace_file_sink.h"

#include "trace_collector.h"
#include "trace_processor.h"

#include <string.h>

#include <stdarg.h>

#include <map>
#include <string>

#if IS_WIN
#if LUNE_OPTICK
#ifdef _DEBUG
#pragma comment(lib, "..\\src\\third_party\\optick\\out\\Debug\\OptickCored.lib")
#else
#pragma comment(lib, "..\\src\\third_party\\optick\\out\\Release\\OptickCore.lib")
#endif
#endif
#endif

namespace lune {
namespace {
uint64_t GetLoggingTime()
{
	return ClkUpdateRealtime();
}
}

namespace details {
namespace printf_log {
CriticalSection cs;
void printf_log(LogPrint *p, LogModule *m, va_list varg)
{
	double now = (double)GetLoggingTime() * 0.000001;
	cs.lock();
	printf("%.6f (%s:%d): ", now, m->file, p->line);
	vprintf(p->fmt, varg);
	cs.unlock();
}
} // namespace printf_log

namespace file_log {
bool also_print = true;
std::unique_ptr<OutputStream> log_output;

void immediate_file_log(LogPrint *p, LogModule *m, va_list varg)
{
	char str[4000];
	double now = (double)GetLoggingTime() * 0.000001;
	int n = snprintf(str, sizeof(str), "%.6f (%s:%d): ", now, m->file, p->line);
	n += vsnprintf(str + n, sizeof(str) - n, p->fmt, varg);
	// puts also prints a newline but we include a newline normally
	if(also_print) {
		printf_log::cs.lock();
		fputs(str, stdout);
		printf_log::cs.unlock();
	}

	log_output->Write(str, n);
}
}

std::atomic<uint32_t> CurrentTracingMode;

namespace {
std::map<std::string, LogModule*> *modules;
void (*log_impl)(LogPrint *print, LogModule *m, va_list varg) = &printf_log::printf_log;

TraceAggregator global_trace_aggregator;
TraceProcessor *global_trace_processor;

TLS_DECL(TraceCollector) trace_writer(&global_trace_aggregator);
bool tracing_on = false;

} // namespace

void LoggingAtExit()
{
	CurrentTracingMode = 0;
	trace_writer.Flush();
	delete global_trace_processor;
}

uint32_t LogParsePrint(const LogPrint *print)
{
	return 1;
}

void Log(LogPrint *print, LogModule *m, ...)
{
	if(!print->flags) {
		// This may be happening simultaneously on multiple threads, however every thread is
		// guaranteed to produce the same value, so there's no need for synchronization.
		// Each thread will either see zero or the correct value
		print->flags = LogParsePrint(print);
	}
	va_list varg;
	va_start(varg, m);
	log_impl(print, m, varg);
	va_end(varg);
}

static const char *GetPrettyLogModule(const char *name)
{
	auto src = strstr(name, "src");
	if(src)
		return src + 4;
	return name;
}

LogModule::LogModule(const char *name) : file(GetPrettyLogModule(name))
{
	if(!modules)
		modules = new std::map<std::string, LogModule*>();
	(*modules)[name] = this;
}

DurationTrace::DurationTrace(LuneDurationEventInfo *info) : info(info)
{
	end = !!(info->enabled & CurrentTracingMode.load(std::memory_order_acquire));
	if(end)
		trace_writer.Begin(info, GetLoggingTime());
}

DurationTrace::~DurationTrace()
{
	if(end) {
		// Emit complete
		trace_writer.End(info, GetLoggingTime());
	}
}

void TraceAsyncStart(LuneDurationEventInfo *info, uint64_t id)
{
	if(info->enabled & CurrentTracingMode.load(std::memory_order_acquire))
		trace_writer.AsyncBegin(info, id, GetLoggingTime());
}

void TraceAsyncEnd(LuneDurationEventInfo *info, uint64_t id)
{
	if(info->enabled & CurrentTracingMode.load(std::memory_order_acquire))
		trace_writer.AsyncEnd(info, id, GetLoggingTime());
}

void TraceObjStart(LuneDurationEventInfo *info, uint64_t id)
{
	if(kTraceObjects & details::CurrentTracingMode.load(std::memory_order_acquire))
		details::trace_writer.ObjNew(info, id, GetLoggingTime());
}

void TraceObjEnd(LuneDurationEventInfo *info, uint64_t id)
{
	if(kTraceObjects & details::CurrentTracingMode.load(std::memory_order_acquire))
		details::trace_writer.ObjDel(info, id, GetLoggingTime());
}

std::vector<LuneDurationEventInfo *> AllKnownDurationEvents = OsPopulateDurationEvents();

TraceProcessorSink *current_trace_sink = nullptr;

TraceProcessorSink *CreateTraceSink(const std::string_view &path)
{
	auto file = sys_vfs.OpenFile(path, file_flags::kAppendOnly, OpenMode::CreateOrTruncate);
	if(!file)
		return nullptr;
	auto op = AsyncOp::OpInto("[\n", 2);
	op->offset = kAppendOffset;
	file.file()->BeginWrite(op);
	return new TraceFileSink(file.file());
}

void EnableTracingInternal(uint32_t level)
{
	CurrentTracingMode = level;
}

void PostFatalLog()
{
	BreakpointNow();
}

void BreakpointNow()
{
#if IS_WIN
	__debugbreak();
#elif IS_LINUX
	asm volatile("int3");
#endif
}

void panic(const char *msg)
{
	BreakpointNow();
}

void EarlyLogSetup(std::string_view log_file, std::string_view trace_file, bool enable_console_log)
{
	file_log::also_print = enable_console_log;
	if(!log_file.empty()) {
		auto f = sys_vfs.OpenFile(log_file, file_flags::kAppendOnly, lune::OpenMode::CreateOrTruncate);
		if(f) {
			log_impl = &file_log::immediate_file_log;
			file_log::log_output = f.CreateOutputStream();
		}
	}
	if(!trace_file.empty()) {
		if(!current_trace_sink) {
			current_trace_sink = CreateTraceSink(trace_file);
			if(!current_trace_sink)
				return;
		}
		if(!global_trace_processor)
			global_trace_processor = new TraceProcessor(&global_trace_aggregator);
		global_trace_aggregator.SetTraceSink(global_trace_processor);
		global_trace_processor->SetSink(current_trace_sink);
	}
}

} // namespace details

void EnableTracing(uint32_t level)
{
	details::EnableTracingInternal(level);
}

void SetTracingLevel(const char *category, uint32_t levels)
{
	for(auto e : details::AllKnownDurationEvents)
		if(!category || category[0] == '*' || !strcmp(e->category, category))
			e->enabled = levels;
}

void SetDefaultLogLevel(int n) {}

} // namespace lune

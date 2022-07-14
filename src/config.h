#pragma once

#include <stdint.h>
#include <stddef.h>

#if _WIN32
#define IS_WIN 1
#endif

#if __linux__
#define IS_LINUX 1
#endif

#ifndef _DEBUG
#define LUNE_DEBUG 1
#endif

#ifndef LUNE_DEBUG
#define LUNE_DEBUG 1
#endif


// Standard stuff
#define LUNE_CONCAT2(x, y) x ## y
#define LUNE_CONCAT(x, y) LUNE_CONCAT2(x, y)


#if !IS_WIN
#define CRITICAL_SECTION_IS_STDMUTEX 1
#define CONDVAR_IS_STDCONDVAR 1
#endif



#ifdef LUNE_APPCONFIG
#include LUNE_APPCONFIG
#endif

#define LUNE_GFX_DEBUG 1

#define LUNE_MEM_DEBUG 1

#define LUNE_BUILTIN_DEBUG 1
#define LUNE_USE_IMGUI 1

#define LUNE_OPTICK 1

#define LUNE_SHADER_COMPILER 1


#if IS_WIN
// Decls without no_tls_guard try dynamic initialization
#define TLS_DECL(t) [[msvc::no_tls_guard]] thread_local t
#endif

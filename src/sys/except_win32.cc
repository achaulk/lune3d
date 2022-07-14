#include "except.h"

#include <Windows.h>

namespace lune {
namespace sys {
namespace {

int OnExcept(DWORD code, LPEXCEPTION_POINTERS except)
{
	return EXCEPTION_CONTINUE_SEARCH;
}

}

void TryCatch(const std::function<void()> &fn)
{
	__try {
		fn();
	} __except(OnExcept(GetExceptionCode(), GetExceptionInformation())) {
	}
}


}
}

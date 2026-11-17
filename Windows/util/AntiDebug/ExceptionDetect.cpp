#include "ExceptionDetect.h"
#include <spdlog/spdlog.h>
namespace zzj::AntiDebug
{
BOOL bIsBeinDbg = TRUE;

LONG WINAPI UnhandledExcepFilter(PEXCEPTION_POINTERS pExcepPointers)
{
    // If a debugger is present, then this function will not be reached.
    bIsBeinDbg = FALSE;
    return EXCEPTION_CONTINUE_EXECUTION;
}

BOOL UnhandledExcepFilterTest()
{
    LPTOP_LEVEL_EXCEPTION_FILTER Top = SetUnhandledExceptionFilter(UnhandledExcepFilter);
    RaiseException(EXCEPTION_FLT_DIVIDE_BY_ZERO, 0, 0, NULL);
    SetUnhandledExceptionFilter(Top);
    return bIsBeinDbg;
}
BOOL HardwareBreakpoints()
{
    BOOL bResult = FALSE;

    // This structure is key to the function and is the
    // medium for detection and removal
    PCONTEXT ctx = PCONTEXT(VirtualAlloc(NULL, sizeof(CONTEXT), MEM_COMMIT, PAGE_READWRITE));

    if (ctx)
    {

        SecureZeroMemory(ctx, sizeof(CONTEXT));

        // The CONTEXT structure is an in/out parameter therefore we have
        // to set the flags so Get/SetThreadContext knows what to set or get.
        ctx->ContextFlags = CONTEXT_DEBUG_REGISTERS;

        // Get the registers
        if (GetThreadContext(GetCurrentThread(), ctx))
        {

            // Now we can check for hardware breakpoints, its not
            // necessary to check Dr6 and Dr7, however feel free to
            if (ctx->Dr0 != 0 || ctx->Dr1 != 0 || ctx->Dr2 != 0 || ctx->Dr3 != 0)
                bResult = TRUE;
        }

        VirtualFree(ctx, 0, MEM_RELEASE);
    }

    return bResult;
}
/*
The Interrupt_0x2d function will check to see if a debugger is attached to the current process. It does this by setting
up SEH and using the Int 2D instruction which will only cause an exception if there is no debugger. Also when used in
OllyDBG it will skip a byte in the disassembly which could be used to detect the debugger.

Vectored Exception Handling is used here because SEH is an anti-debug trick in itself.
*/

extern "C" void __int2d();

static BOOL SwallowedException = TRUE;

static LONG CALLBACK VectoredHandler(_In_ PEXCEPTION_POINTERS ExceptionInfo)
{
    SwallowedException = FALSE;
    if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT)
    {
#ifdef _WIN64
        ExceptionInfo->ContextRecord->Rip++;
#else
        ExceptionInfo->ContextRecord->Eip++;
#endif
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL Interrupt_0x2d()
{
    PVOID Handle       = AddVectoredExceptionHandler(1, VectoredHandler);
    SwallowedException = TRUE;
    __int2d();
    RemoveVectoredExceptionHandler(Handle);
    return SwallowedException;
}
BOOL Interrupt_3()
{
	PVOID Handle = AddVectoredExceptionHandler(1, VectoredHandler);
	SwallowedException = TRUE;
	__debugbreak();
	RemoveVectoredExceptionHandler(Handle);
	return SwallowedException;
}

int ExceptionCheck()
{
    if (UnhandledExcepFilterTest())
    {
        return 1;
    }

    if (HardwareBreakpoints())
    {
        return 2;
    }
    if (Interrupt_0x2d())
    {
        return 3;
    }
    
    if (Interrupt_3())
    {
        return 4;
    }
    
    return 0;
};

BOOL IsSofewareBreakPointInRange(std::uintptr_t start, std::uintptr_t end)
{
    for (std::uintptr_t i = start; i < end; i++)
    {
        if (*(BYTE *)i == 0xCC)
        {
            return TRUE;
        }
    }
    return FALSE;
}
}; // namespace zzj::AntiDebug
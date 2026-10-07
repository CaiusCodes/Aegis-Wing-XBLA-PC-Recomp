#include "aegis_wing_crash_report.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>

#include <rex/ppc.h>

namespace aegis_wing {
namespace {

bool g_symbol_engine_available = false;
thread_local PPCContext* g_guest_context = nullptr;
thread_local const char* g_guest_checkpoint = nullptr;

struct GuestSnapshot {
  const char* checkpoint = nullptr;
  uint32_t r1 = 0;
  uint32_t r3 = 0;
  uint32_t r27 = 0;
  uint32_t r28 = 0;
  uint32_t r29 = 0;
  uint32_t r30 = 0;
  uint32_t r31 = 0;
  uint32_t lr = 0;
  uint32_t ctr = 0;
};

thread_local std::array<GuestSnapshot, 16> g_guest_history = {};
thread_local size_t g_guest_history_count = 0;

LONG WINAPI WriteCrashReport(EXCEPTION_POINTERS* exception) {
  const auto output_directory = std::filesystem::path([] {
    wchar_t executable_path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, executable_path, MAX_PATH);
    return std::filesystem::path(executable_path, executable_path + length)
        .parent_path();
  }());

  const auto report_path = output_directory / "crash_backtrace.txt";
  FILE* report = nullptr;
  _wfopen_s(&report, report_path.c_str(), L"w");
  if (report) {
    const auto* record = exception->ExceptionRecord;
    const auto module_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::fprintf(report, "Exception code: 0x%08lX\n", record->ExceptionCode);
    std::fprintf(report, "Exception address: %p\n", record->ExceptionAddress);
    std::fprintf(report, "Executable base: 0x%llX\n",
                 static_cast<unsigned long long>(module_base));
    std::fprintf(report, "Executable RVA: 0x%llX\n",
                 static_cast<unsigned long long>(
                     reinterpret_cast<uintptr_t>(record->ExceptionAddress) - module_base));
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record->NumberParameters >= 2) {
      const char* operation = record->ExceptionInformation[0] == 0 ? "read" :
          record->ExceptionInformation[0] == 1 ? "write" : "execute";
      std::fprintf(report, "Access violation: %s at 0x%llX\n", operation,
                   static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }

    if (g_guest_context) {
      const auto& guest = *g_guest_context;
      std::fprintf(report,
                   "\nGuest checkpoint: %s\n"
                   "Guest registers: r1=%08X r3=%08X r27=%08X r28=%08X\n"
                   "                 r29=%08X r30=%08X r31=%08X\n"
                   "                 lr=%08X ctr=%08X\n",
                   g_guest_checkpoint ? g_guest_checkpoint : "unknown",
                   guest.r1.u32, guest.r3.u32, guest.r27.u32, guest.r28.u32,
                   guest.r29.u32, guest.r30.u32, guest.r31.u32,
                   static_cast<uint32_t>(guest.lr), guest.ctr.u32);

      std::fprintf(report, "\nGuest checkpoint history:\n");
      const size_t count = std::min(g_guest_history_count,
                                    g_guest_history.size());
      const size_t first = g_guest_history_count > g_guest_history.size()
          ? g_guest_history_count % g_guest_history.size() : 0;
      for (size_t index = 0; index < count; ++index) {
        const auto& snapshot =
            g_guest_history[(first + index) % g_guest_history.size()];
        std::fprintf(report,
                     "  %-28s r1=%08X r3=%08X r27=%08X r28=%08X "
                     "r29=%08X r30=%08X r31=%08X lr=%08X ctr=%08X\n",
                     snapshot.checkpoint ? snapshot.checkpoint : "unknown",
                     snapshot.r1, snapshot.r3, snapshot.r27, snapshot.r28,
                     snapshot.r29, snapshot.r30, snapshot.r31, snapshot.lr,
                     snapshot.ctr);
      }
    }

    HANDLE process = GetCurrentProcess();
    SymRefreshModuleList(process);
    {
      CONTEXT context = *exception->ContextRecord;
      std::fprintf(report,
                   "Registers: RIP=%016llX RSP=%016llX RBP=%016llX\n"
                   "           RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX\n"
                   "           RSI=%016llX RDI=%016llX R8 =%016llX R9 =%016llX\n",
                   context.Rip, context.Rsp, context.Rbp, context.Rax,
                   context.Rbx, context.Rcx, context.Rdx, context.Rsi,
                   context.Rdi, context.R8, context.R9);
      STACKFRAME64 frame = {};
      frame.AddrPC.Offset = context.Rip;
      frame.AddrPC.Mode = AddrModeFlat;
      frame.AddrStack.Offset = context.Rsp;
      frame.AddrStack.Mode = AddrModeFlat;
      frame.AddrFrame.Offset = context.Rbp;
      frame.AddrFrame.Mode = AddrModeFlat;

      std::fprintf(report, "\nStack trace:\n");
      for (unsigned index = 0; index < 96 && frame.AddrPC.Offset; ++index) {
        const DWORD64 address = frame.AddrPC.Offset;
        char symbol_storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0;
        const BOOL has_symbol = SymFromAddr(process, address, &displacement, symbol);
        const BOOL has_line = SymGetLineFromAddr64(process, address,
                                                   &line_displacement, &line);

        std::fprintf(report, "#%02u 0x%016llX", index,
                     static_cast<unsigned long long>(address));
        if (has_symbol) {
          std::fprintf(report, " %s+0x%llX", symbol->Name,
                       static_cast<unsigned long long>(displacement));
        }
        if (has_line) {
          std::fprintf(report, " (%s:%lu)", line.FileName, line.LineNumber);
        }
        std::fputc('\n', report);

        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(),
                         &frame, &context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr)) {
          break;
        }
      }
    }
    std::fprintf(report, "Symbol engine initialized here: %s\n",
                 g_symbol_engine_available ? "yes" : "no (already owned by runtime)");
    std::fclose(report);
  }

  const auto dump_path = output_directory / "crash.dmp";
  HANDLE dump = CreateFileW(dump_path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dump != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION dump_exception = {};
    dump_exception.ThreadId = GetCurrentThreadId();
    dump_exception.ExceptionPointers = exception;
    dump_exception.ClientPointers = FALSE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                      MiniDumpWithIndirectlyReferencedMemory,
                      &dump_exception, nullptr, nullptr);
    CloseHandle(dump);
  }

  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void InstallCrashReporter() {
  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
  g_symbol_engine_available = SymInitialize(process, nullptr, TRUE) != FALSE;
  SetUnhandledExceptionFilter(WriteCrashReport);
}

void TrackGuestContext(PPCContext* context, const char* checkpoint) {
  g_guest_context = context;
  g_guest_checkpoint = checkpoint;
  auto& snapshot =
      g_guest_history[g_guest_history_count % g_guest_history.size()];
  snapshot = {
      checkpoint, context->r1.u32, context->r3.u32,
      context->r27.u32, context->r28.u32, context->r29.u32,
      context->r30.u32, context->r31.u32, static_cast<uint32_t>(context->lr),
      context->ctr.u32,
  };
  ++g_guest_history_count;
}

}  // namespace aegis_wing

#else

namespace aegis_wing {
void InstallCrashReporter() {}
void TrackGuestContext(PPCContext*, const char*) {}
}  // namespace aegis_wing

#endif

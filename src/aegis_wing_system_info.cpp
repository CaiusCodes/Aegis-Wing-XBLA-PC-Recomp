#include "aegis_wing_system_info.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <intrin.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/memory.h>
#include <rex/logging.h>

namespace aegis_wing {
namespace {

std::string Utf8(const wchar_t* text) {
  char out[256] = {};
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out, sizeof(out) - 1, nullptr, nullptr);
  return out;
}

std::string WindowsText() {
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
  HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
  auto get_version =
      ntdll ? reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(ntdll, "RtlGetVersion"))
            : nullptr;
  OSVERSIONINFOW v{};
  v.dwOSVersionInfoSize = sizeof(v);
  if (!get_version || get_version(&v) != 0) return "unknown";
  DWORD ubr = 0, size = sizeof(ubr);
  RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR",
               RRF_RT_REG_DWORD, nullptr, &ubr, &size);
  // Windows 11 still reports itself as 10.0; its builds start at 22000.
  const bool eleven = v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000;
  return fmt::format("{} {}.{}.{}.{}", eleven ? "Windows 11" : "Windows", v.dwMajorVersion,
                     v.dwMinorVersion, v.dwBuildNumber, ubr);
}

std::string CpuText() {
  int regs[4] = {};
  char brand[49] = {};
  __cpuid(regs, int(0x80000000));
  if (unsigned(regs[0]) >= 0x80000004u) {
    for (int i = 0; i < 3; ++i) {
      __cpuid(regs, int(0x80000002 + i));
      std::memcpy(brand + i * 16, regs, 16);
    }
  }
  std::string name = brand;
  name.erase(0, name.find_first_not_of(' '));
  while (!name.empty() && name.back() == ' ') name.pop_back();
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  // Installed memory (64 GB), not what Windows has left after reserving some.
  ULONGLONG installed_kb = 0;
  if (!GetPhysicallyInstalledSystemMemory(&installed_kb)) {
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    installed_kb = mem.ullTotalPhys / 1024;
  }
  return fmt::format("{}, {} threads, {:.0f} GB memory", name.empty() ? "unknown" : name,
                     si.dwNumberOfProcessors, double(installed_kb) / (1024.0 * 1024));
}

// The first hardware adapter (the one the renderer uses), read without running
// anything on the GPU; DXGI and D3D12 are loaded at run time.
std::string GpuText() {
  std::string out = "unknown";
  HMODULE dxgi = ::LoadLibraryW(L"dxgi.dll");
  HMODULE d3d12 = ::LoadLibraryW(L"d3d12.dll");
  if (dxgi && d3d12) {
    using CreateFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
    auto create_factory1 =
        reinterpret_cast<CreateFactory1Fn>(::GetProcAddress(dxgi, "CreateDXGIFactory1"));
    auto create_device =
        reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(::GetProcAddress(d3d12, "D3D12CreateDevice"));
    IDXGIFactory1* factory = nullptr;
    if (create_factory1 && create_device &&
        SUCCEEDED(create_factory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
      IDXGIAdapter1* adapter = nullptr;
      for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        const bool hardware =
            SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE);
        if (hardware) {
          std::string driver = "unknown";
          // The user-mode driver version, a.b.c.d. NVIDIA's own number is the
          // last five digits of c and d together: 32.0.15.6636 is 566.36.
          LARGE_INTEGER umd{};
          if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
            const unsigned a = HIWORD(umd.HighPart), b = LOWORD(umd.HighPart);
            const unsigned c = HIWORD(umd.LowPart), d = LOWORD(umd.LowPart);
            driver = fmt::format("{}.{}.{}.{}", a, b, c, d);
            if (desc.VendorId == 0x10DE) {
              const unsigned nv = (c % 10) * 10000 + d;
              driver += fmt::format(" ({}.{:02})", nv / 100, nv % 100);
            }
          }
          bool integrated = false;
          ID3D12Device* device = nullptr;
          if (SUCCEEDED(create_device(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                      reinterpret_cast<void**>(&device)))) {
            D3D12_FEATURE_DATA_ARCHITECTURE arch = {};
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &arch,
                                                      sizeof(arch)))) {
              integrated = arch.UMA != FALSE;
            }
            device->Release();
          }
          out = fmt::format("{} ({}, {} MB), driver {}", Utf8(desc.Description),
                            integrated ? "integrated" : "dedicated",
                            uint64_t(desc.DedicatedVideoMemory) / (1024 * 1024), driver);
          adapter->Release();
          break;
        }
        adapter->Release();
      }
      factory->Release();
    }
  }
  if (d3d12) ::FreeLibrary(d3d12);
  if (dxgi) ::FreeLibrary(dxgi);
  return out;
}

// One line per active display: its name, the monitor's own name, the desktop
// mode and the exact refresh rate (a "60 Hz" screen is often 59.94 or 60.1).
std::vector<std::string> DisplayLines() {
  std::vector<std::string> out;
  UINT32 path_count = 0, mode_count = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) !=
      ERROR_SUCCESS) {
    return out;
  }
  std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
  std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
  if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count,
                         modes.data(), nullptr) != ERROR_SUCCESS) {
    return out;
  }
  paths.resize(path_count);
  MONITORINFOEXW primary{};
  primary.cbSize = sizeof(primary);
  if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary)) {
    primary.szDevice[0] = 0;
  }
  for (const auto& path : paths) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) {
      source.viewGdiDeviceName[0] = 0;
    }
    DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
    target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    target.header.size = sizeof(target);
    target.header.adapterId = path.targetInfo.adapterId;
    target.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) {
      target.monitorFriendlyDeviceName[0] = 0;
    }
    uint32_t width = 0, height = 0;
    const UINT32 index = path.sourceInfo.modeInfoIdx;
    if (index < modes.size() && modes[index].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
      width = modes[index].sourceMode.width;
      height = modes[index].sourceMode.height;
    }
    const auto& rate = path.targetInfo.refreshRate;
    const double hz = rate.Denominator ? double(rate.Numerator) / double(rate.Denominator) : 0.0;
    const bool is_primary =
        primary.szDevice[0] && wcscmp(primary.szDevice, source.viewGdiDeviceName) == 0;
    const std::string monitor = Utf8(target.monitorFriendlyDeviceName);
    out.push_back(fmt::format("display {}{}: {} x {} at {:.3f} Hz{}",
                              Utf8(source.viewGdiDeviceName),
                              monitor.empty() ? std::string() : " (" + monitor + ")", width,
                              height, hz, is_primary ? ", primary" : ""));
  }
  return out;
}

}  // namespace

void LogSystemInfo() {
  REXLOG_INFO("[system] {}", WindowsText());
  REXLOG_INFO("[system] CPU: {}", CpuText());
  REXLOG_INFO("[system] GPU: {}", GpuText());
  for (const auto& line : DisplayLines()) REXLOG_INFO("[system] {}", line);
  auto flag = [](const char* name) { return rex::cvar::GetFlagByName(name); };
  REXLOG_INFO("[system] settings: gpu {}, fullscreen {}, vsync {}, render scale {}x, "
              "upscaling {}, anti-aliasing {}, window {} x {}",
              flag("gpu"), flag("fullscreen"), flag("vsync"), flag("aw_render_scale"),
              flag("present_effect"), flag("swap_post_effect"), flag("video_mode_width"),
              flag("video_mode_height"));
}

}  // namespace aegis_wing

// ---- Game steps (vblank_timing_log) -------------------------------------------
// Aegis Wing steps its game once per frame with the time since the previous
// frame (the timebase divided by KeQueryPerformanceFrequency; no Xbox 360
// timebase is built in and there is no minimum step), so a frame never counts
// twice: game speed is the guest clock. 0x924B967C is the game's own frame
// counter (+1 per frame; found by snapshotting guest memory 10 s apart).
namespace aegis_wing {

void StartGameStepLog(rex::memory::Memory* memory) {
  if (!memory) return;
  std::thread([memory]() {
    constexpr uint32_t kFrameCounter = 0x924B967C;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    bool was_on = false;
    LARGE_INTEGER start{};
    uint64_t guest_start = 0;
    uint32_t frames_start = 0;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      const bool on = rex::cvar::GetFlagByName("vblank_timing_log") == "true";
      LARGE_INTEGER now;
      QueryPerformanceCounter(&now);
      const uint64_t guest_now = rex::chrono::Clock::QueryGuestTickCount();
      const uint32_t frames_now =
          _byteswap_ulong(*memory->TranslateVirtual<const uint32_t*>(kFrameCounter));
      if (!on) {
        was_on = false;
        continue;
      }
      if (!was_on) {
        was_on = true;
        start = now;
        guest_start = guest_now;
        frames_start = frames_now;
        continue;
      }
      const double seconds = double(now.QuadPart - start.QuadPart) / double(freq.QuadPart);
      if (seconds < 10.0) continue;
      const double guest_seconds =
          double(guest_now - guest_start) / double(rex::chrono::Clock::guest_tick_frequency());
      const uint32_t frames = frames_now - frames_start;
      REXLOG_INFO("[timing] game steps: {} frames in {:.1f} s ({:.2f}/s, one step per frame), "
                  "game clock {:.3f}% of real time",
                  frames, seconds, double(frames) / seconds, guest_seconds / seconds * 100.0);
      start = now;
      guest_start = guest_now;
      frames_start = frames_now;
    }
  }).detach();
}

}  // namespace aegis_wing

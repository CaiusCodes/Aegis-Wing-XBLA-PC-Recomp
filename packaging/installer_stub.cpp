// Aegis Wing PC - Setup ("Setup Aegis Wing.exe")
//
// The release folder holds only this program, README.txt and licenses\.
// Everything Setup installs - the PC runtime, resources\ (the Setup scripts
// and default settings) and release-manifest.json - travels inside this
// executable as a payload appended after the program image:
//
//   [program][entries...][u64 payload offset][8-byte magic "XBLAPAY1"]
//   entry = u16 path length, UTF-8 path ('/' separators), u64 size, bytes
//
// Setup unpacks the payload into a temporary folder, runs
// resources\installer\Install-AegisWing.ps1 from there through a hidden
// Windows PowerShell host with -ReleaseRoot set to this program's folder,
// forwards its own command line (used for automated testing), returns the
// script's exit code and deletes the temporary folder. Nothing is written
// beside this program unless the install succeeds (the script creates Game\).
//
// A GUI-subsystem program, so starting Setup never flashes a console. Built
// by packaging/Build-Stubs.ps1, which embeds an asInvoker manifest: without it
// Windows treats any "Setup*.exe" as an installer and demands administrator
// rights. tools/Make-Release.ps1 appends the payload.

#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr char kMagic[8] = {'X', 'B', 'L', 'A', 'P', 'A', 'Y', '1'};
constexpr wchar_t kTitle[] = L"Aegis Wing Setup";
constexpr wchar_t kScript[] = L"resources\\installer\\Install-AegisWing.ps1";

std::wstring ModulePath() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (length == 0) return L"";
    if (length < path.size()) {
      path.resize(length);
      return path;
    }
    path.resize(path.size() * 2);
  }
}

std::wstring Directory(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

void Fail(const std::wstring& message) {
  MessageBoxW(nullptr, message.c_str(), kTitle, MB_OK | MB_ICONERROR);
}

std::wstring Widen(const std::string& utf8) {
  const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
  std::wstring wide(size_t(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), wide.data(), count);
  return wide;
}

class Reader {
 public:
  explicit Reader(HANDLE file) : file_(file) {}
  bool Seek(uint64_t offset) {
    LARGE_INTEGER position;
    position.QuadPart = LONGLONG(offset);
    return SetFilePointerEx(file_, position, nullptr, FILE_BEGIN) != 0;
  }
  bool Read(void* out, size_t size) {
    auto* bytes = static_cast<uint8_t*>(out);
    while (size > 0) {
      DWORD got = 0;
      const DWORD chunk = DWORD(size > (1u << 20) ? (1u << 20) : size);
      if (!ReadFile(file_, bytes, chunk, &got, nullptr) || got == 0) return false;
      bytes += got;
      size -= got;
    }
    return true;
  }

 private:
  HANDLE file_;
};

bool MakeDirectories(const std::wstring& directory) {
  if (directory.empty()) return true;
  if (GetFileAttributesW(directory.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
  const size_t slash = directory.find_last_of(L'\\');
  if (slash != std::wstring::npos && !MakeDirectories(directory.substr(0, slash))) return false;
  return CreateDirectoryW(directory.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// Unpacks the appended payload into `target`. False with a message on error.
bool Unpack(const std::wstring& self, const std::wstring& target, std::wstring* error) {
  HANDLE file = CreateFileW(self.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    *error = L"Setup could not read itself.";
    return false;
  }
  Reader reader(file);
  LARGE_INTEGER file_size = {};
  GetFileSizeEx(file, &file_size);
  uint64_t payload_start = 0;
  char magic[8] = {};
  bool ok = file_size.QuadPart > 16 && reader.Seek(uint64_t(file_size.QuadPart) - 16) &&
            reader.Read(&payload_start, 8) && reader.Read(magic, 8) &&
            std::memcmp(magic, kMagic, 8) == 0 && payload_start < uint64_t(file_size.QuadPart) - 16;
  if (!ok) {
    CloseHandle(file);
    *error =
        L"This copy of Setup is incomplete (its install data is missing).\n\n"
        L"Download the release again and extract the whole ZIP before running Setup.";
    return false;
  }
  const uint64_t payload_end = uint64_t(file_size.QuadPart) - 16;
  reader.Seek(payload_start);
  uint64_t position = payload_start;
  std::vector<uint8_t> buffer(1u << 20);
  while (ok && position < payload_end) {
    uint16_t path_length = 0;
    uint64_t size = 0;
    std::string path;
    ok = reader.Read(&path_length, 2) && path_length > 0;
    if (ok) {
      path.resize(path_length);
      ok = reader.Read(path.data(), path_length) && reader.Read(&size, 8);
    }
    // Relative paths only: nothing may be written outside the target.
    if (ok && (path.find("..") != std::string::npos || path[0] == '/' || path[0] == '\\' ||
               path.find(':') != std::string::npos)) {
      ok = false;
    }
    if (!ok) break;
    std::wstring relative = Widen(path);
    for (auto& c : relative) {
      if (c == L'/') c = L'\\';
    }
    const std::wstring out_path = target + L"\\" + relative;
    if (!MakeDirectories(Directory(out_path))) {
      ok = false;
      break;
    }
    HANDLE out = CreateFileW(out_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
      ok = false;
      break;
    }
    uint64_t left = size;
    while (ok && left > 0) {
      const size_t chunk = size_t(left > buffer.size() ? buffer.size() : left);
      DWORD written = 0;
      ok = reader.Read(buffer.data(), chunk) &&
           WriteFile(out, buffer.data(), DWORD(chunk), &written, nullptr) && written == chunk;
      left -= chunk;
    }
    CloseHandle(out);
    position += 2 + path_length + 8 + size;
  }
  CloseHandle(file);
  if (!ok) {
    *error =
        L"Setup could not unpack its install data. The download may be damaged, or the "
        L"temporary folder may be full.\n\nDownload the release again and retry.";
  }
  return ok;
}

void DeleteTree(const std::wstring& directory) {
  std::wstring from = directory;
  from.push_back(L'\0');  // SHFileOperation wants a double-null-terminated list
  SHFILEOPSTRUCTW operation = {};
  operation.wFunc = FO_DELETE;
  operation.pFrom = from.c_str();
  operation.fFlags = FOF_NO_UI;
  SHFileOperationW(&operation);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR args, int) {
  const std::wstring self = ModulePath();
  const std::wstring release_root = Directory(self);

  // A private temporary folder for this run.
  wchar_t temp[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring work = std::wstring(temp) + L"AegisWingSetup-" +
                            std::to_wstring(GetCurrentProcessId()) + L"-" +
                            std::to_wstring(GetTickCount64());
  std::wstring error;
  if (!MakeDirectories(work) || !Unpack(self, work, &error)) {
    DeleteTree(work);
    Fail(error.empty() ? L"Setup could not create its temporary folder." : error);
    return 1;
  }

  const std::wstring script = work + L"\\" + kScript;
  if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) {
    DeleteTree(work);
    Fail(L"Setup's install data is incomplete. Download the release again.");
    return 1;
  }

  // Full path, so a stray powershell.exe elsewhere on PATH is never picked up.
  wchar_t system_dir[MAX_PATH] = {};
  GetSystemDirectoryW(system_dir, MAX_PATH);
  const std::wstring powershell =
      std::wstring(system_dir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";

  std::wstring command = L"\"" + powershell +
                         L"\" -NoProfile -STA -ExecutionPolicy Bypass -File \"" + script +
                         L"\" -ReleaseRoot \"" + release_root + L"\"";
  if (args && *args) {
    command += L" ";
    command += args;
  }

  // "D:" alone means "the current folder on D:"; a release extracted to a
  // drive root needs "D:\" as a working directory. (The argument above stays
  // "D:" - a trailing backslash would escape its closing quote - and the
  // script adds the backslash itself.)
  std::wstring working_dir = release_root;
  if (!working_dir.empty() && working_dir.back() == L':') working_dir += L"\\";

  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  // CREATE_NO_WINDOW hides PowerShell's console; the script draws its own window.
  if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, working_dir.c_str(), &startup, &process)) {
    DeleteTree(work);
    Fail(L"Setup could not start Windows PowerShell.");
    return 1;
  }

  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hProcess);
  CloseHandle(process.hThread);
  DeleteTree(work);
  return static_cast<int>(exit_code);
}

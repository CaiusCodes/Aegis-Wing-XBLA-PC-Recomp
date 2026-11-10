#include "aegis_wing_update_check.h"

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <atomic>
#include <cctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/logging.h>


namespace aegis_wing {
namespace {

constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kApiPath[] = L"/repos/CaiusCodes/Aegis-Wing-XBLA-PC-Recomp/releases/latest";
// Always this page, never a link taken from the response.
constexpr wchar_t kReleasesPage[] =
    L"https://github.com/CaiusCodes/Aegis-Wing-XBLA-PC-Recomp/releases/latest";

std::atomic<UpdateState> g_state{UpdateState::kIdle};
std::mutex g_mutex;
std::string g_latest;

// "v0.9.3", "0.9.3a" -> {0, 9, 3}: the numbers only, for comparing.
std::vector<int> VersionNumbers(const std::string& text) {
  std::vector<int> numbers;
  int value = -1;
  for (char c : text) {
    if (std::isdigit(static_cast<unsigned char>(c))) {
      value = (value < 0 ? 0 : value * 10) + (c - '0');
    } else if (c == '.') {
      if (value >= 0) numbers.push_back(value);
      value = -1;
    } else if (value >= 0) {
      break;  // a trailing letter or suffix ends the number
    }
  }
  if (value >= 0) numbers.push_back(value);
  return numbers;
}

bool IsNewer(const std::string& latest, const std::string& current) {
  std::vector<int> a = VersionNumbers(latest), b = VersionNumbers(current);
  const size_t n = std::max(a.size(), b.size());
  a.resize(n, 0);
  b.resize(n, 0);
  return a > b;
}

// The body of GET https://api.github.com/<path>, or empty on any failure.
std::string HttpGet() {
  const std::wstring agent = L"AegisWingPC/" + std::wstring(AEGISWING_VERSION, AEGISWING_VERSION +
                                                                                    sizeof(AEGISWING_VERSION) - 1);
  std::string body;
  HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return body;
  WinHttpSetTimeouts(session, 5000, 5000, 8000, 8000);
  HINTERNET connect = WinHttpConnect(session, kApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", kApiPath, nullptr,
                                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                   WINHTTP_FLAG_SECURE)
                              : nullptr;
  DWORD status = 0, status_size = sizeof(status);
  if (request &&
      WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", DWORD(-1),
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(request, nullptr) &&
      WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                          WINHTTP_NO_HEADER_INDEX) &&
      status == 200) {
    for (;;) {
      DWORD available = 0;
      if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
      std::string chunk(available, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) break;
      body.append(chunk.data(), read);
      if (body.size() > (1u << 20)) break;  // a release record is a few KB
    }
  }
  if (request) WinHttpCloseHandle(request);
  if (connect) WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return body;
}

// The "tag_name" string from the release JSON.
std::string TagName(const std::string& json) {
  const size_t key = json.find("\"tag_name\"");
  if (key == std::string::npos) return {};
  const size_t open = json.find('"', json.find(':', key) + 1);
  const size_t close = open == std::string::npos ? open : json.find('"', open + 1);
  if (close == std::string::npos || close - open > 40) return {};
  return json.substr(open + 1, close - open - 1);
}

}  // namespace

void StartUpdateCheck() {
  UpdateState expected = g_state.load();
  if (expected == UpdateState::kChecking ||
      !g_state.compare_exchange_strong(expected, UpdateState::kChecking)) {
    return;
  }
  std::thread([] {
    const std::string tag = TagName(HttpGet());
    std::string latest = tag;
    if (!latest.empty() && (latest[0] == 'v' || latest[0] == 'V')) latest.erase(0, 1);
    {
      std::lock_guard lock(g_mutex);
      g_latest = latest;
    }
    if (latest.empty() || VersionNumbers(latest).empty()) {
      REXLOG_INFO("[update] check failed (no answer from GitHub)");
      g_state = UpdateState::kFailed;
    } else if (IsNewer(latest, AEGISWING_VERSION)) {
      REXLOG_INFO("[update] {} is available (this is {})", latest, AEGISWING_VERSION);
      g_state = UpdateState::kAvailable;
    } else {
      REXLOG_INFO("[update] up to date ({}; newest release {})", AEGISWING_VERSION, latest);
      g_state = UpdateState::kUpToDate;
    }
  }).detach();
}

UpdateState GetUpdateState() { return g_state.load(); }

std::string GetLatestVersion() {
  std::lock_guard lock(g_mutex);
  return g_latest;
}

void OpenReleasesPage() {
  ShellExecuteW(nullptr, L"open", kReleasesPage, nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace aegis_wing

// Aegis Wing XBLA PC Recomp - the setup window.
//
// "Formation run": the look of the earlier Setup, redrawn natively. A side-
// scrolling stage over the orange Europa nebula, rebuilt from primitives (no
// game artwork): the four player ships fly linked along a gold rail through
// the steps (PACKAGE, IMPORT, PLAY) while the controls sit in one dark glass
// panel below. One page: the package line (Browse package or drag and drop),
// the import progress, the optional extras and Play.
//
// Everything is painted with GDI+ in 96-DPI layout units and scaled for the
// monitor's DPI, so the window stays sharp at any Windows scaling.
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus

#include "launcher.h"

#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>
#include <shobjidl.h>

#include <cmath>
#include <memory>
#include <thread>

#include "resource.h"

namespace aw {
namespace {

using namespace Gdiplus;

// ------------------------------------------------------------- layout --
constexpr float kW = 720, kH = 708;
constexpr float kUiScale = 1.12f;  // drawn 12% larger than the 96-DPI layout
constexpr float kRailX0 = 70, kRailX1 = 650, kRailY = 236;
constexpr float kPanelX = 40, kPanelY = 296, kPanelW = 640, kPanelH = 380;
constexpr float kFieldY = 366, kFieldH = 38;
constexpr float kRowsY = 366, kRowPitch = 28;  // candidate rows when several packages are found
constexpr int kMaxRows = 4;
constexpr float kExtrasDivider = 482, kExtrasFirst = 508, kExtraPitch = 36;

enum Control { kNone = -1, kBrowse, kLeft, kPlay, kExtra0, kExtra1, kExtra2, kRow0 };

enum class Mode { Idle, Choosing, Importing, Ready };

constexpr UINT WM_APP_START = WM_APP + 1;
constexpr UINT WM_APP_PROGRESS = WM_APP + 2;
constexpr UINT WM_APP_DONE = WM_APP + 3;
constexpr UINT_PTR kAnimationTimer = 1;

const wchar_t* const kClassName = L"AegisWingSetup";
const wchar_t kDot[] = L"·";
const wchar_t kEllipsis[] = L"…";
const wchar_t kTick[] = L"✓";

Color Hex(uint32_t rgb, BYTE alpha = 255) {
  return Color(alpha, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

Color Mix(uint32_t a, uint32_t b, float t) {
  auto channel = [&](int shift) {
    const float x = float((a >> shift) & 0xFF), y = float((b >> shift) & 0xFF);
    return static_cast<BYTE>(x + (y - x) * t);
  };
  return Color(255, channel(16), channel(8), channel(0));
}

namespace pal {
const uint32_t Gold = 0xE4B56C, GoldHi = 0xFFE2B0, Ember = 0xE08A3A, Cream = 0xFFF1DA, Sand = 0xC9AE8E, Dim = 0x8C7358,
               Ok = 0x9BE3A0, Bad = 0xFF8F80, Beam = 0x9CE6FF;
const uint32_t Red = 0xFF5A6E, Blue = 0x4E8DF5, Green = 0x46C85A, Yellow = 0xF2CF3A;
}  // namespace pal

struct ProgressMsg {
  int percent;
  std::wstring phase, detail;
};

struct RectL {
  float x, y, w, h;
  bool Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct ExtraDef {
  const wchar_t* title;
  const wchar_t* detail;
};
const ExtraDef kExtraDefs[3] = {
    {L"Unlock Stage Select", L"Unlocked by completing the game on Insane."},
    {L"Start in fullscreen", L"Switch at any time from Help and Options, Settings."},
    {L"Create desktop shortcut", L"Adds an Aegis Wing icon to your desktop. It follows this folder if you move it."},
};

class SetupWindow {
 public:
  SetupWindow(const Paths& p, const SetupOptions& o) : p_(p), options_(o) {}
  int Run(HINSTANCE instance);

 private:
  static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

  void CreateFonts();
  void ApplyScale(UINT dpi, const RECT* suggested);
  void BuildBackdrop(int width, int height);
  void Paint(HDC dc);
  void PaintButton(Graphics& g, Control id, const wchar_t* text, bool enabled, bool primary, Font* font);
  void PaintText(Graphics& g, const std::wstring& text, Font* font, uint32_t color, RectF rect,
                 StringAlignment align = StringAlignmentNear, bool wrap = false);
  void PaintShip(Graphics& g, float cx, float cy, float length, uint32_t color);

  RectL ControlRect(Control id) const;
  bool Enabled(Control id) const;
  Control HitTest(float x, float y) const;
  void Click(Control id);

  void Start();
  void Browse();
  void SelectPath(const fs::path& path);
  void StartImport(Source source);  // by value: it may come from candidates_, which is cleared
  void OnProgress(ProgressMsg* msg);
  void OnDone(ImportResult* result);
  void Play();
  void RequestClose();
  void Invalidate() { InvalidateRect(hwnd_, nullptr, FALSE); }
  void InvalidateStage();
  float ShipTarget() const;
  int Stage() const;

  const Paths& p_;
  SetupOptions options_;
  HWND hwnd_ = nullptr;
  HICON icon_ = nullptr;
  float scale_ = 1;
  Mode mode_ = Mode::Idle;
  bool installed_ = false;
  bool imported_now_ = false;
  bool close_after_import_ = false;
  std::vector<Source> candidates_;
  std::wstring heading_, hint_, path_text_, status_, phase_, stats_, detail_;
  uint32_t status_color_ = pal::Dim, phase_color_ = pal::Gold;
  bool path_set_ = false;
  int progress_ = 0;
  bool show_bar_ = false;  // an import is running or just finished: the formation follows progress_
  float ship_f_ = 0, ship_target_ = 0;
  Extras want_, was_;
  Control hover_ = kNone, pressed_ = kNone;
  bool tracking_ = false;
  std::atomic<bool> cancel_{false};
  std::thread worker_;
  std::unique_ptr<Bitmap> backdrop_;
  std::unique_ptr<Font> word_, tag_, station_, button_, play_, title_, body_, small_, caps_, mono_, row_title_, row_detail_;
};

// ------------------------------------------------------------ helpers --
void RoundedPath(GraphicsPath& path, float x, float y, float w, float h, float r) {
  const float d = std::max(1.0f, std::min(r * 2, std::min(w, h)));
  path.AddArc(x, y, d, d, 180, 90);
  path.AddArc(x + w - d, y, d, d, 270, 90);
  path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
  path.AddArc(x, y + h - d, d, d, 90, 90);
  path.CloseFigure();
}

// The first installed family wins (GDI+ silently substitutes a missing one).
// Sizes are in 96-DPI pixels; the paint transform scales them.
std::unique_ptr<Font> MakeFont(std::initializer_list<const wchar_t*> families, float pixels, int style) {
  for (const wchar_t* family : families) {
    FontFamily candidate(family);
    if (candidate.IsAvailable() && candidate.IsStyleAvailable(style))
      return std::make_unique<Font>(&candidate, pixels, style, UnitPixel);
  }
  return std::make_unique<Font>(L"Segoe UI", pixels, style, UnitPixel);
}

void FillGradient(Graphics& g, GraphicsPath& path, const RectF& area, std::initializer_list<Color> colors,
                  std::initializer_list<float> positions, float angle) {
  // The rectangle is padded by a pixel because GDI+ wraps the first colour onto the last row otherwise.
  RectF padded(area.X - 1, area.Y - 1, area.Width + 2, area.Height + 2);
  std::vector<Color> c(colors);
  std::vector<REAL> pos(positions);
  LinearGradientBrush brush(padded, c.front(), c.back(), angle, FALSE);
  brush.SetInterpolationColors(c.data(), pos.data(), static_cast<INT>(c.size()));
  g.FillPath(&brush, &path);
}

// A soft radial glow: full colour at the centre, transparent at the edge.
void PaintGlow(Graphics& g, float cx, float cy, float rx, float ry, BYTE alpha, uint32_t color) {
  GraphicsPath path;
  path.AddEllipse(cx - rx, cy - ry, rx * 2, ry * 2);
  PathGradientBrush brush(&path);
  brush.SetCenterColor(Hex(color, alpha));
  Color edge = Hex(color, 0);
  INT count = 1;
  brush.SetSurroundColors(&edge, &count);
  g.FillPath(&brush, &path);
}

void SetupWindow::CreateFonts() {
  word_ = MakeFont({L"Bahnschrift SemiBold Condensed", L"Bahnschrift Condensed", L"Bahnschrift", L"Arial Narrow"}, 52, FontStyleBold);
  tag_ = MakeFont({L"Bahnschrift SemiBold Condensed", L"Bahnschrift", L"Segoe UI Semibold"}, 13, FontStyleRegular);
  station_ = MakeFont({L"Bahnschrift SemiBold Condensed", L"Bahnschrift", L"Segoe UI Semibold"}, 13, FontStyleRegular);
  button_ = MakeFont({L"Bahnschrift SemiBold", L"Bahnschrift", L"Segoe UI Semibold"}, 15, FontStyleRegular);
  play_ = MakeFont({L"Bahnschrift SemiBold", L"Bahnschrift", L"Segoe UI Semibold"}, 19, FontStyleRegular);
  title_ = MakeFont({L"Segoe UI Semibold", L"Segoe UI"}, 19, FontStyleRegular);
  body_ = MakeFont({L"Segoe UI"}, 13.5f, FontStyleRegular);
  small_ = MakeFont({L"Segoe UI"}, 12, FontStyleRegular);
  caps_ = MakeFont({L"Segoe UI Semibold", L"Segoe UI"}, 11, FontStyleRegular);
  mono_ = MakeFont({L"Consolas", L"Courier New"}, 12.5f, FontStyleRegular);
  row_title_ = MakeFont({L"Segoe UI Semibold", L"Segoe UI"}, 13.5f, FontStyleRegular);
  row_detail_ = MakeFont({L"Segoe UI"}, 11.5f, FontStyleRegular);
}

// --------------------------------------------------------------- scale --
// The layout scale for a monitor: its DPI times the 12% enlargement, reduced
// when the window would not fit the monitor's work area (small laptop screens).
void SetupWindow::ApplyScale(UINT dpi, const RECT* suggested) {
  HMONITOR monitor = suggested ? MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST)
                               : (hwnd_ ? MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST)
                                        : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
  MONITORINFO info = {sizeof(info)};
  GetMonitorInfoW(monitor, &info);
  const RECT work = info.rcWork;
  const float base = dpi / 96.0f;
  const float fit = std::min((work.right - work.left) * 0.95f / kW, (work.bottom - work.top) * 0.95f / kH);
  scale_ = std::max(0.5f, std::min(base * kUiScale, fit));
  const int w = static_cast<int>(kW * scale_ + 0.5f), h = static_cast<int>(kH * scale_ + 0.5f);
  int x, y;
  if (suggested) {
    x = suggested->left;
    y = suggested->top;
  } else {
    x = work.left + ((work.right - work.left) - w) / 2;
    y = work.top + ((work.bottom - work.top) - h) / 2;
  }
  SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  const int r = static_cast<int>(18 * scale_);
  SetWindowRgn(hwnd_, CreateRoundRectRgn(0, 0, w + 1, h + 1, r * 2, r * 2), TRUE);
  backdrop_.reset();
  Invalidate();
}

// ------------------------------------------------------------ backdrop --
// Everything that never changes is drawn once into a bitmap at the real pixel
// size: the nebula, the stars, the wordmark, the unlit rail and the glass panel.
void SetupWindow::BuildBackdrop(int width, int height) {
  backdrop_ = std::make_unique<Bitmap>(width, height, PixelFormat32bppPARGB);
  Graphics g(backdrop_.get());
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
  g.ScaleTransform(scale_, scale_);
  g.Clear(Hex(0x050304));

  // The Europa nebula: a warm bank up and to the right, a faint ember low left.
  PaintGlow(g, 560, 205, 360, 250, 255, 0x6A2A0E);
  PaintGlow(g, 585, 190, 230, 160, 210, 0xB9551D);
  PaintGlow(g, 600, 180, 110, 80, 120, 0xF08A3A);
  PaintGlow(g, 110, 470, 260, 180, 150, 0x5A230C);

  // Stars from a fixed seed, so every launch shows the same sky.
  uint64_t seed = 1337;
  auto next = [&]() {
    seed = (seed * 1103515245ull + 12345ull) % 2147483648ull;
    return seed;
  };
  for (int i = 0; i < 150; ++i) {
    const float sx = (next() % 7200) / 10.0f;
    const float sy = (next() % 3000) / 10.0f;
    const uint64_t last = next();
    const float size = 0.6f + (last % 100) / 100.0f;
    const BYTE alpha = static_cast<BYTE>(90 + last % 150);
    SolidBrush star(Hex(0xFFF6EA, alpha));
    g.FillEllipse(&star, sx, sy, size, size);
  }

  // "AEGIS WING" in a sheared condensed face, gold from pale to bronze, with a
  // dark edge and a drop line - drawn as outlines from a system font.
  {
    FontFamily family;
    word_->GetFamily(&family);
    GraphicsPath word;
    // The lean makes neighbouring glyphs overlap (notably "AE"); the winding
    // fill merges them into one shape instead of punching holes.
    word.SetFillMode(FillModeWinding);
    word.AddString(L"AEGIS WING", -1, &family, word_->GetStyle(), 56, PointF(0, 0), StringFormat::GenericTypographic());
    RectF b;
    word.GetBounds(&b);
    const float x = 42, bottom = 80;
    Matrix move;
    move.Translate(x - b.X, bottom - (b.Y + b.Height));
    word.Transform(&move);
    // Italic lean: x' = x - 0.18 (y - bottom), so the baseline stays put.
    Matrix lean(1, 0, -0.18f, 1, 0.18f * bottom, 0);
    word.Transform(&lean);
    RectF wb;
    word.GetBounds(&wb);
    std::unique_ptr<GraphicsPath> shadow(word.Clone());
    Matrix drop;
    drop.Translate(0, 3);
    shadow->Transform(&drop);
    SolidBrush shadow_brush(Hex(0x1A0803, 200));
    g.FillPath(&shadow_brush, shadow.get());
    Pen edge(Hex(0x3A1A06), 1.1f);
    // Stroke first and fill over it, so the outline is never drawn across a
    // seam where two letters overlap.
    g.DrawPath(&edge, &word);
    FillGradient(g, word, wb, {Hex(0xFFF3D6), Hex(0xF0C987), Hex(0xD8A460), Hex(0x9A5220)}, {0, 0.35f, 0.55f, 1}, 90);
  }
  SolidBrush tag_brush(Hex(0xE8C9A0));
  g.DrawString((std::wstring(L"SETUP  ") + kDot + L"  V" + [&] {
                 std::wstring v = kVersion;
                 for (wchar_t& c : v) c = static_cast<wchar_t>(towupper(c));
                 return v;
               }()).c_str(), -1, tag_.get(), RectF(46, 88, 360, 18), nullptr, &tag_brush);
  SolidBrush muted(Hex(pal::Sand));
  StringFormat right;
  right.SetAlignment(StringAlignmentFar);
  g.DrawString(L"XBLA RECOMP", -1, tag_.get(), RectF(430, 36, 250, 18), &right, &muted);
  g.DrawString(L"TITLE 5841083C", -1, tag_.get(), RectF(430, 54, 250, 18), &right, &muted);

  // The unlit rail.
  {
    GraphicsPath rail;
    rail.AddRectangle(RectF(kRailX0 - 20, kRailY - 1, kRailX1 - kRailX0 + 40, 2));
    FillGradient(g, rail, RectF(kRailX0 - 20, kRailY - 1, kRailX1 - kRailX0 + 40, 3),
                 {Hex(pal::Gold, 0), Hex(pal::Gold, 120), Hex(pal::Gold, 120), Hex(pal::Gold, 0)}, {0, 0.06f, 0.94f, 1}, 0);
  }

  // Dark glass panel.
  {
    GraphicsPath panel;
    RoundedPath(panel, kPanelX, kPanelY, kPanelW, kPanelH, 16);
    FillGradient(g, panel, RectF(kPanelX, kPanelY, kPanelW, kPanelH), {Hex(0x160B06, 232), Hex(0x0A0503, 244)}, {0, 1}, 90);
    Pen rim(Hex(pal::Gold, 90), 1);
    g.DrawPath(&rim, &panel);
    Pen high(Hex(pal::GoldHi, 40), 1);
    g.DrawLine(&high, kPanelX + 18, kPanelY + 1, kPanelX + kPanelW - 18, kPanelY + 1);
  }

  GraphicsPath outline;
  RoundedPath(outline, 0.5f, 0.5f, kW - 1, kH - 1, 18);
  Pen outline_pen(Hex(pal::Gold, 110), 1);
  g.DrawPath(&outline_pen, &outline);
}

// ------------------------------------------------------------ controls --
RectL SetupWindow::ControlRect(Control id) const {
  switch (id) {
    case kBrowse: return {518, kFieldY, 138, kFieldH};
    case kLeft: return {372, 626, 120, 40};
    case kPlay: return {508, 622, 148, 48};
    case kExtra0:
    case kExtra1:
    case kExtra2: return {54, kExtrasFirst + (id - kExtra0) * kExtraPitch - 5, 602, kExtraPitch - 2};
    default: {
      const int row = id - kRow0;
      return {64, kRowsY + row * kRowPitch, 442, kRowPitch - 2};
    }
  }
}

bool SetupWindow::Enabled(Control id) const {
  switch (id) {
    case kBrowse: return mode_ != Mode::Importing && !imported_now_;
    case kLeft: return !(mode_ == Mode::Importing && cancel_.load());
    case kPlay: return mode_ == Mode::Ready && installed_;
    case kExtra0:
    case kExtra1:
    case kExtra2: return true;
    default: return mode_ == Mode::Choosing && id - kRow0 < static_cast<int>(candidates_.size());
  }
}

Control SetupWindow::HitTest(float x, float y) const {
  std::vector<Control> ids = {kBrowse, kLeft, kPlay, kExtra0, kExtra1, kExtra2};
  if (mode_ == Mode::Choosing) {
    for (int i = 0; i < static_cast<int>(candidates_.size()) && i < kMaxRows; ++i)
      ids.push_back(static_cast<Control>(kRow0 + i));
  }
  for (Control id : ids) {
    if (ControlRect(id).Contains(x, y) && Enabled(id)) return id;
  }
  return kNone;
}

// Where the formation should be: Package at the first station, the import
// carries it to the second, and a finished setup (or an installed game) sends
// it to Play at the end of the rail.
float SetupWindow::ShipTarget() const {
  if (mode_ == Mode::Ready) return 1.0f;
  if (mode_ == Mode::Importing) return 0.5f * progress_ / 100.0f;
  return 0.0f;
}

int SetupWindow::Stage() const {
  if (mode_ == Mode::Ready) return 2;
  if (mode_ == Mode::Importing) return 1;
  return 0;
}

void SetupWindow::InvalidateStage() {
  RECT r = {static_cast<LONG>(10 * scale_), static_cast<LONG>(116 * scale_), static_cast<LONG>((kW - 10) * scale_),
            static_cast<LONG>(280 * scale_)};
  InvalidateRect(hwnd_, &r, FALSE);
}

// --------------------------------------------------------------- paint --
void SetupWindow::PaintText(Graphics& g, const std::wstring& text, Font* font, uint32_t color, RectF rect,
                            StringAlignment align, bool wrap) {
  if (text.empty()) return;
  StringFormat format;
  format.SetAlignment(align);
  format.SetLineAlignment(StringAlignmentNear);
  format.SetTrimming(wrap ? StringTrimmingWord : StringTrimmingEllipsisPath);
  if (!wrap) format.SetFormatFlags(StringFormatFlagsNoWrap);
  SolidBrush brush(Hex(color));
  g.DrawString(text.c_str(), -1, font, rect, &format, &brush);
}

// The ship silhouette used throughout Setup and in the icon: an arrowhead
// pointing right, with a cockpit glint and a small engine glow behind it.
void SetupWindow::PaintShip(Graphics& g, float cx, float cy, float length, uint32_t color) {
  PaintGlow(g, cx - length * 0.55f, cy, length * 0.32f, length * 0.16f, 170, pal::Ember);
  const float h = length * 0.55f;
  const PointF points[4] = {PointF(cx - length / 2, cy), PointF(cx + length * 0.18f, cy - h / 2),
                            PointF(cx + length / 2, cy), PointF(cx + length * 0.18f, cy + h / 2)};
  GraphicsPath path;
  path.AddPolygon(points, 4);
  FillGradient(g, path, RectF(cx - length / 2, cy - h / 2, length, h),
               {Mix(color, 0xFFFFFF, 0.45f), Hex(color), Mix(color, 0x000000, 0.4f)}, {0, 0.5f, 1}, 90);
  Pen pen(Mix(color, 0x000000, 0.65f), 1);
  g.DrawPath(&pen, &path);
  const PointF glint[3] = {PointF(cx - length * 0.12f, cy), PointF(cx + length * 0.2f, cy - h * 0.18f),
                           PointF(cx + length * 0.2f, cy + h * 0.18f)};
  SolidBrush shine(Hex(0xFFFFFF, 170));
  g.FillPolygon(&shine, glint, 3);
}

void SetupWindow::PaintButton(Graphics& g, Control id, const wchar_t* text, bool enabled, bool primary, Font* font) {
  const RectL r = ControlRect(id);
  GraphicsPath path;
  RoundedPath(path, r.x, r.y, r.w, r.h, 8);
  uint32_t text_color;
  if (!enabled) {
    SolidBrush fill(Hex(0xFFFFFF, 18));
    g.FillPath(&fill, &path);
    Pen pen(Hex(pal::Gold, 45), 1);
    g.DrawPath(&pen, &path);
    text_color = 0x7A6650;
  } else if (primary) {
    FillGradient(g, path, RectF(r.x, r.y, r.w, r.h), {Hex(0xFFD591), Hex(0xE08A3A), Hex(0xB45A1E)}, {0, 0.6f, 1}, 90);
    Pen gloss(Hex(0xFFFFFF, 140), 1);
    g.DrawLine(&gloss, r.x + 8, r.y + 1.5f, r.x + r.w - 8, r.y + 1.5f);
    text_color = 0x2A1206;
  } else {
    SolidBrush fill(Hex(0xFFFFFF, 22));
    g.FillPath(&fill, &path);
    Pen pen(Hex(pal::Gold, 120), 1);
    g.DrawPath(&pen, &path);
    text_color = 0xF3DFC4;
  }
  if (enabled) {
    if (pressed_ == id) {
      SolidBrush shade(Hex(0x000000, 50));
      g.FillPath(&shade, &path);
    } else if (hover_ == id) {
      SolidBrush shade(Hex(0xFFFFFF, 34));
      g.FillPath(&shade, &path);
    }
  }
  StringFormat center;
  center.SetAlignment(StringAlignmentCenter);
  center.SetLineAlignment(StringAlignmentCenter);
  center.SetFormatFlags(StringFormatFlagsNoWrap);
  SolidBrush brush(Hex(text_color));
  g.DrawString(text, -1, font, RectF(r.x, r.y, r.w, r.h), &center, &brush);
}

void SetupWindow::Paint(HDC dc) {
  RECT client;
  GetClientRect(hwnd_, &client);
  if (!backdrop_ || static_cast<int>(backdrop_->GetWidth()) != client.right ||
      static_cast<int>(backdrop_->GetHeight()) != client.bottom)
    BuildBackdrop(client.right, client.bottom);
  HDC memory = CreateCompatibleDC(dc);
  HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
  HGDIOBJ old = SelectObject(memory, bitmap);
  {
    Graphics g(memory);
    g.DrawImage(backdrop_.get(), 0, 0, client.right, client.bottom);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.ScaleTransform(scale_, scale_);

    // Lit rail up to the formation, with a warm glow under it.
    const float f = ship_f_;
    const float lit_x = kRailX0 + (kRailX1 - kRailX0) * f;
    if (lit_x > kRailX0 + 1) {
      Pen glow(Hex(pal::Ember, 70), 7);
      glow.SetStartCap(LineCapRound);
      glow.SetEndCap(LineCapRound);
      g.DrawLine(&glow, kRailX0, kRailY, lit_x, kRailY);
      Pen lit(Hex(pal::GoldHi), 2.5f);
      g.DrawLine(&lit, kRailX0, kRailY, lit_x, kRailY);
    }

    // Stations and their labels. The current step is cream and carries the
    // live percentage while importing.
    const int stage = Stage();
    const wchar_t* names[3] = {L"PACKAGE", L"IMPORT", L"PLAY"};
    StringFormat center;
    center.SetAlignment(StringAlignmentCenter);
    center.SetLineAlignment(StringAlignmentCenter);
    for (int i = 0; i < 3; ++i) {
      const float sx = kRailX0 + (kRailX1 - kRailX0) * i / 2.0f;
      const bool lit_station = f >= i / 2.0f - 0.002f || i <= stage;
      if (lit_station) PaintGlow(g, sx, kRailY, 16, 16, 150, pal::Ember);
      SolidBrush dot(lit_station ? Hex(pal::GoldHi) : Hex(0x1A0C06));
      g.FillEllipse(&dot, sx - 7.5f, kRailY - 7.5f, 15.0f, 15.0f);
      Pen ring(Hex(pal::Gold), 1.6f);
      g.DrawEllipse(&ring, sx - 7.5f, kRailY - 7.5f, 15.0f, 15.0f);
      std::wstring label = names[i];
      if (i < stage) label = std::wstring(kTick) + L" " + label;
      if (i == 1 && mode_ == Mode::Importing) label = std::wstring(L"IMPORT ") + kDot + L" " + std::to_wstring(progress_) + L"%";
      if (mode_ == Mode::Ready && i == 2) label = std::wstring(kTick) + L" PLAY";
      const uint32_t color = i == stage ? pal::Cream : lit_station ? pal::Gold : pal::Dim;
      SolidBrush brush(Hex(color));
      g.DrawString(label.c_str(), -1, station_.get(), RectF(sx - 70, kRailY + 14, 140, 18), &center, &brush);
    }

    // The formation: blue leads, red and green on the wings, yellow trailing,
    // joined by link beams like attached ships in the game.
    const float cx = kRailX0 + 30 + (kRailX1 - kRailX0 - 60) * f;
    const float cy = 176;
    Pen beam(Hex(pal::Beam, 150), 1.4f);
    const float ends[4][2] = {{30, 0}, {0, -28}, {0, 28}, {-30, 0}};
    for (const auto& end : ends) g.DrawLine(&beam, cx, cy, cx + end[0], cy + end[1]);
    PaintShip(g, cx - 30, cy, 38, pal::Yellow);
    PaintShip(g, cx, cy - 28, 38, pal::Red);
    PaintShip(g, cx, cy + 28, 38, pal::Green);
    PaintShip(g, cx + 30, cy, 38, pal::Blue);

    // Heading and hint.
    PaintText(g, heading_, title_.get(), pal::Cream, RectF(64, 310, 592, 28));
    PaintText(g, hint_, body_.get(), pal::Sand, RectF(64, 338, 592, 22));

    if (mode_ == Mode::Choosing) {
      for (int i = 0; i < static_cast<int>(candidates_.size()) && i < kMaxRows; ++i) {
        const RectL r = ControlRect(static_cast<Control>(kRow0 + i));
        GraphicsPath row;
        RoundedPath(row, r.x, r.y, r.w, r.h, 6);
        SolidBrush well(Hex(0x000000, hover_ == kRow0 + i ? 200 : 140));
        g.FillPath(&well, &row);
        Pen edge(Hex(pal::Gold, hover_ == kRow0 + i ? 150 : 70), 1);
        g.DrawPath(&edge, &row);
        Pen ring(Hex(pal::Gold), 1.4f);
        g.DrawEllipse(&ring, r.x + 8, r.y + 5.5f, 14.0f, 14.0f);
        const Source& s = candidates_[i];
        std::wstring text = (s.kind == SourceKind::PreviousInstall ? L"Previous install  " : L"") + s.path.wstring();
        PaintText(g, text, mono_.get(), pal::Cream, RectF(r.x + 32, r.y + 3, r.w - 112, 22));
        PaintText(g, FormatMB(s.bytes), small_.get(), pal::Sand, RectF(r.x + r.w - 82, r.y + 4, 76, 22), StringAlignmentFar);
      }
    } else {
      // The package field.
      GraphicsPath field;
      RoundedPath(field, 64, kFieldY, 442, kFieldH, 8);
      SolidBrush well(Hex(0x000000, 170));
      g.FillPath(&well, &field);
      Pen field_pen(Hex(pal::Gold, 80), 1);
      g.DrawPath(&field_pen, &field);
      PaintText(g, path_text_, mono_.get(), path_set_ ? pal::Cream : pal::Dim, RectF(74, kFieldY + 9, 422, 22));
      PaintText(g, status_, small_.get(), status_color_, RectF(64, 408, 592, 34), StringAlignmentNear, true);

      // Progress: phase on the left, live counts on the right.
      PaintText(g, phase_, caps_.get(), phase_color_, RectF(64, 444, 400, 18));
      PaintText(g, stats_, small_.get(), pal::Sand, RectF(356, 444, 300, 18), StringAlignmentFar);
      PaintText(g, detail_, small_.get(), pal::Sand, RectF(64, 462, 592, 18));
    }
    PaintButton(g, kBrowse, imported_now_ ? L"Imported" : L"Browse package", Enabled(kBrowse), false, button_.get());

    // Extras: a painted box that gets a tick, a title and a one-line explanation.
    Pen divider(Hex(pal::Gold, 40), 1);
    g.DrawLine(&divider, 64.0f, kExtrasDivider, 656.0f, kExtrasDivider);
    PaintText(g, std::wstring(L"OPTIONAL EXTRAS  ") + kDot + L"  APPLIED WHEN YOU PRESS PLAY", caps_.get(), pal::Gold,
              RectF(64, kExtrasDivider + 4, 592, 18));
    const bool on[3] = {want_.stage_select, want_.fullscreen, want_.shortcut};
    for (int i = 0; i < 3; ++i) {
      const float y = kExtrasFirst + i * kExtraPitch;
      const RectL r = ControlRect(static_cast<Control>(kExtra0 + i));
      if (hover_ == kExtra0 + i) {
        GraphicsPath hover;
        RoundedPath(hover, r.x, r.y, r.w, r.h, 6);
        SolidBrush shade(Hex(0xFFFFFF, 16));
        g.FillPath(&shade, &hover);
      }
      GraphicsPath box;
      RoundedPath(box, 64, y + 3, 22, 22, 6);
      if (on[i]) {
        FillGradient(g, box, RectF(64, y + 3, 22, 22), {Hex(0xFFD591), Hex(0xE08A3A)}, {0, 1}, 90);
        Pen tick(Hex(0x2A1206), 2.2f);
        tick.SetStartCap(LineCapRound);
        tick.SetEndCap(LineCapRound);
        tick.SetLineJoin(LineJoinRound);
        const PointF points[3] = {PointF(69, y + 14.5f), PointF(74, y + 19.5f), PointF(82, y + 8.5f)};
        g.DrawLines(&tick, points, 3);
      } else {
        SolidBrush off(Hex(0x000000, 150));
        g.FillPath(&off, &box);
        Pen edge(Hex(pal::Gold, 150), 1.2f);
        g.DrawPath(&edge, &box);
      }
      PaintText(g, kExtraDefs[i].title, row_title_.get(), pal::Cream, RectF(98, y - 1, 556, 20));
      PaintText(g, kExtraDefs[i].detail, row_detail_.get(), pal::Sand, RectF(98, y + 16, 556, 18));
    }

    // The two buttons.
    const wchar_t* left = mode_ == Mode::Importing ? (cancel_.load() ? L"Cancelling" : L"Cancel") : L"Close";
    PaintButton(g, kLeft, left, Enabled(kLeft), false, button_.get());
    PaintButton(g, kPlay, L"PLAY", Enabled(kPlay), true, play_.get());
  }
  BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
  SelectObject(memory, old);
  DeleteObject(bitmap);
  DeleteDC(memory);
}

// ---------------------------------------------------------------- flow --
void SetupWindow::Start() {
  std::wstring missing;
  installed_ = IsInstalled(p_, &missing);
  // The extras start unticked on a first setup, where an unticked box changes
  // nothing. Reopened with --setup they show what is set, and unticking one
  // switches it off.
  was_ = installed_ ? CurrentExtras(p_, options_.shortcut_folder) : Extras{};
  want_ = was_;
  if (!options_.package.empty()) {
    SelectPath(options_.package);
    return;
  }
  if (installed_) {
    mode_ = Mode::Ready;
    ship_f_ = ship_target_ = 1.0f;
    heading_ = L"Aegis Wing is set up in this folder";
    hint_ = L"Press Play, change your extras, or import your package again to repair the game data.";
    path_text_ = L"resources\\assets";
    path_set_ = true;
    status_ = std::wstring(kTick) + L"  Game data imported. Saves and settings are in resources\\userdata.";
    status_color_ = pal::Ok;
    phase_.clear();
    Invalidate();
    return;
  }
  hint_ = std::wstring(L"Looking beside Aegis Wing.exe") + kEllipsis;
  path_text_ = L"No package selected";
  path_set_ = false;
  Invalidate();
  UpdateWindow(hwnd_);
  std::vector<Source> found = DetectSources(p_);
  Log(L"Detected " + std::to_wstring(found.size()) + L" package(s) or previous install(s).");
  hint_ = L"The Xbox 360 Live Arcade file you own. Drag it here or browse for it.";
  if (found.size() == 1) {
    StartImport(found[0]);
  } else if (found.size() > 1) {
    candidates_ = found;
    if (candidates_.size() > kMaxRows) candidates_.resize(kMaxRows);
    mode_ = Mode::Choosing;
    heading_ = L"More than one was found. Choose the one to import:";
    Invalidate();
  } else {
    status_ = L"No package was found beside Aegis Wing.exe or in a \"Game Package\" folder.";
    status_color_ = pal::Sand;
    Invalidate();
  }
}

void SetupWindow::Browse() {
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
  DWORD flags = 0;
  dialog->GetOptions(&flags);
  dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_DONTADDTORECENT);
  dialog->SetTitle(L"Choose your Aegis Wing Xbox 360 package");
  COMDLG_FILTERSPEC filter = {L"Xbox 360 package (any file)", L"*.*"};
  dialog->SetFileTypes(1, &filter);
  IShellItem* folder = nullptr;
  if (SUCCEEDED(SHCreateItemFromParsingName(p_.root.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
    dialog->SetFolder(folder);
    folder->Release();
  }
  if (SUCCEEDED(dialog->Show(hwnd_))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        SelectPath(path);
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dialog->Release();
}

void SetupWindow::SelectPath(const fs::path& path) {
  if (mode_ == Mode::Importing || imported_now_) return;
  path_text_ = path.wstring();
  path_set_ = true;
  phase_.clear();
  stats_.clear();
  detail_.clear();
  show_bar_ = false;
  SetCursor(LoadCursorW(nullptr, IDC_WAIT));
  std::wstring why;
  std::vector<Source> found = SourcesIn(path, &why);
  if (found.empty()) {
    Log(L"Rejected " + path.wstring() + L": " + why);
    if (mode_ == Mode::Choosing) mode_ = Mode::Idle;
    if (installed_) mode_ = Mode::Ready;
    status_ = why;
    status_color_ = pal::Bad;
    Invalidate();
    return;
  }
  if (found.size() == 1) {
    StartImport(found[0]);
    return;
  }
  candidates_ = found;
  if (candidates_.size() > kMaxRows) candidates_.resize(kMaxRows);
  mode_ = Mode::Choosing;
  heading_ = L"That folder holds more than one. Choose the one to import:";
  Invalidate();
}

void SetupWindow::StartImport(Source source) {
  if (worker_.joinable()) worker_.join();
  mode_ = Mode::Importing;
  candidates_.clear();
  path_text_ = Describe(source);
  path_set_ = true;
  heading_ = L"Importing your game data";
  hint_ = L"Your package is only read, never changed. Nothing is installed outside this folder.";
  status_ = std::wstring(kTick) + L"  Aegis Wing  " + kDot + L"  " +
            (source.kind == SourceKind::PreviousInstall ? L"previous install" : L"Xbox LIVE Arcade package") +
            L"  " + kDot + L"  Title 5841083C  " + kDot + L"  " + FormatMB(source.bytes);
  status_color_ = pal::Ok;
  phase_ = L"PREPARING";
  phase_color_ = pal::Gold;
  stats_.clear();
  detail_.clear();
  progress_ = 0;
  show_bar_ = true;
  cancel_ = false;
  Invalidate();
  const fs::path saves_from = source.kind == SourceKind::PreviousInstall ? source.path : FindPreviousInstall(p_);
  HWND hwnd = hwnd_;
  worker_ = std::thread([this, hwnd, source, saves_from]() {
    ImportResult* result = new ImportResult(
        Import(p_, source, saves_from, cancel_, [hwnd](int percent, const std::wstring& phase, const std::wstring& detail) {
          PostMessageW(hwnd, WM_APP_PROGRESS, 0, reinterpret_cast<LPARAM>(new ProgressMsg{percent, phase, detail}));
        }));
    PostMessageW(hwnd, WM_APP_DONE, 0, reinterpret_cast<LPARAM>(result));
  });
}

void SetupWindow::OnProgress(ProgressMsg* msg) {
  if (mode_ == Mode::Importing) {
    progress_ = std::clamp(msg->percent, 0, 100);
    phase_ = msg->phase;
    stats_ = msg->detail;
    ship_target_ = ShipTarget();
    Invalidate();
  }
  delete msg;
}

void SetupWindow::OnDone(ImportResult* result) {
  if (worker_.joinable()) worker_.join();
  stats_.clear();
  if (result->ok) {
    mode_ = Mode::Ready;
    installed_ = true;
    imported_now_ = true;
    heading_ = L"Aegis Wing is ready";
    hint_ = L"Your package is no longer needed. Next time Aegis Wing.exe starts the game straight away.";
    progress_ = 100;
    phase_ = L"AEGIS WING IS READY";
    phase_color_ = pal::Ok;
    detail_ = result->migrated.empty() ? L"Game data imported into resources\\assets." : result->migrated;
    status_ = std::wstring(kTick) + L"  Imported and verified. Press Play when you are ready.";
    status_color_ = pal::Ok;
    if (GetForegroundWindow() != hwnd_) {
      FLASHWINFO flash = {sizeof(flash), hwnd_, FLASHW_ALL | FLASHW_TIMERNOFG, 0, 0};
      FlashWindowEx(&flash);
    }
  } else {
    mode_ = installed_ ? Mode::Ready : Mode::Idle;
    show_bar_ = false;
    phase_ = result->cancelled ? L"IMPORT CANCELLED" : L"IMPORT DID NOT COMPLETE";
    phase_color_ = result->cancelled ? pal::Gold : pal::Bad;
    status_ = result->message;
    status_color_ = result->cancelled ? pal::Sand : pal::Bad;
    detail_ = result->cancelled ? L"" : L"Your package was not changed. Details: resources\\runtime\\logs\\setup.log";
    heading_ = installed_ ? L"Aegis Wing is set up in this folder" : L"Choose your Aegis Wing package";
    hint_ = L"The Xbox 360 Live Arcade file you own. Drag it here or browse for it.";
  }
  ship_target_ = ShipTarget();
  delete result;
  cancel_ = false;
  Invalidate();
  if (close_after_import_) DestroyWindow(hwnd_);
}

void SetupWindow::Play() {
  if (GameRunning(p_)) {
    MessageBoxW(hwnd_, L"Aegis Wing is already running from this folder.", L"Aegis Wing", MB_OK | MB_ICONINFORMATION);
    return;
  }
  const std::wstring note = ApplyExtras(p_, want_, was_, options_.shortcut_folder, [this](const fs::path& link) {
    const std::wstring text = L"There is already an Aegis Wing shortcut on your desktop that opens a different copy:\n\n" +
                              link.wstring() + L"\n\nReplace it with one for this folder?";
    return MessageBoxW(hwnd_, text.c_str(), L"Aegis Wing", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
  });
  was_ = want_;
  if (!note.empty()) MessageBoxW(hwnd_, note.c_str(), L"Aegis Wing", MB_OK | MB_ICONINFORMATION);
  std::wstring error;
  if (!LaunchGame(p_, options_.game_args, &error)) {
    Log(L"FAILED to start the game: " + error);
    MessageBoxW(hwnd_, error.c_str(), L"Aegis Wing", MB_OK | MB_ICONERROR);
    return;
  }
  Log(L"Play: game started.");
  DestroyWindow(hwnd_);
}

void SetupWindow::RequestClose() {
  if (mode_ == Mode::Importing) {
    // Closing stops the import first; the window goes once it has cleaned up.
    close_after_import_ = true;
    cancel_ = true;
    Invalidate();
    return;
  }
  DestroyWindow(hwnd_);
}

void SetupWindow::Click(Control id) {
  switch (id) {
    case kBrowse: Browse(); break;
    case kLeft:
      if (mode_ == Mode::Importing) {
        cancel_ = true;
        Invalidate();
      } else {
        RequestClose();
      }
      break;
    case kPlay: Play(); break;
    case kExtra0: want_.stage_select = !want_.stage_select; break;
    case kExtra1: want_.fullscreen = !want_.fullscreen; break;
    case kExtra2: want_.shortcut = !want_.shortcut; break;
    default:
      if (id >= kRow0 && id - kRow0 < static_cast<int>(candidates_.size())) StartImport(candidates_[id - kRow0]);
      break;
  }
  Invalidate();
}

// ------------------------------------------------------------- messages --
LRESULT CALLBACK SetupWindow::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  SetupWindow* self = reinterpret_cast<SetupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    self = static_cast<SetupWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT SetupWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd_, &ps);
      Paint(dc);
      EndPaint(hwnd_, &ps);
      return 0;
    }
    case WM_PRINTCLIENT: Paint(reinterpret_cast<HDC>(wp)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DPICHANGED: ApplyScale(HIWORD(wp), reinterpret_cast<RECT*>(lp)); return 0;
    case WM_APP_START: Start(); return 0;
    case WM_APP_PROGRESS: OnProgress(reinterpret_cast<ProgressMsg*>(lp)); return 0;
    case WM_APP_DONE: OnDone(reinterpret_cast<ImportResult*>(lp)); return 0;
    case WM_TIMER:
      if (wp == kAnimationTimer) {
        // Eases the formation toward its target, so the ships glide as real
        // progress arrives. Only repaints while they are moving.
        ship_target_ = ShipTarget();
        const float delta = ship_target_ - ship_f_;
        if (std::fabs(delta) > 0.0006f) {
          ship_f_ += delta * 0.14f;
          InvalidateStage();
        } else if (ship_f_ != ship_target_) {
          ship_f_ = ship_target_;
          InvalidateStage();
        }
      }
      return 0;
    case WM_MOUSEMOVE: {
      if (!tracking_) {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd_, 0};
        tracking_ = TrackMouseEvent(&track) != FALSE;
      }
      const Control hit = HitTest(GET_X_LPARAM(lp) / scale_, GET_Y_LPARAM(lp) / scale_);
      if (hit != hover_) {
        hover_ = hit;
        Invalidate();
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      tracking_ = false;
      if (hover_ != kNone) {
        hover_ = kNone;
        Invalidate();
      }
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        SetCursor(LoadCursorW(nullptr, hover_ != kNone ? IDC_HAND : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN: {
      const Control hit = HitTest(GET_X_LPARAM(lp) / scale_, GET_Y_LPARAM(lp) / scale_);
      if (hit == kNone) {
        // Borderless window: drag it from anywhere that is not a control.
        ReleaseCapture();
        SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
      }
      pressed_ = hit;
      SetCapture(hwnd_);
      Invalidate();
      return 0;
    }
    case WM_LBUTTONUP: {
      const Control was = pressed_;
      pressed_ = kNone;
      ReleaseCapture();
      const Control hit = HitTest(GET_X_LPARAM(lp) / scale_, GET_Y_LPARAM(lp) / scale_);
      if (was != kNone && was == hit) Click(hit);
      Invalidate();
      return 0;
    }
    case WM_KEYDOWN:
      if (wp == VK_ESCAPE) RequestClose();
      if (wp == VK_RETURN && Enabled(kPlay)) Play();
      return 0;
    case WM_DROPFILES: {
      HDROP drop = reinterpret_cast<HDROP>(wp);
      wchar_t path[32768] = {};
      if (DragQueryFileW(drop, 0, path, 32768) > 0) SelectPath(path);
      DragFinish(drop);
      return 0;
    }
    case WM_CLOSE: RequestClose(); return 0;
    case WM_DESTROY:
      KillTimer(hwnd_, kAnimationTimer);
      if (worker_.joinable()) {
        cancel_ = true;
        worker_.join();
      }
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

int SetupWindow::Run(HINSTANCE instance) {
  ULONG_PTR token = 0;
  GdiplusStartupInput input;
  if (GdiplusStartup(&token, &input, nullptr) != Ok) return 1;
  CreateFonts();
  icon_ = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.style = CS_DROPSHADOW;
  wc.lpfnWndProc = Proc;
  wc.hInstance = instance;
  wc.hIcon = icon_;
  wc.hIconSm = icon_;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_ACCEPTFILES, kClassName, L"Aegis Wing Setup",
                          WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX, 0, 0, 100, 100, nullptr, nullptr, instance, this);
  if (!hwnd_) return 1;
  // Explorer runs unelevated like us, but allow drops from it either way.
  ChangeWindowMessageFilterEx(hwnd_, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
  ChangeWindowMessageFilterEx(hwnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
  ChangeWindowMessageFilterEx(hwnd_, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, nullptr);
  ApplyScale(GetDpiForWindow(hwnd_), nullptr);
  heading_ = L"Choose your Aegis Wing package";
  hint_ = L"The Xbox 360 Live Arcade file you own. Drag it here or browse for it.";
  SetTimer(hwnd_, kAnimationTimer, 16, nullptr);
  ShowWindow(hwnd_, SW_SHOWNORMAL);
  SetForegroundWindow(hwnd_);
  UpdateWindow(hwnd_);
  PostMessageW(hwnd_, WM_APP_START, 0, 0);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  const int code = installed_ ? 0 : 1;
  // Fonts and the backdrop are GDI+ objects: release them before GDI+ shuts down.
  backdrop_.reset();
  word_.reset(), tag_.reset(), station_.reset(), button_.reset(), play_.reset(), title_.reset(), body_.reset();
  small_.reset(), caps_.reset(), mono_.reset(), row_title_.reset(), row_detail_.reset();
  GdiplusShutdown(token);
  return code;
}

}  // namespace

int RunSetupWindow(HINSTANCE instance, const Paths& p, const SetupOptions& options) {
  Log(L"Setup opened" + (options.reason.empty() ? std::wstring() : L" (" + options.reason + L")"));
  SetupWindow window(p, options);
  return window.Run(instance);
}

}  // namespace aw

#include "aegis_wing_menu_pointer.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/function.h>
#include <rex/system/xmemory.h>
#include <rex/ui/virtual_key.h>

#include "generated/default/aegis_wing_init.h"

namespace aegis_wing {
namespace {

using rex::input::mnk::MnkInputDriver;
using rex::ui::VirtualKey;

// The game's front scene (every menu scene's shared init and "shown again"
// store themselves here).
constexpr uint32_t kFrontScene = 0x924C2614;
// XUI's handle table: +0 entries, +36 count. Entries are 12 bytes:
// u16 check, u16 flags (bit 0 = in use), u32 record. A handle is
// check << 16 | index.
constexpr uint32_t kHandleTable = 0x924B9B10;
// XUI's focused element: one shared slot, then one slot per player. Menus
// opened for a signed-in player keep their focus in that player's slot.
constexpr uint32_t kFocusShared = 0x924BAABC;
constexpr uint32_t kFocusPlayer0 = 0x924BAAC0;
constexpr uint32_t kSharedUser = 255;

// Layer records (one per class in an element's chain, most derived first):
// +0x04 next layer, +0x0C first layer, +0x18 class info (+0x04 its name),
// +0x20 that class's data. The XuiElement layer's data: +0x00 handle,
// +0x04 ID string, +0x0C width, +0x10 height, +0x18 first child,
// +0x20 next sibling, +0x2C x, +0x30 y (relative to the parent).
// XuiControl data keeps state flags at +0x10 (bit 0 clear while hidden or
// disabled); XuiList data keeps the first shown row at +0x80, the selection
// at +0x84 and the row count at +0x88.
constexpr uint32_t kRecordNext = 0x04;
constexpr uint32_t kRecordFirst = 0x0C;
constexpr uint32_t kRecordClass = 0x18;
constexpr uint32_t kRecordData = 0x20;
constexpr uint32_t kClassName = 0x04;
constexpr uint32_t kElementId = 0x04;
constexpr uint32_t kWidth = 0x0C;
constexpr uint32_t kHeight = 0x10;
constexpr uint32_t kFirstChild = 0x18;
constexpr uint32_t kNextSibling = 0x20;
constexpr uint32_t kX = 0x2C;
constexpr uint32_t kY = 0x30;
constexpr uint32_t kControlFlags = 0x10;
constexpr uint32_t kListTop = 0x80;
constexpr uint32_t kListSelection = 0x84;
constexpr uint32_t kListCount = 0x88;

// Menu rows are full-size buttons; the A/B legends along the bottom of a
// menu are buttons too, but about 35 tall. Pointing at a legend must not
// pull the focus off the menu.
constexpr float kMinRowHeight = 45.0f;
constexpr size_t kMaxElements = 1024;

class Guest {
 public:
  explicit Guest(rex::memory::Memory* memory) : memory_(memory) {}

  // Guest heap pointers only: anything else is not followed.
  static bool Plausible(uint32_t address) {
    return address >= 0x40000000u && address < 0xC0000000u && (address & 3) == 0;
  }
  uint32_t U32(uint32_t address) const {
    return rex::memory::load_and_swap<uint32_t>(
        memory_->TranslateVirtual<const uint32_t*>(address));
  }
  uint16_t U16(uint32_t address) const {
    return rex::memory::load_and_swap<uint16_t>(
        memory_->TranslateVirtual<const uint16_t*>(address));
  }
  float F32(uint32_t address) const {
    const uint32_t bits = U32(address);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }
  // Compares a guest UTF-16 string with ASCII text.
  bool TextIs(uint32_t address, const char* text) const {
    if (!Plausible(address & ~3u)) return false;
    for (uint32_t i = 0;; ++i) {
      const uint16_t c = U16(address + i * 2);
      if (c != uint8_t(text[i])) return false;
      if (c == 0) return true;
    }
  }
  uint32_t Record(uint32_t handle) const {
    const uint32_t table = U32(kHandleTable);
    const uint32_t count = U32(kHandleTable + 36);
    const uint32_t index = handle & 0xFFFF;
    if (!Plausible(table) || index >= count) return 0;
    const uint32_t entry = table + index * 12;
    if (!(U16(entry + 2) & 1) || U16(entry) != (handle >> 16)) return 0;
    const uint32_t record = U32(entry + 4);
    return Plausible(record) ? record : 0;
  }

 private:
  rex::memory::Memory* memory_;
};

struct Node {
  uint32_t base = 0;    // XuiElement data
  uint32_t handle = 0;  // the element's (most derived) handle
  uint32_t first = 0;   // first layer record
  float x = 0, y = 0, w = 0, h = 0;
  size_t end = 0;       // one past the last node of this subtree
  uint32_t list = 0;    // XuiList data, when this is a list
  bool row = false;     // a shown, enabled menu row (button)
  bool list_item = false;
};

uint32_t LayerData(const Guest& g, uint32_t first, const char* class_name) {
  for (uint32_t r = first; Guest::Plausible(r); r = g.U32(r + kRecordNext)) {
    if (g.TextIs(g.U32(g.U32(r + kRecordClass) + kClassName), class_name)) {
      const uint32_t data = g.U32(r + kRecordData);
      return Guest::Plausible(data) ? data : 0;
    }
  }
  return 0;
}

void Collect(const Guest& g, uint32_t base, float parent_x, float parent_y,
             std::vector<Node>* nodes) {
  if (!Guest::Plausible(base) || nodes->size() >= kMaxElements) return;
  const uint32_t record = g.Record(g.U32(base));
  const uint32_t first = record ? g.U32(record + kRecordFirst) : 0;
  if (!Guest::Plausible(first)) return;
  Node node;
  node.base = base;
  node.first = first;
  node.handle = g.U32(first);
  node.x = parent_x + g.F32(base + kX);
  node.y = parent_y + g.F32(base + kY);
  node.w = g.F32(base + kWidth);
  node.h = g.F32(base + kHeight);
  const uint32_t top_name = g.U32(g.U32(first + kRecordClass) + kClassName);
  // The "Are you sure?" box answers with two wide back buttons (Yes / No);
  // the B Back legends are back buttons too, but narrow and short.
  const bool answer_button = g.TextIs(top_name, "XuiBackButton") && node.h >= 40.0f &&
                             node.w >= 200.0f;
  if ((node.h >= kMinRowHeight &&
       (g.TextIs(top_name, "XuiButton") || g.TextIs(top_name, "XuiNavButton"))) ||
      answer_button) {
    const uint32_t control = LayerData(g, first, "XuiControl");
    node.row = control && (g.U32(control + kControlFlags) & 1) != 0;
  }
  node.list_item = g.TextIs(top_name, "XuiListItem");
  node.list = LayerData(g, first, "XuiList");
  const size_t index = nodes->size();
  nodes->push_back(node);
  for (uint32_t child = g.U32(base + kFirstChild);
       Guest::Plausible(child) && nodes->size() < kMaxElements;
       child = g.U32(child + kNextSibling)) {
    Collect(g, child, node.x, node.y, nodes);
  }
  (*nodes)[index].end = nodes->size();
}

bool Contains(const Node& n, float px, float py) {
  return px >= n.x && px < n.x + n.w && py >= n.y && py < n.y + n.h;
}

// A hidden list (off screen) that the scene mirrors with panels of its own:
// pointing at a panel picks that entry (or, with index -1, only makes the
// list the active row). A click on a value row steps it instead of pressing
// A, whose meaning there is "go on to the next screen". A panel is found by
// its element ID where it has one (the options scene moves its panels about
// between single player and LAN), else by a fixed rectangle.
struct Proxy {
  const char* scene;
  const char* panel;  // element ID, or nullptr to use the rectangle
  float x, y, w, h;
  const char* list;
  int index;
  bool click_steps;  // left half Left, right half Right
};
constexpr Proxy kProxies[] = {
    // Join LAN: Normal / Either / Insane.
    {"CCustomMatchScene", nullptr, 126, 177, 329, 304, "lstDifficulty", 0, false},
    {"CCustomMatchScene", nullptr, 485, 177, 314, 304, "lstDifficulty", 1, false},
    {"CCustomMatchScene", nullptr, 823, 177, 332, 304, "lstDifficulty", 2, false},
    // Single player / Create LAN: Normal / Insane, the private/public slots
    // row (LAN only), the level row.
    {"CGameOptionsScene", "imgEasy", 0, 0, 0, 0, "lstDifficulty", 0, false},
    {"CGameOptionsScene", "imgInsane", 0, 0, 0, 0, "lstDifficulty", 1, false},
    {"CGameOptionsScene", "imgMiddlePanel", 0, 0, 0, 0, "lstPublicSeats", -1, true},
    {"CGameOptionsScene", "imgBottomPanel", 0, 0, 0, 0, "lstLevels", -1, true},
};

const Node* FindById(const Guest& g, const std::vector<Node>& nodes, const char* id) {
  for (const Node& n : nodes) {
    if (g.TextIs(g.U32(n.base + kElementId), id)) return &n;
  }
  return nullptr;
}

struct Target {
  uint32_t focus = 0;  // element to give the focus to
  uint32_t list = 0;   // XuiList data to step, or 0
  int index = -1;      // entry to step it to
  bool vertical = false;
  uint16_t click = 0;  // left-click pad key override (0 = A)
};

// The list whose subtree holds `nodes[i]`, as an index into `nodes`.
int OwningList(const std::vector<Node>& nodes, size_t i) {
  int owner = -1;
  for (size_t j = 0; j < i; ++j) {
    if (nodes[j].list && i < nodes[j].end) owner = int(j);
  }
  return owner;
}

bool FindTarget(const Guest& g, const std::vector<Node>& nodes, float px, float py,
                Target* target) {
  if (nodes.empty()) return false;
  // Scene-specific panels first.
  const Node& scene = nodes.front();
  const uint32_t scene_class = g.U32(g.U32(scene.first + kRecordClass) + kClassName);
  for (const Proxy& p : kProxies) {
    if (!g.TextIs(scene_class, p.scene)) continue;
    float x = p.x, y = p.y, w = p.w, h = p.h;
    if (p.panel) {
      const Node* panel = FindById(g, nodes, p.panel);
      if (!panel) continue;
      x = panel->x; y = panel->y; w = panel->w; h = panel->h;
      // Single player hides the slots row: its panel is then overlapped by
      // the difficulty panels, which move down into its place.
      if (g.TextIs(g.U32(panel->base + kElementId), "imgMiddlePanel")) {
        const Node* easy = FindById(g, nodes, "imgEasy");
        if (easy && easy->y + easy->h > y) continue;
      }
    }
    if (px < x || px >= x + w || py < y || py >= y + h) continue;
    const Node* list = FindById(g, nodes, p.list);
    if (!list || !list->list) continue;
    target->focus = list->handle;
    target->list = list->list;
    target->index = p.index;
    if (p.click_steps) {
      target->click = static_cast<uint16_t>(px < x + w * 0.5f
                                                ? VirtualKey::kXInputPadLThumbLeft
                                                : VirtualKey::kXInputPadLThumbRight);
    }
    return true;
  }
  // Menu rows, topmost (last drawn) first.
  for (size_t i = nodes.size(); i-- > 0;) {
    if (nodes[i].row && Contains(nodes[i], px, py)) {
      target->focus = nodes[i].handle;
      return true;
    }
  }
  // Rows of a list shown on screen (the game list): the entry is the list's
  // first shown row plus this row's place among the shown rows.
  for (size_t i = nodes.size(); i-- > 0;) {
    if (!nodes[i].list_item || !Contains(nodes[i], px, py)) continue;
    const int owner = OwningList(nodes, i);
    if (owner < 0) return false;
    const Node& list = nodes[size_t(owner)];
    std::vector<const Node*> rows;
    for (size_t j = size_t(owner) + 1; j < list.end; ++j) {
      if (nodes[j].list_item && OwningList(nodes, j) == owner) rows.push_back(&nodes[j]);
    }
    float min_x = 1e9f, max_x = -1e9f;
    for (const Node* r : rows) {
      min_x = std::min(min_x, r->x);
      max_x = std::max(max_x, r->x);
    }
    const bool vertical = (max_x - min_x) < 1.0f;
    std::sort(rows.begin(), rows.end(), [vertical](const Node* a, const Node* b) {
      return vertical ? a->y < b->y : a->x < b->x;
    });
    const auto it = std::find(rows.begin(), rows.end(), &nodes[i]);
    target->focus = list.handle;
    target->list = list.list;
    target->index = int(g.U32(list.list + kListTop)) + int(it - rows.begin());
    target->vertical = vertical;
    return true;
  }
  return false;
}

// What the pointer asked a list for, kept until the list has stepped there.
struct Pending {
  uint32_t scene = 0;
  uint32_t list = 0;
  int index = -1;
  bool vertical = false;
} pending;

uint32_t last_moves = 0;
// Where the pointer was last seen. The first report only records it: a
// window that opens under a resting cursor gets a "move" without anyone
// touching the mouse, and that must not take the menu's default focus.
bool have_last_position = false;
int32_t last_x = 0;
int32_t last_y = 0;

void StepPending(const Guest& g) {
  if (!pending.list || pending.index < 0) return;
  if (g.U32(kFrontScene) != pending.scene) {
    pending = {};
    return;
  }
  // One step at a time: wait for the last one to be read.
  if (MnkInputDriver::QueuedKeystrokes() > 0) return;
  const int count = int(g.U32(pending.list + kListCount));
  const int selection = int(g.U32(pending.list + kListSelection));
  if (pending.index >= count || pending.index == selection) {
    pending = {};
    return;
  }
  const bool forward = pending.index > selection;
  const VirtualKey key = pending.vertical
                             ? (forward ? VirtualKey::kXInputPadLThumbDown
                                        : VirtualKey::kXInputPadLThumbUp)
                             : (forward ? VirtualKey::kXInputPadLThumbRight
                                        : VirtualKey::kXInputPadLThumbLeft);
  MnkInputDriver::InjectPadKeystroke(static_cast<uint16_t>(key));
}

}  // namespace

bool IsLevelSceneInFront(rex::memory::Memory* memory) {
  if (!memory) return false;
  const Guest g(memory);
  const uint32_t scene = g.U32(kFrontScene);
  if (!Guest::Plausible(scene)) return false;
  const uint32_t record = g.Record(g.U32(scene + 4));
  if (!record) return false;
  const uint32_t first = g.U32(record + kRecordFirst);
  if (!Guest::Plausible(first)) return false;
  return g.TextIs(g.U32(g.U32(first + kRecordClass) + kClassName), "CIngameMenu");
}

void UpdateMenuPointer(rex::memory::Memory* memory, bool menu_up) {
  const MnkInputDriver::Pointer pointer = MnkInputDriver::GetPointer();
  if (!menu_up || !memory) {
    pending = {};
    MnkInputDriver::SetLeftClickPadKey(0);
    last_moves = pointer.moves;
    return;
  }
  const Guest g(memory);
  StepPending(g);

  // Only movement points: a cursor left resting on a row must not take the
  // focus back from the keys, the wheel or a pad.
  if (pointer.moves == last_moves) return;
  last_moves = pointer.moves;
  const bool moved = have_last_position && (pointer.x != last_x || pointer.y != last_y);
  have_last_position = true;
  last_x = pointer.x;
  last_y = pointer.y;
  if (!moved || pointer.width <= 0 || pointer.height <= 0) return;

  // The game draws its 1280x720 frame letterboxed into the window.
  const float scale = std::min(pointer.width / 1280.0f, pointer.height / 720.0f);
  if (scale <= 0.0f) return;
  const float px = (pointer.x - (pointer.width - 1280.0f * scale) * 0.5f) / scale;
  const float py = (pointer.y - (pointer.height - 720.0f * scale) * 0.5f) / scale;

  const uint32_t scene = g.U32(kFrontScene);
  if (!Guest::Plausible(scene)) return;
  uint32_t record = g.Record(g.U32(scene + 4));
  if (!record) return;
  while (Guest::Plausible(g.U32(record + kRecordNext))) record = g.U32(record + kRecordNext);
  std::vector<Node> nodes;
  nodes.reserve(256);
  Collect(g, g.U32(record + kRecordData), 0.0f, 0.0f, &nodes);

  Target target;
  const bool found_target = FindTarget(g, nodes, px, py, &target);
  if (!found_target) {
    MnkInputDriver::SetLeftClickPadKey(0);
    return;
  }
  MnkInputDriver::SetLeftClickPadKey(target.click);

  // Give the focus to the row (or the list), unless it - or, for a list, one
  // of its rows - already has it. The menu's own player slot wins when used.
  const uint32_t player_focus = g.U32(kFocusPlayer0);
  const uint32_t user = player_focus ? 0u : kSharedUser;
  const uint32_t focused = player_focus ? player_focus : g.U32(kFocusShared);
  bool has_focus = focused == target.focus;
  if (!has_focus && target.list) {
    for (size_t i = 0; i < nodes.size() && !has_focus; ++i) {
      if (nodes[i].handle != target.focus) continue;
      for (size_t j = i; j < nodes[i].end; ++j) {
        if (nodes[j].handle == focused) has_focus = true;
      }
      break;
    }
  }
  if (!has_focus) {
    // XUI's own "set user focus": the highlight, the row's description and
    // the focus sounds all follow as if the stick had moved there.
    rex::ppc::GuestToHostFunction<uint32_t>(sub_923B9048, target.focus, user);
  }
  if (target.list && target.index >= 0) {
    pending.scene = scene;
    pending.list = target.list;
    pending.index = target.index;
    pending.vertical = target.vertical;
    StepPending(g);
  }
}

}  // namespace aegis_wing

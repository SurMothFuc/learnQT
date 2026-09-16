#include "UiDiagnostics.h"

#include <algorithm>

// 独立编译单元：任何使用 UiSlotTimer 的目标都只需要这个文件，不必链接整个 EditorController。
UiSlotTimer::~UiSlotTimer()
{
    auto &diagnostics = UiDiagnostics::instance();
    const double ms = clock.nsecsElapsed() / 1e6;
    diagnostics.slotMs[slot] += ms;
    diagnostics.maxSlotMs[slot] = std::max(diagnostics.maxSlotMs[slot], ms);
}

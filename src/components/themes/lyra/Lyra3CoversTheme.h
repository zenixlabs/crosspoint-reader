#pragma once

#include <HalClock.h>

#include "components/icons/bookmark.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace Lyra3CoversMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeRecentBooksCount = 3;
  v.homeTopPadding = 51;
  v.controlRadius = 2;
  v.listRowRadius = 2, 
  v.popupCornerRadius = 2, 
  v.controlRadius = 2, 
  v.sheetRadius = 2, 
  v.capsuleRadius = 2;
  v.listSelectionStyle = 0;

  const int pageHeight = 792;
  const int available =
      pageHeight - 2 * v.homeTopPadding - v.buttonHintsHeight - v.menuRowHeight - 8;  // 8 -> hPaddingInSelection

  v.homeCoverTileHeight = available / v.homeRecentBooksCount;
  v.homeCoverHeight = v.homeCoverTileHeight - 2 * 8;
  return v;
}();
}  // namespace Lyra3CoversMetrics

class Lyra3CoversTheme : public LyraTheme {
 public:
  int homeCoverThumbHeight(const GfxRenderer& renderer) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;

  void drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage, const int pageCount,
                     std::string title, const int paddingBottom, const int textYOffset, const bool fillMargin,
                     const bool isPageBookmarked, const bool pageCountEstimated) const override;

  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;  // Only bc Lyra theme used a different cornerRadius.

  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawEmptyRecents(const GfxRenderer& renderer, const Rect rect) const override;

 private:
  void drawCovers(GfxRenderer& renderer, const RecentBook& book, const int coverX, const int coverY,
                  const int coverWidth, const int coverHeight, const bool wideCovers, const int cornerRadius) const;
  void drawPctBanner(GfxRenderer& renderer, const int coverX, const int coverY, const int coverWidth,
                     const int coverHeight, const int pct, const bool wideCovers, const int cornerRadius) const;
};

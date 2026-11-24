#include "Lyra3CoversTheme.h"

#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/bookmark.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"
#include "util/BookProgress.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int mainMenuIconSize = 32;  // was 32
constexpr int bookmarkStatusIconWidth = 16;
constexpr int bookmarkStatusIconHeight = 14;
constexpr int bookmarkStatusIconGap = 4;
constexpr int bookmarkStatusIconTopCrop = 2;
constexpr int authorPadding = 6;
constexpr int tuckIn = 15;
constexpr int titleAccommodate = Lyra3CoversMetrics::values.homeTopPadding;
const int coverHeight = Lyra3CoversMetrics::values.homeCoverTileHeight - 2 * hPaddingInSelection;

const uint8_t* iconForName(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Plugins:
      return BlocksIcon;
    case UIIcon::Wifi:
      return WifiIcon;
    case UIIcon::Hotspot:
      return HotspotIcon;
    case UIIcon::Bookmark:
      return BookmarkIcon;
    case UIIcon::Blocks:
      return BlocksIcon;
    default:
      return nullptr;
  }
}

void drawBookmarkStatusIcon(const GfxRenderer& renderer, const int x, const int y) {
  constexpr int bytesPerRow = bookmarkStatusIconWidth / 8;
  for (int row = 0; row < bookmarkStatusIconHeight; ++row) {
    for (int col = 0; col < bookmarkStatusIconWidth; ++col) {
      const uint8_t byte = BookmarkStatusIcon[(row + bookmarkStatusIconTopCrop) * bytesPerRow + col / 8];
      const uint8_t mask = 1U << (7 - (col % 8));
      renderer.drawPixel(x + col, y + row, (byte & mask) != 0);
    }
  }
}

struct CropHeader {
  uint32_t magic;
  int32_t w, h;  // no x/y anymore
  uint32_t orientation;
  uint32_t bytes;
};

std::string cropPathFor(const std::string& thumbPath, int x, int y) {
  return thumbPath + ".crop" + std::to_string(x % 8) + std::to_string(y % 8);
}

constexpr uint32_t CROP_MAGIC = 0x43524F50;  // "CROP"

bool loadCropCache(GfxRenderer& r, const std::string& path, int x, int y, int w, int h) {
  HalFile f;
  if (!Storage.openFileForRead("HOME", path, f)) return false;
  CropHeader hd{};
  bool ok = f.read(reinterpret_cast<uint8_t*>(&hd), sizeof(hd)) == sizeof(hd) && hd.magic == CROP_MAGIC && hd.w == w &&
            hd.h == h && hd.orientation == static_cast<uint32_t>(r.getOrientation()) &&
            hd.bytes == r.getRegionByteSize(x, y, w, h);
  uint8_t* buf = ok ? static_cast<uint8_t*>(malloc(hd.bytes)) : nullptr;
  ok = buf && f.read(buf, hd.bytes) == static_cast<int>(hd.bytes) && r.copyBufferToRegion(x, y, w, h, buf, hd.bytes);
  free(buf);
  f.close();
  return ok;
}

void saveCropCache(GfxRenderer& r, const std::string& path, int x, int y, int w, int h) {
  const size_t bytes = r.getRegionByteSize(x, y, w, h);
  uint8_t* buf = bytes ? static_cast<uint8_t*>(malloc(bytes)) : nullptr;
  if (!buf) return;
  if (r.copyRegionToBuffer(x, y, w, h, buf, bytes)) {
    HalFile f;
    if (Storage.openFileForWrite("HOME", path, f)) {
      const CropHeader hd{CROP_MAGIC, w, h, static_cast<uint32_t>(r.getOrientation()), static_cast<uint32_t>(bytes)};
      f.write(reinterpret_cast<const uint8_t*>(&hd), sizeof(hd));
      f.write(buf, bytes);
      f.close();
    }
  }
  free(buf);
}

}  // namespace

int Lyra3CoversTheme::homeCoverThumbHeight(const GfxRenderer& renderer) const {
  // Thumbs generate at ~0.6 aspect, so ask for enough
  // height that the resulting width fills the slot (crop the excess height).
  const bool wideCovers = SETTINGS.coverSlices == 1;
  if (wideCovers) {
    return static_cast<int>(coverHeight * 4.2);
  }
  return static_cast<int>(coverHeight * 1.1);
}

void Lyra3CoversTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                       const char* btn4) const {
  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  if (gpio.hasTouch()) {
    return;
  }

  const int pageWidth = renderer.getScreenWidth();
  const int buttonWidth = pageWidth / 4;
  const int buttonHeight = Lyra3CoversMetrics::values.buttonHintsHeight;
  const int buttonY = renderer.getScreenHeight() - buttonHeight;
  const int labelHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int labelY = buttonY + std::max(0, buttonHeight - labelHeight) / 2;

  const int buttonPositions[] = {0, buttonWidth, 2 * buttonWidth, 3 * buttonWidth};
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool grayscale = renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      if (grayscale) continue;
      renderer.fillRect(x, buttonY, buttonWidth, buttonHeight, false);
      const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
      const int labelX = x + std::max(0, buttonWidth - labelWidth) / 2;
      renderer.drawText(SMALL_FONT_ID, labelX, labelY, labels[i], true);
    } else {
      renderer.fillRect(x, buttonY, buttonWidth, buttonHeight, false);
    }
  }
  renderer.fillRect(0, buttonY, buttonWidth * 4, 3, true);
  renderer.setOrientation(orig_orientation);
}

void Lyra3CoversTheme::drawPctBanner(GfxRenderer& renderer, const int coverX, const int coverY, const int coverWidth,
                                     const int coverHeight, const int pct, const bool wideCovers,
                                     const int cornerRadius) const {
  char bannerLabel[8];
  snprintf(bannerLabel, sizeof(bannerLabel), "%d%%", pct);
  const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, bannerLabel);
  const int labelHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int bannerHeight = labelHeight + 10;
  const int bannerWidth = renderer.getTextWidth(SMALL_FONT_ID, "100%") + 10;
  int bannerX = coverX + coverWidth - bannerWidth;
  int bannerY = coverY + coverHeight - bannerHeight;
  int labelX = bannerX + (bannerWidth - labelWidth) / 2;
  int labelY = bannerY + 5;
  bool labelColor = true;

  if (wideCovers) {
    constexpr int iconWidth = 63;  // odd number for centered tip
    constexpr int iconHeight = 65;
    const int iconX = coverX + coverWidth - iconWidth - 30;
    const int iconY = coverY - 3;
    const int wedgeHeight = 12;
    const int vertices = 5;

    // paint the frmae 1px wider/taller than the icon
    const int frameX[] = {iconX - 1, iconX + iconWidth, iconX + iconWidth, iconX + iconWidth / 2, iconX - 1};
    const int frameY[] = {iconY, iconY, iconY + iconHeight, iconY + iconHeight - wedgeHeight, iconY + iconHeight};
    renderer.fillPolygon(frameX, frameY, 5, false);  // white outline layer

    const int badgeX[] = {iconX, iconX + iconWidth - 1, iconX + iconWidth - 1, iconX + iconWidth / 2, iconX};
    const int badgeY[] = {iconY, iconY, iconY + iconHeight - 1, iconY + iconHeight - wedgeHeight - 1,
                          iconY + iconHeight - 1};
    renderer.fillPolygon(badgeX, badgeY, 5, true);  // black badge on top

    labelX = iconX + (std::max(0, iconWidth - labelWidth)) / 2;
    labelY = iconY + (iconHeight - wedgeHeight - labelHeight) / 2;
    labelColor = false;
  } else {
    renderer.fillRoundedRect(bannerX, bannerY, bannerWidth, bannerHeight, cornerRadius, true, false, false, true,
                             Color::White);  // black border
    renderer.drawRoundedRect(bannerX, bannerY, bannerWidth, bannerHeight, 1, cornerRadius, true, false, false, true,
                             true);  // white separator line
  }
  renderer.drawText(SMALL_FONT_ID, labelX, labelY, bannerLabel,
                    labelColor);  // white text
}

void Lyra3CoversTheme::drawCovers(GfxRenderer& renderer, const RecentBook& book, const int coverX, const int coverY,
                                  const int coverWidth, const int coverHeight, const bool wideCovers,
                                  const int cornerRadius) const {
  std::string coverPath = book.coverBmpPath;

  bool hasCover = false;
  if (!coverPath.empty()) {
    const std::string thumbPath = UITheme::getCoverThumbPath(coverPath, homeCoverThumbHeight(renderer));

    std::string cropPath;
    if (wideCovers) {
      cropPath = cropPathFor(thumbPath, coverX, coverY);
      hasCover = loadCropCache(renderer, cropPath, coverX, coverY, coverWidth, coverHeight);
    }

    if (!hasCover) {
      HalFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          hasCover = drawCoverThumbFill(renderer, bitmap, Rect{coverX, coverY, coverWidth, coverHeight});
          if (hasCover && wideCovers) saveCropCache(renderer, cropPath, coverX, coverY, coverWidth, coverHeight);
        }
        file.close();
      }
    }
  }

  // Draw either way
  renderer.drawRect(coverX, coverY, coverWidth, coverHeight, true);

  if (!hasCover) {
    drawCoverPlaceholder(renderer, Rect{coverX, coverY, coverWidth, coverHeight});
  }

  const int pct = std::max(0, book.progress);
  if (pct > 0) {
    drawPctBanner(renderer, coverX, coverY, coverWidth, coverHeight, pct, wideCovers, cornerRadius);
  }

  renderer.drawRoundedRect(coverX, coverY, coverWidth, coverHeight, 1, cornerRadius, true);
  renderer.maskRoundedRectOutsideCorners(coverX, coverY, coverWidth, coverHeight, cornerRadius, Color::White);
}

void Lyra3CoversTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const int tileWidth = rect.width - 2 * Lyra3CoversMetrics::values.contentSidePadding;
  const int tileHeight = rect.height;
  const int tileX = rect.x + Lyra3CoversMetrics::values.contentSidePadding;
  const bool hasContinueReading = !recentBooks.empty();
  const bool wideCovers = SETTINGS.coverSlices == 1;

  int cornerRadius = 8;
  int coverWidth = tileWidth - 2 * hPaddingInSelection;
  ;
  if (!wideCovers) {
    cornerRadius = Lyra3CoversMetrics::values.controlRadius;
    coverWidth = static_cast<int>(coverHeight * 0.7f);
  }

  // Draw page title first
  const char* title = SETTINGS.homeTitle[0] ? SETTINGS.homeTitle : tr(STR_LITTLEFINGER);  
  renderer.drawCenteredText(UI_12_FONT_ID, Lyra3CoversMetrics::values.homeTopPadding,  
                          title, true, EpdFontFamily::BOLD);                            

  // Draw book card regardless, fill with message based on `hasContinueReading`
  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading) {
    const int recentBooksCount = recentBooks.size();
    const int maxCardCount = std::min(Lyra3CoversMetrics::values.homeRecentBooksCount, recentBooksCount);

    if (!coverRendered) {
      for (int i = 0; i < maxCardCount; i++) {
        const int tileY = rect.y + titleAccommodate + i * tileHeight;
        drawCovers(renderer, recentBooks[i], tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                   coverHeight, wideCovers, cornerRadius);
      }
      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
    }

    for (int i = 0; i < maxCardCount; i++) {
      const int tileY = rect.y + titleAccommodate + i * tileHeight;
      RecentBook book = recentBooks[i];
      bool bookSelected = (selectorIndex == i);

      if (bookSelected) {
        // Draw selection box
        renderer.drawRoundedRect(tileX, tileY, tileWidth, tileHeight, 2, cornerRadius, true);
      }

      if (!wideCovers) {
        // Title and author
        const int titleFontId = UI_10_FONT_ID;
        const int authorFontId = UI_10_FONT_ID;
        int textWidth =
            tileWidth - 2 * hPaddingInSelection - 2 * Lyra3CoversMetrics::values.verticalSpacing - coverWidth;
        const int textX = tileX + hPaddingInSelection + coverWidth + Lyra3CoversMetrics::values.verticalSpacing;

        auto titleLines = renderer.wrappedText(titleFontId, book.title.c_str(), textWidth, 3, EpdFontFamily::BOLD);
        auto author = renderer.truncatedText(authorFontId, book.author.c_str(), textWidth);

        const int titleLineHeight = renderer.getLineHeight(titleFontId);
        const int titleBlockHeight = titleLineHeight * static_cast<int>(titleLines.size());
        const int authorLineHeight = book.author.empty() ? 0 : renderer.getLineHeight(authorFontId);
        const int totalBlockHeight = titleBlockHeight + authorPadding + authorLineHeight;
        int textY = tileY + tileHeight / 2 - totalBlockHeight / 2;

        for (const auto& line : titleLines) {
          renderer.drawText(titleFontId, textX, textY, line.c_str(), true, EpdFontFamily::BOLD);
          textY += titleLineHeight;
        }
        if (!book.author.empty()) {
          textY += authorPadding;
          renderer.drawText(authorFontId, textX, textY, author.c_str(), true);
        }
      }
    }
  } else {
    drawEmptyRecents(renderer, rect);
  }
}

void Lyra3CoversTheme::drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage,
                                     const int pageCount, std::string title, const int paddingBottom,
                                     const int textYOffset, const bool fillMargin, const bool isPageBookmarked,
                                     const bool pageCountEstimated) const {
  auto metrics = UITheme::getInstance().getMetrics();
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  const auto sb = SETTINGS.statusBarSpec();
  const bool showStatusBarTextLane = sb.textLaneVisible(halClock.isAvailable());

  const auto screenHeight = renderer.getScreenHeight();

  const int sbHeight = UITheme::getInstance().getStatusBarHeight() + 11;
  const int textY =
      screenHeight - sbHeight - orientedMarginBottom - paddingBottom + BoardConfig::ACTIVE.statusBarOffsetY;
  int sbFontId;
  if (sb.serifFont) {
    sbFontId = CUSTOM1_FONT_ID;
  } else {
    sbFontId = SMALL_FONT_ID;
  }

  // Do not show title on page 1 of chapter
  if (currentPage == 1) {
    title = "";
  }

  // ---- Layout constants ----
  constexpr int sideMargin = 30;          // margin on either side of the centered block
  constexpr int batteryTextGap = 6;       // gap between battery icon and battery percentage text
  constexpr int estimateGap = 2;          // gap between "~" and the progress text
  constexpr int separatorGap = 8;         // pixels of empty space on each side of the separator dot
  constexpr int separatorDotNudge = 0;    // shift the dot left (-) or right (+) if the glyph looks off-center
  const char* separatorDot = "\xC2\xB7";  // U+00B7 middle dot
  const int separatorDotWidth = renderer.getTextWidth(sbFontId, separatorDot);
  const int separatorWidth = separatorGap * 2 + separatorDotWidth;

  // Space available for the whole block
  const int availableWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight - 2 * sideMargin;

  // Left cluster (rendered left to right): bookmark | battery | clock(left)
  // Right cluster (rendered left to right): clock(right) | progress
  // Title sits between the two clusters.
  int leftClusterWidth = 0;
  int rightClusterWidth = 0;

  // Adds an item's width to a cluster, inserting a separator before it if the cluster already has content.
  auto addToCluster = [&](int& clusterWidth, const int itemWidth) {
    clusterWidth += (clusterWidth > 0 ? separatorWidth : 0) + itemWidth;
  };

  // ---- Bookmark (left cluster) ----
  const bool showBookmark = showStatusBarTextLane && isPageBookmarked;
  if (showBookmark) {
    addToCluster(leftClusterWidth, bookmarkStatusIconWidth);
  }

  // ---- Battery (left cluster) ----
  const bool showBatteryPercentage = sb.showBatteryPercent;
  std::string batteryText;
  int batteryTextWidth = 0;
  int batteryBlockWidth = 0;

  if (sb.showBattery) {
    batteryBlockWidth = metrics.batteryWidth;
    if (showBatteryPercentage) {
      batteryText = std::to_string(powerManager.getBatteryPercentage()) + "%";
      batteryTextWidth = renderer.getTextWidth(sbFontId, batteryText.c_str());
      batteryBlockWidth += batteryTextGap + batteryTextWidth;
    }
    addToCluster(leftClusterWidth, batteryBlockWidth);
  }

  // ---- Clock (any board whose RTC probe succeeded) ----
  char timeBuf[9] = {0};
  bool showClock = false;
  bool clockOnLeft = false;
  int clockTextWidth = 0;

  if (sb.showsClock() && halClock.isAvailable()) {
    if (halClock.formatTime(timeBuf, sizeof(timeBuf), sb.clock12h)) {
      clockTextWidth = renderer.getTextWidth(sbFontId, timeBuf);
      if (sb.clockMode == CrossPointSettings::STATUS_BAR_CLOCK_LEFT) {
        showClock = true;
        clockOnLeft = true;
        addToCluster(leftClusterWidth, clockTextWidth);
      } else if (sb.clockMode == CrossPointSettings::STATUS_BAR_CLOCK_RIGHT) {
        showClock = true;
        clockOnLeft = false;
        addToCluster(rightClusterWidth, clockTextWidth);
      }
    }
  }

  // ---- Progress text elements (right cluster) ----
  const bool showPageCount = sb.showChapterPageCount;
  const bool showPercent = sb.showBookProgressPercent;
  const bool showEstimate = showPageCount && pageCountEstimated;

  char pageStr[24] = {0};
  char percentStr[12] = {0};
  int pageStrWidth = 0;
  int percentStrWidth = 0;
  int estimateWidth = 0;
  int estimateSpacing = 0;

  if (showPageCount) {
    snprintf(pageStr, sizeof(pageStr), "%d/%d", currentPage, pageCount);
    pageStrWidth = renderer.getTextWidth(sbFontId, pageStr);
    estimateWidth = showEstimate ? renderer.getTextWidth(UI_10_FONT_ID, "~") : 0;
    estimateSpacing = showEstimate ? estimateGap : 0;
    addToCluster(rightClusterWidth, estimateWidth + estimateSpacing + pageStrWidth);
  }

  if (showPercent) {
    snprintf(percentStr, sizeof(percentStr), "%.0f%%", bookProgress);
    percentStrWidth = renderer.getTextWidth(sbFontId, percentStr);
    addToCluster(rightClusterWidth, percentStrWidth);
  }

  // ---- Progress bar (independent of the centered block) ----
  if (sb.showsProgressBar()) {
    const int barMarginLeft = fillMargin ? 0 : orientedMarginLeft;
    const int barMarginRight = fillMargin ? 0 : orientedMarginRight;
    const int progressBarMaxWidth = renderer.getScreenWidth() - barMarginLeft - barMarginRight;
    const int progressBarY = renderer.getScreenHeight() - orientedMarginBottom - sb.progressBarHeightPx -
                             paddingBottom + (fillMargin ? 1 : 0);
    size_t progress;
    if (sb.progressBarMode == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::BOOK_PROGRESS) {
      progress = std::min<size_t>(static_cast<size_t>(bookProgress), 100);
    } else {
      // Chapter progress
      progress = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) * 100 : 0;
    }
    const int barWidth = progressBarMaxWidth * progress / 100;
    const int barHeight = sb.progressBarHeightPx + (fillMargin ? orientedMarginBottom - 1 : 0);
    renderer.fillRect(barMarginLeft, progressBarY, barWidth, barHeight, true);
  }

  // ---- Title: sized last, using whatever space is left ----
  int titleWidth = 0;
  if (!title.empty()) {
    // Separators needed between the title and each non-empty cluster
    const int titleSepWidth =
        (leftClusterWidth > 0 ? separatorWidth : 0) + (rightClusterWidth > 0 ? separatorWidth : 0);
    int availableTitleSpace = availableWidth - leftClusterWidth - rightClusterWidth - titleSepWidth;

    if (availableTitleSpace <= 0) {
      title.clear();
    } else {
      titleWidth = renderer.getTextWidth(sbFontId, title.c_str());
      if (titleWidth > availableTitleSpace) {
        title = renderer.truncatedText(sbFontId, title.c_str(), availableTitleSpace);
        titleWidth = renderer.getTextWidth(sbFontId, title.c_str());
      }
    }
  }

  // ---- Total block width ----
  // Items in order: [left cluster] [title] [right cluster], with a separator between each non-empty part.
  int totalWidth = leftClusterWidth;
  if (titleWidth > 0) {
    totalWidth += (totalWidth > 0 ? separatorWidth : 0) + titleWidth;
  }
  if (rightClusterWidth > 0) {
    totalWidth += (totalWidth > 0 ? separatorWidth : 0) + rightClusterWidth;
  }

  // ---- Render the block, centered ----
  int x = orientedMarginLeft + sideMargin + (availableWidth - totalWidth) / 2;
  bool drewAnything = false;

  // Draws the dot with a fixed gap on each side and advances x past the whole separator slot.
  auto drawSeparator = [&]() {
    renderer.drawText(sbFontId, x + separatorGap + separatorDotNudge, textY, separatorDot);
    x += separatorWidth;
  };

  auto beginItem = [&]() {
    if (drewAnything) {
      drawSeparator();
    }
    drewAnything = true;
  };

  // 1. Bookmark
  if (showBookmark) {
    beginItem();
    drawBookmarkStatusIcon(renderer, x, textY + 5);
    x += bookmarkStatusIconWidth;
  }

  // 2. Battery (icon, 6px gap, then percentage text; no separator between them)
  if (sb.showBattery) {
    beginItem();
    // Draw the icon only; the percentage text is drawn manually to control the gap.
    GUI.drawBatteryLeft(renderer, Rect{x, textY, metrics.batteryWidth, metrics.batteryHeight}, false);
    x += metrics.batteryWidth;
    if (showBatteryPercentage) {
      x += batteryTextGap;
      renderer.drawText(sbFontId, x, textY, batteryText.c_str());
      x += batteryTextWidth;
    }
  }

  // 3. Clock (left mode)
  if (showClock && clockOnLeft) {
    beginItem();
    renderer.drawText(sbFontId, x, textY, timeBuf);
    x += clockTextWidth;
  }

  // 4. Title
  if (titleWidth > 0) {
    beginItem();
    renderer.drawText(sbFontId, x, textY - textYOffset, title.c_str());
    x += titleWidth;
  }

  // 5. Clock (right mode)
  if (showClock && !clockOnLeft) {
    beginItem();
    renderer.drawText(sbFontId, x, textY, timeBuf);
    x += clockTextWidth;
  }

  // 6. Chapter page count
  if (showPageCount) {
    beginItem();
    if (showEstimate) {
      const int estimateY = textY + (renderer.getLineHeight(sbFontId) - renderer.getLineHeight(UI_10_FONT_ID)) / 2;

      renderer.drawText(UI_10_FONT_ID, x, estimateY, "~");
      x += estimateWidth + estimateSpacing;
    }
    renderer.drawText(sbFontId, x, textY, pageStr);
    x += pageStrWidth;
  }

  // 7. Book progress percent
  if (showPercent) {
    beginItem();
    renderer.drawText(sbFontId, x, textY, percentStr);
    x += percentStrWidth;
  }
}

void Lyra3CoversTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                      const std::function<std::string(int index)>& buttonLabel,
                                      const std::function<UIIcon(int index)>& rowIcon) const {
  const int pageHeight = renderer.getScreenHeight();
  const int tileHeight = Lyra3CoversMetrics::values.menuRowHeight;
  const int tileWidth = tileHeight * 3 / 2;
  const int buttonRowX = rect.x + ((rect.width - tileWidth * buttonCount) / 2);
  const int buttonRowY = pageHeight - Lyra3CoversMetrics::values.buttonHintsHeight - hPaddingInSelection - tileHeight;

  for (int i = 0; i < buttonCount; ++i) {
    Rect tileRect = Rect{buttonRowX + i * tileWidth, buttonRowY, tileWidth, tileHeight};

    const bool selected = selectedIndex == i;
    const bool wideCovers = SETTINGS.coverSlices == 1;

    if (i < buttonCount - 1) {
      renderer.fillRoundedRect(tileRect.x + tileWidth, tileRect.y + tuckIn, 1, tileHeight - 2 * tuckIn, 0,
                               Color::LightGray);
    }

    if (selected) {
      renderer.drawRoundedRect(tileRect.x - 1, tileRect.y + 5, tileWidth + 2, tileHeight - 10, 2,
                               ((SETTINGS.coverSlices == 1) ? 8 : (Lyra3CoversMetrics::values.controlRadius)), true);
    }

    int iconX = tileRect.x + (tileWidth - mainMenuIconSize) / 2;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int iconY = tileRect.y + (tileHeight - mainMenuIconSize) / 2;

    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = iconForName(icon);
      if (iconBitmap != nullptr) {
        renderer.drawIcon(iconBitmap, iconX, iconY, mainMenuIconSize);
      }
    }
  }
}

void Lyra3CoversTheme::drawEmptyRecents(const GfxRenderer& renderer, const Rect rect) const {
  renderer.drawCenteredText(UI_10_FONT_ID, rect.y + rect.height / 2 - renderer.getLineHeight(UI_10_FONT_ID) - 2,
                            tr(STR_NO_OPEN_BOOK), true, EpdFontFamily::BOLD);
}

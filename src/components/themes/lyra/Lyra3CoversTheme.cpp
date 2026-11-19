#include "Lyra3CoversTheme.h"

#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
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
constexpr int cornerRadius = 8;
constexpr int mainMenuIconSize = 32;  // was 32
constexpr int bookmarkStatusIconWidth = 16;
constexpr int bookmarkStatusIconHeight = 14;
constexpr int bookmarkStatusIconGap = 4;
constexpr int bookmarkStatusIconTopCrop = 2;
constexpr int authorPadding = 10;
constexpr int progBarPadding = 6;
constexpr int progBarHeight = 8;
constexpr int tuckIn = 15;
int coverWidth = 0;

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
}  // namespace

void Lyra3CoversTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                       const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  constexpr int buttonHeight = Lyra3CoversMetrics::values.buttonHintsHeight;
  constexpr int buttonY = Lyra3CoversMetrics::values.buttonHintsHeight;  // Distance from bottom
  // Keyed to the portrait panel width: the 528-wide X3 gets more spacing than
  // the 480-wide boards (X4, X4 Pro, and the other 800x480 panels).
  constexpr int narrowButtonPositions[] = {58, 146, 254, 342};
  constexpr int wideButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool grayscale = renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();
  constexpr int buttonUnderlineHeight = 6;

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      // Draw the filled background and border for a FULL-sized button
      renderer.fillRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, cornerRadius,
                               grayscale ? Color::Black : Color::White);
      if (grayscale) continue;
      drawHintLabel(renderer, SMALL_FONT_ID, labels[i], x, buttonWidth, pageHeight - buttonY - buttonUnderlineHeight,
                    buttonHeight);
      renderer.fillRect(x, pageHeight - buttonUnderlineHeight, buttonWidth, 2, true);
    } else {
      // Draw the filled background and border for a SMALL-sized button
      renderer.fillRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, cornerRadius,
                               grayscale ? Color::Black : Color::White);
    }
  }

  renderer.setOrientation(orig_orientation);
}

void Lyra3CoversTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const int tileWidth = rect.width - 2 * Lyra3CoversMetrics::values.contentSidePadding;
  const int tileHeight = rect.height;
  const bool hasContinueReading = !recentBooks.empty();
  const int coverHeight = Lyra3CoversMetrics::values.homeCoverTileHeight - 2 * hPaddingInSelection;

  // Draw book card regardless, fill with message based on `hasContinueReading`
  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading) {
    const int recentBooksCount = recentBooks.size();
    const int maxCardCount = std::min(Lyra3CoversMetrics::values.homeRecentBooksCount, recentBooksCount);

    if (!coverRendered) {
      for (int i = 0; i < maxCardCount; i++) {
        if (coverWidth == 0) {
          coverWidth = coverHeight * 0.6;
        }
        const int tileY = rect.y + i * tileHeight;

        RecentBook book = recentBooks[i];
        std::string coverPath = book.coverBmpPath;
        bool hasCover = true;
        int tileX = rect.x + Lyra3CoversMetrics::values.contentSidePadding;
        if (coverPath.empty()) {
          hasCover = false;
        } else {
          const std::string coverBmpPath = UITheme::getCoverThumbPath(coverPath, coverHeight);

          // First time: load cover from SD and render
          HalFile file;
          if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
            Bitmap bitmap(file);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
              coverWidth = bitmap.getWidth();
              // Narrow covers come out taller than the slot; fill 1:1 and crop
              // vertically instead of rescaling the dither.
              drawCoverThumbFill(
                  renderer, bitmap,
                  Rect{tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth, coverHeight});
            } else {
              hasCover = false;
            }
            file.close();
          }
        }

        // Draw either way
        renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth, coverHeight, true);

        if (!hasCover) {
          drawCoverPlaceholder(renderer,
                               Rect{tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth, coverHeight});
        }
        renderer.drawRoundedRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth, coverHeight, 1,
                                 cornerRadius, true);
        renderer.maskRoundedRectOutsideCorners(tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                                               coverHeight, cornerRadius, Color::White);
      }
      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
    }

    for (int i = 0; i < maxCardCount; i++) {
      const int tileY = rect.y + i * tileHeight;
      RecentBook book = recentBooks[i];
      bool bookSelected = (selectorIndex == i);

      int tileX = rect.x + Lyra3CoversMetrics::values.contentSidePadding;
      int textWidth = tileWidth - 2 * hPaddingInSelection - Lyra3CoversMetrics::values.verticalSpacing - coverWidth;

      if (bookSelected) {
        // Draw selection box
        renderer.fillRoundedRect(tileX, tileY, tileWidth, hPaddingInSelection, cornerRadius, true, true, false, false,
                                 Color::LightGray);
        renderer.fillRectDither(tileX, tileY + hPaddingInSelection, hPaddingInSelection, coverHeight, Color::LightGray);
        renderer.fillRectDither(tileX + hPaddingInSelection + coverWidth, tileY + hPaddingInSelection,
                                tileWidth - hPaddingInSelection - coverWidth, coverHeight, Color::LightGray);
        renderer.fillRoundedRect(tileX, tileY + coverHeight + hPaddingInSelection, tileWidth, hPaddingInSelection,
                                 cornerRadius, false, false, true, true, Color::LightGray);
        renderer.maskRoundedRectOutsideCorners(tileX + hPaddingInSelection, tileY + hPaddingInSelection, coverWidth,
                                               coverHeight, cornerRadius, Color::LightGray);
      }

      auto titleLines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), textWidth, 3, EpdFontFamily::BOLD);

      auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
      const uint16_t progPct = static_cast<uint16_t>(std::max(0, book.progress));

      const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
      const int titleBlockHeight = titleLineHeight * static_cast<int>(titleLines.size());
      const int authorLineHeight = book.author.empty() ? 0 : renderer.getLineHeight(UI_10_FONT_ID);

      const bool hasProgress = progPct > 0;
      const int progBarWidth = textWidth - Lyra3CoversMetrics::values.verticalSpacing;
      const int progBarLabelHeight = renderer.getLineHeight(SMALL_FONT_ID);
      const int progBlockHeight = progBarPadding + progBarLabelHeight + progBarHeight + 2;

      const int totalBlockHeight =
          titleBlockHeight + authorPadding + authorLineHeight + (hasProgress ? progBlockHeight : 0);

      int titleY = tileY + tileHeight / 2 - totalBlockHeight / 2;

      if (hasProgress) {
        titleY = tileY + tuckIn;
      }
      const int textX = tileX + hPaddingInSelection + coverWidth + Lyra3CoversMetrics::values.verticalSpacing;
      for (const auto& line : titleLines) {
        renderer.drawText(UI_12_FONT_ID, textX, titleY, line.c_str(), true, EpdFontFamily::BOLD);
        titleY += titleLineHeight;
      }
      if (!book.author.empty()) {
        titleY += authorPadding;
        renderer.drawText(UI_10_FONT_ID, textX, titleY, author.c_str(), true);
        titleY += authorLineHeight;
      }

      if (hasProgress) {
        titleY = tileY + tileHeight - tuckIn - progBlockHeight;
        const int progressBarY = titleY + progBarLabelHeight + 2;
        const int filledWidth = std::clamp(static_cast<int>((progPct / 100.0f) * progBarWidth), 0, progBarWidth);
        char progressLabel[16];
        snprintf(progressLabel, sizeof(progressLabel), "%i%% %s", progPct, tr(STR_READ));
        renderer.drawText(SMALL_FONT_ID, textX, titleY, progressLabel, true);
        renderer.drawRoundedRect(textX, progressBarY, progBarWidth, progBarHeight, 2, 4, true);
        if (filledWidth > 0) {
          renderer.fillRoundedRect(textX + 1, progressBarY + 1, std::max(0, filledWidth - 2),
                                   std::max(0, progBarHeight - 2), 4, Color::Black);
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

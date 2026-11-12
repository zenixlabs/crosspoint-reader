#include "XtcReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace fui = freeink::ui;

XtcReaderChapterSelectionActivity::XtcReaderChapterSelectionActivity(GfxRenderer& renderer,
                                                                     MappedInputManager& mappedInput,
                                                                     const std::shared_ptr<Xtc>& xtc,
                                                                     const uint32_t currentPage)
    : UiListActivity("XtcReaderChapterSelection", renderer, mappedInput), xtc(xtc), currentPage(currentPage) {}

int XtcReaderChapterSelectionActivity::findChapterIndexForPage(const uint32_t page) const {
  if (!xtc) {
    return 0;
  }

  const auto& chapters = xtc->getChapters();
  const auto chapter = std::find_if(chapters.begin(), chapters.end(), [page](const auto& candidate) {
    return page >= candidate.startPage && page <= candidate.endPage;
  });
  return chapter != chapters.end() ? static_cast<int>(chapter - chapters.begin()) : 0;
}

void XtcReaderChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();

  if (!xtc) {
    return;
  }

  buildRowItems();

  // Open on the current chapter, which may sit past the first page; the first
  // screen build pulls the viewport to it (ListNav follow-on-build).
  nav.selected = findChapterIndexForPage(currentPage);
}

// Derives rowItems from the xtc's chapters. Called once from onEnter() since
// chapters are static for this screen's lifetime.
void XtcReaderChapterSelectionActivity::buildRowItems() {
  const auto& chapters = xtc->getChapters();
  rowItems.clear();
  rowValues.clear();
  rowItems.reserve(chapters.size());
  rowValues.reserve(chapters.size());
  for (const auto& chapter : chapters) {
    fui::ListItem item;
    item.label = chapter.name.empty() ? tr(STR_UNNAMED) : chapter.name.c_str();
    rowValues.push_back(std::to_string(chapter.startPage + 1));
    item.value = rowValues.back().c_str();
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
  }
}

void XtcReaderChapterSelectionActivity::activateIndex(const int index) {
  // The activated row leaves this screen (finish); a lingering flash would
  // gray an unrelated element on the next render.
  app.clearTapFlash();
  const auto& chapters = xtc->getChapters();
  if (!chapters.empty() && index >= 0 && index < static_cast<int>(chapters.size())) {
    nav.selected = index;
    setResult(PageResult{chapters[index].startPage});
    finish();
  }
}

bool XtcReaderChapterSelectionActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }

  if (!xtc) {
    return true;  // no book: nothing else to route this pass
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void XtcReaderChapterSelectionActivity::buildScreen(UiScreen& screen) {
  if (!xtc) {
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Content: the safe area minus the header band drawChrome paints the title in.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  // Progress summary where the old sub-header band sat.
  std::string progressLine = "";
  const int bookPageCount = static_cast<int>(xtc->getPageCount());
  const int bookPage = static_cast<int>(currentPage) + 1;
  // const float bookPercent = bookPageCount > 0 ? (bookPage * 100.0f) / bookPageCount : 0.0f;
  // char percentStr[8];
  // snprintf(percentStr, sizeof(percentStr), "%.0f", bookPercent);

  int chapterPage = bookPage, chapterTotal = bookPageCount;  // fallback: no chapters
  if (xtc->hasChapters()) {
    const auto& chapters = xtc->getChapters();
    const auto it = std::find_if(chapters.begin(), chapters.end(), [this](const xtc::ChapterInfo& c) {
      return currentPage >= c.startPage && currentPage <= c.endPage;
    });
    if (it != chapters.end() && it->endPage >= it->startPage) {
      chapterPage = static_cast<int>(currentPage - it->startPage) + 1;
      chapterTotal = static_cast<int>(it->endPage - it->startPage) + 1;
    }
  }
  if (chapterTotal > 0) {
    // progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::string(tr(STR_PAGE)) + std::to_string(chapterPage) +
    // "/" + std::to_string(chapterTotal);

    const int32_t chapterTimeLeft = xtc->getTimeRemaining(chapterTotal - chapterPage + 1, false);
    std::string chapterTimeLeftStr =
        StringUtils::formatDuration(static_cast<uint32_t>(chapterTimeLeft)) + std::string(tr(STR_LEFT));
    progressLine += std::string(tr(STR_CHAPTER_PREFIX)) + chapterTimeLeftStr + std::string(tr(STR_SEPARATOR));
  }

  const int32_t bookTimeLeft = xtc->getTimeRemaining(bookPageCount - bookPage + 1, true);
  std::string bookTimeLeftStr =
      StringUtils::formatDuration(static_cast<uint32_t>(bookTimeLeft)) + std::string(tr(STR_LEFT));
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + bookTimeLeftStr;

  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight));
  const int16_t pad = screen.theme().headerSidePadding;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), progressLine.c_str(), screen.theme().smallText);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rowItems.empty()) {
    screen.centeredText(tr(STR_NO_CHAPTERS), screen.theme().bodyText);
    return;
  }

  // rowItems is built once in onEnter() (see buildRowItems()) and reused
  // here on every repaint.
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void XtcReaderChapterSelectionActivity::drawChrome() {
  std::string title = xtc->getTitle();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight}, title.c_str());
}

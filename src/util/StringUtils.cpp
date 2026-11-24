#include "StringUtils.h"

#include <Utf8.h>
#include <strings.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace StringUtils {

namespace {
constexpr size_t MAX_PRESERVED_EXTENSION_BYTES = 16;

bool hasConventionalExtension(const std::string& name, const size_t dot) {
  const size_t extensionBytes = name.size() - dot;
  if (dot == 0 || extensionBytes < 2 || extensionBytes > MAX_PRESERVED_EXTENSION_BYTES) return false;
  return std::all_of(name.begin() + dot + 1, name.end(), [](const unsigned char c) { return std::isalnum(c) != 0; });
}

std::string sanitizeFilenameRange(const std::string& name, const size_t endOffset, const size_t contentMaxBytes,
                                  const size_t reserveBytes) {
  std::string result;
  result.reserve(std::min(name.size(), reserveBytes));

  const auto* text = reinterpret_cast<const unsigned char*>(name.c_str());
  const auto* rangeEnd = text + std::min(name.size(), endOffset);

  // Skip leading spaces and dots so they don't consume the byte budget
  while (text < rangeEnd && (*text == ' ' || *text == '.')) {
    text++;
  }

  // Process full UTF-8 codepoints to avoid trimming in the middle of a multibyte sequence
  while (text < rangeEnd) {
    const auto* cpStart = text;
    uint32_t cp = utf8NextCodepoint(&text);

    if (cp == '/' || cp == '\\' || cp == ':' || cp == '*' || cp == '?' || cp == '"' || cp == '<' || cp == '>' ||
        cp == '|') {
      // Replace illegal and control characters with '_'
      if (result.length() + 1 > contentMaxBytes) break;
      result += '_';
    } else if (cp >= 128 || (cp >= 32 && cp < 127)) {
      const size_t cpBytes = text - cpStart;
      if (result.length() + cpBytes > contentMaxBytes) break;
      result.append(reinterpret_cast<const char*>(cpStart), cpBytes);
    }
  }

  // Trim trailing spaces and dots
  size_t end = result.find_last_not_of(" .");
  if (end != std::string::npos) {
    result.resize(end + 1);
  } else {
    result.clear();
  }

  return result.empty() ? "book" : result;
}
}  // namespace

static bool isPunctOrSpace(const unsigned char c) { return std::ispunct(c) || std::isspace(c); }

static size_t skipSpaces(const std::string& s, size_t i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
  return i;
}

// Returns the remainder if non-empty, otherwise the original name.
std::string sanitizeChapterName(const std::string& name) {
  const size_t n = name.size();
  if (n == 0) return name;

  auto rest = [&](size_t i) -> std::string { return i < n ? name.substr(i) : name; };

  // 1. "12. Title" -> "Title"   (digits, then at least one space/punct)
  {
    size_t i = 0;
    while (i < n && std::isdigit(static_cast<unsigned char>(name[i]))) i++;
    if (i > 0) {
      const size_t sepStart = i;
      while (i < n && isPunctOrSpace(static_cast<unsigned char>(name[i]))) i++;
      if (i > sepStart && i < n) return name.substr(i);
    }
  }

  // 2. "IV. Title" / "XIV - Title"   (roman numerals, then at least one punct, optional spaces)
  {
    size_t i = 0;
    while (i < n && std::strchr("IVXLCDM", name[i]) && name[i] != '\0') i++;
    if (i > 0) {
      const size_t punctStart = i;
      while (i < n && std::ispunct(static_cast<unsigned char>(name[i]))) i++;
      if (i > punctStart) {
        i = skipSpaces(name, i);
        if (i < n) return name.substr(i);
      }
    }
  }

  // 3/4. "Chapter X <sep> Title"  /  "Part X <sep> Title"  /  "Chapter twenty" (whole string)
  for (const char* word : {"chapter", "part"}) {
    const size_t wl = std::strlen(word);
    if (n <= wl || strncasecmp(name.c_str(), word, wl) != 0) continue;
    if (!std::isspace(static_cast<unsigned char>(name[wl]))) continue;

    size_t i = skipSpaces(name, wl);
    const size_t tokStart = i;
    while (i < n && (std::isalnum(static_cast<unsigned char>(name[i])) || name[i] == '-')) i++;
    if (i == tokStart) continue;

    // whole-string form: "chapter twenty" -> "twenty" (letters/hyphens only)
    if (i == n) {
      bool alphaOnly = true;
      for (size_t k = tokStart; k < n; k++) {
        if (!std::isalpha(static_cast<unsigned char>(name[k])) && name[k] != '-') alphaOnly = false;
      }
      if (alphaOnly) return name.substr(tokStart);
      continue;
    }

    // "Chapter X - Title": optional spaces, one punct/space, then at least one space, then the title
    size_t j = skipSpaces(name, i);
    if (j < n && isPunctOrSpace(static_cast<unsigned char>(name[j]))) {
      j++;
      const size_t spStart = j;
      j = skipSpaces(name, j);
      if (j > spStart && j < n) return name.substr(j);
    }
  }

  return name;
}

std::string formatDuration(const uint32_t seconds) {
  if (!seconds || seconds == 0) {
    return "calc. time";
  }

  const uint32_t h = seconds / 3600;
  uint32_t m = (seconds % 3600) / 60;
  const uint32_t s = seconds % 60;
  if (s > 30) {
    m++;
  }

  char buf[24];
  if (h > 0 && m == 0) {
    snprintf(buf, sizeof(buf), "%luh", (unsigned long)h);
  } else if (h > 0 && m != 0) {
    snprintf(buf, sizeof(buf), "%luh %lum", (unsigned long)h, (unsigned long)m);
  } else if (m > 0) {
    snprintf(buf, sizeof(buf), "%lum", (unsigned long)m);
  } else {
    snprintf(buf, sizeof(buf), "<1m");
  }
  return buf;
}

std::string sanitizeFilename(const std::string& name, size_t maxBytes) {
  return sanitizeFilenameRange(name, name.size(), maxBytes, maxBytes);
}

std::string sanitizeFilenamePreservingExtension(const std::string& name, const size_t maxBytes) {
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos || !hasConventionalExtension(name, dot)) return sanitizeFilename(name, maxBytes);

  const size_t extensionBytes = name.size() - dot;
  if (extensionBytes >= maxBytes) return sanitizeFilename(name, maxBytes);

  std::string stem = sanitizeFilenameRange(name, dot, maxBytes - extensionBytes, maxBytes);
  stem.append(name, dot, extensionBytes);
  return stem;
}

}  // namespace StringUtils

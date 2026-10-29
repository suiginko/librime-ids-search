//
// ids_text.cc — UTF-8 / IDS 文本与候选注释帮助函数。
//
#include "ids_text.h"

#include <cstdio>

#include "ids_store.h"

namespace ids_search {

namespace {

const char* kBlockNames[17] = {
    "基本",     "扩A",      "扩B",      "扩C",  "扩D", "扩E",
    "扩F",      "扩G",      "扩H",      "扩I",  "扩J", "兼容区",
    "兼容补充", "部首补充", "康熙部首", "笔画", "CJK"};

}  // namespace

uint32_t DecodeNext(const std::string& s, size_t* i) {
  size_t n = s.size();
  if (*i >= n)
    return 0;
  unsigned char b1 = static_cast<unsigned char>(s[*i]);
  if (b1 < 0x80) {
    *i += 1;
    return b1;
  }
  if (b1 < 0xE0) {
    if (*i + 1 >= n)
      return 0;
    uint32_t b2 = static_cast<unsigned char>(s[*i + 1]);
    *i += 2;
    return (b1 - 0xC0) * 0x40 + (b2 - 0x80);
  }
  if (b1 < 0xF0) {
    if (*i + 2 >= n)
      return 0;
    uint32_t b2 = static_cast<unsigned char>(s[*i + 1]);
    uint32_t b3 = static_cast<unsigned char>(s[*i + 2]);
    *i += 3;
    return (b1 - 0xE0) * 0x1000 + (b2 - 0x80) * 0x40 + (b3 - 0x80);
  }
  if (*i + 3 >= n)
    return 0;
  uint32_t b2 = static_cast<unsigned char>(s[*i + 1]);
  uint32_t b3 = static_cast<unsigned char>(s[*i + 2]);
  uint32_t b4 = static_cast<unsigned char>(s[*i + 3]);
  *i += 4;
  return (b1 - 0xF0) * 0x40000 + (b2 - 0x80) * 0x1000 +
         (b3 - 0x80) * 0x40 + (b4 - 0x80);
}

std::string CpToUtf8(uint32_t cp) {
  std::string out;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return out;
}

std::vector<std::string> Utf8Chars(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    size_t old = i;
    uint32_t cp = DecodeNext(s, &i);
    if (i == old)
      break;
    if (cp == 0)
      break;
    out.push_back(s.substr(old, i - old));
  }
  return out;
}

size_t Utf8Length(const std::string& s) {
  return Utf8Chars(s).size();
}

bool IsIdc(uint32_t cp) {
  return cp >= 0x2FF0 && cp <= 0x2FFB;
}

bool HasWhitespace(const std::string& s) {
  for (unsigned char c : s) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      return true;
  }
  return false;
}

std::string Join(const std::vector<std::string>& parts,
                 const std::string& sep) {
  std::string r;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i)
      r += sep;
    r += parts[i];
  }
  return r;
}

std::string FormatIds(const std::string& raw_ids, const std::string& format) {
  if (format == "unicode" || format.empty())
    return raw_ids;
  std::string out;
  size_t i = 0;
  size_t n = raw_ids.size();
  while (i < n) {
    size_t old = i;
    uint32_t cp = DecodeNext(raw_ids, &i);
    if (IsIdc(cp)) {
      if (format == "bracket") {
        static const char* kIdcLabels[] = {
            "[左右]",   "[上下]",   "[左中右]", "[上中下]",
            "[全包]",   "[上包]",   "[下包]",   "[左包]",
            "[左上包]", "[右上包]", "[左下包]", "[交叉]"};
        out += kIdcLabels[cp - 0x2FF0];
      } else if (format == "plain") {
        // 省略结构符
      } else {
        out += raw_ids.substr(old, i - old);
      }
    } else {
      out += raw_ids.substr(old, i - old);
    }
  }
  return out;
}

const char* BlockName(int block) {
  return (block >= 0 && block < 17) ? kBlockNames[block] : "基本";
}

std::string BuildComment(IdsStore& store,
                         uint32_t id,
                         bool show_pinyin,
                         bool show_ids,
                         bool show_unicode,
                         bool show_block,
                         const std::string& ids_format) {
  const uint8_t* py = nullptr;
  const uint8_t* ids = nullptr;
  uint16_t py_len = 0;
  uint16_t ids_len = 0;
  store.Cold(id, &py, &py_len, &ids, &ids_len);
  const HotRec& hot = store.Hot(id);
  std::vector<std::string> parts;
  if (show_pinyin && py_len > 0) {
    parts.push_back(std::string(reinterpret_cast<const char*>(py), py_len));
  }
  if (show_ids && ids_len > 0) {
    parts.push_back(FormatIds(
        std::string(reinterpret_cast<const char*>(ids), ids_len), ids_format));
  }
  uint32_t cp = store.CpOfId(id);
  if (show_unicode) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", cp);
    parts.push_back(buf);
  }
  if (show_block)
    parts.push_back(BlockName(hot.block));
  return Join(parts, " ");
}

std::string IdsOfText(IdsStore& store, const std::string& text) {
  std::vector<std::string> parts;
  for (const auto& ch : Utf8Chars(text)) {
    size_t i = 0;
    uint32_t cp = DecodeNext(ch, &i);
    uint32_t id = store.IdOfCp(cp);
    if (id == kInvalidId) {
      parts.push_back(ch);
      continue;
    }
    const uint8_t* py = nullptr;
    const uint8_t* ids = nullptr;
    uint16_t py_len = 0;
    uint16_t ids_len = 0;
    store.Cold(id, &py, &py_len, &ids, &ids_len);
    if (ids_len > 0) {
      parts.push_back(std::string(reinterpret_cast<const char*>(ids), ids_len));
    } else {
      parts.push_back(ch);
    }
  }
  return Join(parts, " ");
}

}  // namespace ids_search

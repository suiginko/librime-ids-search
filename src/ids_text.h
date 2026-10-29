//
// ids_text.h — UTF-8 / IDS 文本与候选注释帮助函数。
//
#ifndef RIME_IDS_SEARCH_IDS_TEXT_H_
#define RIME_IDS_SEARCH_IDS_TEXT_H_

#include <cstdint>
#include <string>
#include <vector>

namespace ids_search {

class IdsStore;

uint32_t DecodeNext(const std::string& s, size_t* i);
std::string CpToUtf8(uint32_t cp);
std::vector<std::string> Utf8Chars(const std::string& s);
size_t Utf8Length(const std::string& s);
bool IsIdc(uint32_t cp);
bool HasWhitespace(const std::string& s);

std::string Join(const std::vector<std::string>& parts, const std::string& sep);

// 将 IDS 结构符转换为指定展示格式。
// unicode: 保留 ⿰ 等 IDC 字符
// bracket: 转为 [左右] 等标签
// plain:   省略 IDC 字符
std::string FormatIds(const std::string& raw_ids, const std::string& format);

const char* BlockName(int block);

// 生成 “读音 IDS Unicode 分区” 形式的候选注释。
std::string BuildComment(IdsStore& store,
                         uint32_t id,
                         bool show_pinyin,
                         bool show_ids,
                         bool show_unicode,
                         bool show_block,
                         const std::string& ids_format);

// 提取单个或多个字符的 IDS 结构；多个字符之间以空格连接。
std::string IdsOfText(IdsStore& store, const std::string& text);

}  // namespace ids_search

#endif  // RIME_IDS_SEARCH_IDS_TEXT_H_

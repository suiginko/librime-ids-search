//
// ids_hybrid_search.cc — O 模式混合检字实现。
//
#include "ids_hybrid_search.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>

#include "ids_store.h"
#include "ids_text.h"

namespace ids_search {

namespace {

using rime::hash_map;
using rime::hash_set;

bool IsAsciiAlphaOnly(const std::string& s) {
  if (s.empty())
    return false;
  for (unsigned char c : s) {
    if (!std::isalpha(c) || c > 0x7F)
      return false;
  }
  return true;
}

std::string AsciiLower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

std::string StripSpace(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
      out.push_back(static_cast<char>(c));
  }
  return out;
}

bool StartsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// ---------------------------------------------------------------------------
// 双拼解码（只保留插件已支持的 flypy / zrm / quanpin）
// ---------------------------------------------------------------------------

std::string DecodeShuangpin(IdsStore& store,
                            const std::string& raw,
                            const std::string& mode) {
  std::string sp = AsciiLower(raw);
  if (sp.size() != 2 || !IsAsciiAlphaOnly(sp) || mode == "quanpin")
    return sp;

  static const hash_map<std::string, std::string> kZeroVowels = {
      {"aa", "a"},   {"ai", "ai"}, {"an", "an"}, {"ah", "ang"},
      {"ao", "ao"},  {"ee", "e"},  {"ei", "ei"}, {"en", "en"},
      {"eg", "eng"}, {"er", "er"}, {"oo", "o"},  {"ou", "ou"}};
  auto z = kZeroVowels.find(sp);
  if (z != kZeroVowels.end() && (store.valid_pinyins().count(z->second) ||
                                 store.py_map().count(z->second))) {
    return z->second;
  }

  auto init_of = [](char c) -> std::string {
    if (c == 'v')
      return "zh";
    if (c == 'i')
      return "ch";
    if (c == 'u')
      return "sh";
    return std::string(1, c);
  };

  static const hash_map<char, std::vector<std::string>> kFlypyFinals = {
      {'q', {"iu"}},
      {'w', {"ei"}},
      {'r', {"uan"}},
      {'t', {"ue", "ve"}},
      {'y', {"un"}},
      {'p', {"ie"}},
      {'d', {"ai"}},
      {'f', {"en"}},
      {'g', {"eng"}},
      {'h', {"ang"}},
      {'j', {"an"}},
      {'k', {"uai", "ing"}},
      {'l', {"uang", "iang"}},
      {'z', {"ou"}},
      {'x', {"ua", "ia"}},
      {'c', {"ao"}},
      {'v', {"ui", "v"}},
      {'b', {"in"}},
      {'n', {"iao"}},
      {'m', {"ian"}},
      {'s', {"iong", "ong"}},
      {'o', {"uo", "o"}},
      {'u', {"u"}},
      {'i', {"i"}},
      {'a', {"a"}},
      {'e', {"e"}}};

  static const hash_map<char, std::vector<std::string>> kZrmFinals = {
      {'q', {"iu"}},
      {'w', {"ua", "ia"}},
      {'r', {"uan"}},
      {'t', {"ue", "ve"}},
      {'y', {"uai", "ing"}},
      {'p', {"un"}},
      {'d', {"uang", "iang"}},
      {'f', {"en"}},
      {'g', {"eng"}},
      {'h', {"ang"}},
      {'j', {"an"}},
      {'k', {"ao"}},
      {'l', {"ai"}},
      {'z', {"ou"}},
      {'x', {"ia"}},
      {'c', {"ch"}},
      {'v', {"ui", "v"}},
      {'b', {"in"}},
      {'n', {"in"}},
      {'m', {"ian"}},
      {'s', {"iong", "ong"}},
      {'o', {"uo", "o"}},
      {'u', {"u"}},
      {'i', {"i"}},
      {'a', {"a"}},
      {'e', {"e"}}};

  const auto& finals = (mode == "zrm") ? kZrmFinals : kFlypyFinals;
  auto fit = finals.find(sp[1]);
  if (fit == finals.end())
    return sp;
  std::string init = init_of(sp[0]);
  for (const auto& fin : fit->second) {
    std::string py = init + fin;
    if ((init == "j" || init == "q" || init == "x" || init == "y") &&
        fin == "ve")
      py = init + "ue";
    else if ((init == "j" || init == "q" || init == "x" || init == "y") &&
             fin == "v")
      py = init + "u";
    if (store.valid_pinyins().count(py) || store.py_map().count(py))
      return py;
  }
  return sp;
}

std::string NormalizeComponent(IdsStore& store,
                               const std::string& raw,
                               const std::string& mode) {
  std::string s = StripSpace(raw);
  if (s.empty())
    return s;
  std::string lower = AsciiLower(s);
  bool known = store.FindKey(s) != nullptr || store.FindKey(lower) != nullptr ||
               store.py_map().count(s) || store.py_map().count(lower) ||
               store.ResolveTerm(s) != kNoToken;
  if (!known && lower.size() == 2 && IsAsciiAlphaOnly(lower)) {
    std::string decoded = DecodeShuangpin(store, lower, mode);
    if (decoded != lower && (store.valid_pinyins().count(decoded) ||
                             store.py_map().count(decoded))) {
      return Canon(decoded);
    }
  }
  return Canon(s);
}

// ---------------------------------------------------------------------------
// 查询代数 (流式操作步骤)
// ---------------------------------------------------------------------------

struct AlgebraStep {
  char op;  // '+' or '-'
  std::string item;
};

std::vector<AlgebraStep> ParseAlgebraSteps(const std::vector<std::string>& components) {
  std::vector<AlgebraStep> steps;
  char curr_op = '+';

  for (const auto& raw : components) {
    std::string s = StripSpace(raw);
    if (s.empty())
      continue;

    size_t i = 0;
    while (i < s.size()) {
      char c = s[i];
      if (c == '+' || c == '-') {
        curr_op = c;
        ++i;
      } else {
        size_t start = i;
        while (i < s.size() && s[i] != '+' && s[i] != '-') {
          ++i;
        }
        std::string chunk = s.substr(start, i - start);
        if (IsAsciiAlphaOnly(chunk)) {
          steps.push_back({curr_op, chunk});
        } else {
          // 包含中文字符：按 UTF-8 单字拆解（例如 "立月" 拆为 "立" 与 "月"）
          std::vector<std::string> chars = Utf8Chars(chunk);
          for (const auto& ch : chars) {
            steps.push_back({curr_op, ch});
          }
        }
      }
    }
  }
  return steps;
}

// ---------------------------------------------------------------------------
// IDS / 部件分解
// ---------------------------------------------------------------------------

std::vector<std::string> TopLeavesById(IdsStore& store, uint32_t id) {
  std::vector<std::string> leaves;
  if (id == kInvalidId)
    return leaves;
  const uint8_t* py = nullptr;
  const uint8_t* ids = nullptr;
  uint16_t py_len = 0;
  uint16_t ids_len = 0;
  store.Cold(id, &py, &py_len, &ids, &ids_len);
  if (ids_len > 0) {
    std::string raw(reinterpret_cast<const char*>(ids), ids_len);
    for (const auto& ch : Utf8Chars(raw)) {
      size_t pos = 0;
      uint32_t cp = DecodeNext(ch, &pos);
      if (IsIdc(cp))
        continue;
      leaves.push_back(Canon(ch));
    }
  }
  if (leaves.empty()) {
    uint32_t cp = store.CpOfId(id);
    if (cp)
      leaves.push_back(Canon(CpToUtf8(cp)));
  }
  return leaves;
}

std::vector<std::string> TopLeavesOfComponent(IdsStore& store,
                                              const std::string& comp) {
  std::vector<std::string> chars = Utf8Chars(comp);
  if (chars.size() != 1)
    return {Canon(comp)};
  size_t pos = 0;
  uint32_t cp = DecodeNext(comp, &pos);
  uint32_t id = store.IdOfCp(cp);
  if (id == kInvalidId)
    return {Canon(comp)};
  std::vector<std::string> leaves = TopLeavesById(store, id);
  if (leaves.empty())
    leaves.push_back(Canon(comp));
  return leaves;
}

const hash_set<std::string>& AtomicComponents() {
  static const hash_set<std::string> kAtomic = {
      "木", "氵", "水", "火", "灬", "土", "金", "钅", "日", "月", "目", "口",
      "人", "亻", "手", "扌", "心", "忄", "女", "子", "艹", "言", "讠", "石",
      "田", "山", "刀", "刂", "力", "又", "十", "大", "小", "工", "弓", "广",
      "门", "马", "鸟", "鱼", "虫", "犭", "犬", "辶", "宀", "冖", "穴", "竹",
      "⺮", "米", "糸", "纟", "车", "足", "⻊", "雨", "页", "酉", "贝", "走",
      "身", "辛", "辰", "邑", "阝", "阜", "方", "斤", "止", "歹", "殳", "毛",
      "氏", "气", "白", "皮", "皿", "矢", "示", "礻", "禾", "立", "羽", "舟",
      "艮", "见", "角", "谷", "豆", "豕", "豸", "赤", "革", "音", "风", "飞",
      "食", "饣", "首", "香", "骨", "鬼", "黾", "鼎", "鼓", "鼠", "鼻", "齐",
      "齿", "龙", "龟",
      // Canon() 归一后的代表字形
      "⺡", "⺣", "⺅", "龵", "⺖", "⺾", "⺬", "⺭", "⻐", "⻠", "⺰", "⺲",
      "⻌", "⻖", "⺩", "⺙", "⺹", "⻊", "⺫", "⺮", "⺉", "⺶", "㇒", "㇐",
      "㇑", "㇔", "㇏", "㇀", "乛", "亅"};
  return kAtomic;
}

std::vector<std::string> AtomicDecomposition(
    IdsStore& store,
    const std::string& comp,
    hash_map<std::string, std::vector<std::string>>* cache,
    int depth) {
  std::string ch = Canon(comp);
  auto hit = cache->find(ch);
  if (hit != cache->end())
    return hit->second;
  if (depth > 2 || AtomicComponents().count(ch)) {
    std::vector<std::string> r = {ch};
    (*cache)[ch] = r;
    return r;
  }
  std::vector<std::string> leaves = TopLeavesOfComponent(store, ch);
  if (leaves.size() <= 1) {
    std::vector<std::string> r = {ch};
    (*cache)[ch] = r;
    return r;
  }
  std::vector<std::string> out;
  for (const auto& leaf : leaves) {
    if (leaf == ch) {
      out.push_back(leaf);
    } else {
      std::vector<std::string> sub =
          AtomicDecomposition(store, leaf, cache, depth + 1);
      out.insert(out.end(), sub.begin(), sub.end());
    }
  }
  (*cache)[ch] = out;
  return out;
}

// ---------------------------------------------------------------------------
// postings 读取 / 求交
// ---------------------------------------------------------------------------

void EmitKeyIds(IdsStore& store,
                const KeyEntry& e,
                std::vector<uint32_t>* out,
                uint32_t limit) {
  store.ForEachFull(e, [&](uint32_t id) {
    out->push_back(id);
    return out->size() < limit;
  });
}

std::vector<std::string> KeysForTerm(IdsStore& store,
                                     const std::string& term,
                                     int count) {
  std::vector<std::string> keys;
  std::string suffix = (count > 1) ? "*" + std::to_string(count) : "";
  if (store.FindKey(term + suffix)) {
    keys.push_back(term + suffix);
    return keys;
  }
  auto pit = store.py_map().find(term);
  if (pit != store.py_map().end()) {
    for (uint16_t t : pit->second) {
      const std::string& token = store.TokenStr(t);
      if (token.empty())
        continue;
      std::string k = token + suffix;
      if (!store.FindKey(k) && count > 1)
        k = token;
      if (store.FindKey(k))
        keys.push_back(k);
    }
    if (!keys.empty())
      return keys;
  }
  uint16_t token = store.ResolveTerm(term);
  if (token != kNoToken) {
    std::string k = store.TokenStr(token) + suffix;
    if (!store.FindKey(k) && count > 1)
      k = store.TokenStr(token);
    if (store.FindKey(k))
      keys.push_back(k);
  }
  return keys;
}

std::vector<uint32_t> PostingIdsForTerm(IdsStore& store,
                                        const std::string& term,
                                        int count,
                                        uint32_t limit) {
  std::vector<uint32_t> ids;
  hash_set<uint32_t> seen;
  for (const auto& key : KeysForTerm(store, term, count)) {
    const KeyEntry* e = store.FindKey(key);
    if (!e || e->full_cnt == 0)
      continue;
    std::vector<uint32_t> part;
    EmitKeyIds(store, *e, &part, limit);
    for (uint32_t id : part) {
      if (seen.insert(id).second)
        ids.push_back(id);
    }
    if (ids.size() >= limit)
      break;
  }
  return ids;
}

std::vector<uint32_t> IntersectPostings(IdsStore& store,
                                        const std::vector<std::string>& terms,
                                        const std::vector<int>& counts,
                                        uint32_t limit) {
  if (terms.empty())
    return {};
  // 先分别取出各项候选，再从最小集合开始求交，尽量避免大集合截断造成漏字。
  constexpr uint32_t kPerTermLimit = 20000;
  std::vector<std::vector<uint32_t>> sets;
  sets.reserve(terms.size());
  size_t start = 0;
  for (size_t i = 0; i < terms.size(); ++i) {
    sets.push_back(PostingIdsForTerm(store, terms[i], counts[i],
                                     std::max(kPerTermLimit, limit)));
    if (sets.back().empty())
      return {};
    if (sets[i].size() < sets[start].size())
      start = i;
  }
  std::vector<uint32_t> result = sets[start];
  for (size_t i = 0; i < sets.size() && !result.empty(); ++i) {
    if (i == start)
      continue;
    hash_set<uint32_t> next_set(sets[i].begin(), sets[i].end());
    std::vector<uint32_t> filtered;
    filtered.reserve(result.size());
    for (uint32_t id : result) {
      if (next_set.count(id))
        filtered.push_back(id);
    }
    result.swap(filtered);
  }
  return result;
}

// ---------------------------------------------------------------------------
// 黄金排序
// ---------------------------------------------------------------------------

void AddCount(std::map<std::string, int>* counts, const std::string& k) {
  (*counts)[k] += 1;
}

int64_t ScoreHybrid(IdsStore& store,
                    uint32_t id,
                    const std::vector<std::string>& normalized,
                    const std::map<std::string, int>& user_atomics,
                    const std::vector<std::string>& user_atomic_seq,
                    hash_map<std::string, std::vector<std::string>>* cache) {
  uint32_t cp = store.CpOfId(id);
  std::string ch = CpToUtf8(cp);
  std::vector<std::string> cand_leaves = TopLeavesById(store, id);
  std::vector<std::string> cand_atomic_list =
      AtomicDecomposition(store, ch, cache, 0);

  std::map<std::string, int> cand_atomics;
  for (const auto& a : cand_atomic_list)
    AddCount(&cand_atomics, a);

  bool satisfied = true;
  int missing_count = 0;
  for (const auto& kv : user_atomics) {
    auto it = cand_atomics.find(kv.first);
    int avail = it == cand_atomics.end() ? 0 : it->second;
    if (avail < kv.second) {
      satisfied = false;
      missing_count += kv.second - avail;
    }
  }

  int extra_count = 0;
  for (const auto& kv : cand_atomics) {
    auto it = user_atomics.find(kv.first);
    int req = it == user_atomics.end() ? 0 : it->second;
    if (kv.second > req)
      extra_count += kv.second - req;
  }

  int64_t ordered_bonus = 0;
  bool match_macro = cand_leaves.size() == normalized.size();
  if (match_macro) {
    for (size_t i = 0; i < normalized.size(); ++i) {
      if (cand_leaves[i] != normalized[i]) {
        match_macro = false;
        break;
      }
    }
  }
  bool match_macro_rev = false;
  if (!match_macro && cand_leaves.size() == normalized.size()) {
    match_macro_rev = true;
    size_t n = normalized.size();
    for (size_t i = 0; i < n; ++i) {
      if (cand_leaves[i] != normalized[n - i - 1]) {
        match_macro_rev = false;
        break;
      }
    }
  }
  if (match_macro) {
    ordered_bonus = 20000000;
  } else if (match_macro_rev) {
    ordered_bonus = 10000000;
  } else if (user_atomic_seq.size() == cand_atomic_list.size()) {
    bool seq_match = true;
    for (size_t i = 0; i < user_atomic_seq.size(); ++i) {
      if (user_atomic_seq[i] != cand_atomic_list[i]) {
        seq_match = false;
        break;
      }
    }
    if (seq_match)
      ordered_bonus = 15000000;
  }

  int64_t score = 0;
  if (satisfied) {
    if (extra_count == 0)
      score = 50000000 + ordered_bonus;
    else
      score = 20000000 - extra_count * 500000 + ordered_bonus / 2;
  } else {
    score = -missing_count * 5000000 - extra_count * 500000;
  }

  int block = store.Hot(id).block;
  if (block == 0)
    score += 2000000;
  else if (block == 1)
    score += 1000000;
  return score;
}

std::vector<uint32_t> RankHybrid(IdsStore& store,
                                 std::vector<uint32_t> cands,
                                 const std::vector<std::string>& normalized) {
  if (cands.size() <= 1 || normalized.empty())
    return cands;

  std::map<std::string, int> user_atomics;
  std::vector<std::string> user_atomic_seq;
  hash_map<std::string, std::vector<std::string>> cache;
  std::vector<std::string> norm_input;
  for (const auto& raw : normalized) {
    std::string c = Canon(raw);
    norm_input.push_back(c);
    std::vector<std::string> atomics = AtomicDecomposition(store, c, &cache, 0);
    for (const auto& a : atomics) {
      AddCount(&user_atomics, a);
      user_atomic_seq.push_back(a);
    }
  }

  size_t limit = std::min<size_t>(cands.size(), 500);
  struct Scored {
    uint32_t id;
    int64_t score;
    size_t index;
  };
  std::vector<Scored> scored;
  scored.reserve(limit);
  for (size_t i = 0; i < limit; ++i) {
    scored.push_back({cands[i],
                      ScoreHybrid(store, cands[i], norm_input, user_atomics,
                                  user_atomic_seq, &cache),
                      i});
  }
  std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score)
      return a.score > b.score;
    return a.index < b.index;
  });
  std::vector<uint32_t> out;
  out.reserve(cands.size());
  for (const auto& it : scored)
    out.push_back(it.id);
  for (size_t i = limit; i < cands.size(); ++i)
    out.push_back(cands[i]);
  return out;
}

// ---------------------------------------------------------------------------
// 递归抠除与连续代数运算（支持多层级深层结构折叠展开，如 龍 - 立月 = 𳑳）
// ---------------------------------------------------------------------------

std::string ToStandardChar(const std::string& c) {
  static const rime::hash_map<std::string, std::string> kStdMap = {
      {"水", "氵"}, {"⺡", "氵"}, {"氺", "氵"},
      {"火", "灬"}, {"⺣", "灬"},
      {"人", "亻"}, {"𠆢", "亻"}, {"⺅", "亻"},
      {"手", "扌"}, {"龵", "扌"},
      {"心", "忄"}, {"⺖", "忄"}, {"⺗", "忄"},
      {"肉", "月"}, {"⺼", "月"}, {"⺝", "月"},
      {"艸", "艹"}, {"⺾", "艹"}, {"⻀", "艹"},
      {"示", "礻"}, {"⺬", "礻"},
      {"衣", "衤"}, {"⺭", "衤"}, {"𧘇", "衤"},
      {"金", "钅"}, {"釒", "钅"}, {"⻐", "钅"},
      {"犬", "犭"}, {"⺲", "犭"},
      {"辵", "辶"}, {"⻌", "辶"}, {"⻍", "辶"},
      {"阜", "阝"}, {"邑", "阝"}, {"⻖", "阝"}, {"⻗", "阝"},
  };
  auto it = kStdMap.find(c);
  return it != kStdMap.end() ? it->second : c;
}

bool SubtractCharacter(IdsStore& store,
                       const std::string& base_char,
                       const std::string& neg_item,
                       std::vector<std::string>* out_leaves,
                       int depth = 0) {
  std::string norm_base = Canon(base_char);
  if (norm_base == neg_item || base_char == neg_item) {
    out_leaves->clear();
    return true;
  }
  if (depth > 3 || AtomicComponents().count(norm_base)) {
    return false;
  }

  std::vector<std::string> leaves = TopLeavesOfComponent(store, base_char);
  if (leaves.empty() || (leaves.size() == 1 && (Canon(leaves[0]) == norm_base || leaves[0] == base_char))) {
    return false;
  }

  // 1. 直接叶子命中
  for (size_t i = 0; i < leaves.size(); ++i) {
    if (Canon(leaves[i]) == neg_item || leaves[i] == neg_item) {
      out_leaves->clear();
      for (size_t j = 0; j < leaves.size(); ++j) {
        if (j != i) {
          out_leaves->push_back(leaves[j]);
        }
      }
      return true;
    }
  }

  // 2. 递归深入复合子部件展开
  bool sub_done = false;
  std::vector<std::string> new_leaves;
  for (const auto& leaf : leaves) {
    if (!sub_done) {
      std::vector<std::string> sub_res;
      if (SubtractCharacter(store, leaf, neg_item, &sub_res, depth + 1)) {
        new_leaves.insert(new_leaves.end(), sub_res.begin(), sub_res.end());
        sub_done = true;
      } else {
        new_leaves.push_back(leaf);
      }
    } else {
      new_leaves.push_back(leaf);
    }
  }

  if (sub_done) {
    *out_leaves = std::move(new_leaves);
    return true;
  }
  return false;
}

bool ApplySingleSubtraction(IdsStore& store,
                            std::vector<std::string>* current_comps,
                            const std::string& neg_item) {
  // 优先从当前已有的复合字符中向下递归抠除
  for (size_t i = 0; i < current_comps->size(); ++i) {
    std::vector<std::string> sub_res;
    if (SubtractCharacter(store, (*current_comps)[i], neg_item, &sub_res, 0)) {
      std::vector<std::string> updated;
      for (size_t j = 0; j < i; ++j) {
        updated.push_back((*current_comps)[j]);
      }
      for (const auto& r : sub_res) {
        updated.push_back(r);
      }
      for (size_t j = i + 1; j < current_comps->size(); ++j) {
        updated.push_back((*current_comps)[j]);
      }
      *current_comps = std::move(updated);
      return true;
    }
  }

  // 若无法从字内抠除，尝试从当前部件列表中字面剔除
  for (size_t i = 0; i < current_comps->size(); ++i) {
    if (Canon((*current_comps)[i]) == neg_item || (*current_comps)[i] == neg_item) {
      current_comps->erase(current_comps->begin() + i);
      return true;
    }
  }

  return false;
}

}  // namespace

std::vector<uint32_t> SearchHybrid(IdsStore& store,
                                   const std::vector<std::string>& components,
                                   const std::string& mode,
                                   size_t max_results,
                                   const rime::hash_set<int>* disabled_blocks) {
  if (components.empty())
    return {};

  std::vector<AlgebraStep> steps = ParseAlgebraSteps(components);
  if (steps.empty())
    return {};

  std::vector<std::string> current_comps;
  bool pure_sub = true;
  bool has_sub = false;

  for (const auto& step : steps) {
    std::string item = StripSpace(step.item);
    if (item.empty())
      continue;
    if (item.size() == 2 && IsAsciiAlphaOnly(item) && !store.py_map().count(item) &&
        !store.FindKey(item)) {
      item = DecodeShuangpin(store, item, mode);
    }

    if (step.op == '+') {
      if (!current_comps.empty() && has_sub) {
        pure_sub = false;
      }
      // 被减数汉字保留真实字形（如 龍、佩、森），避免被 Canon 强行繁转简破坏 IDS 结构
      std::vector<std::string> chars = Utf8Chars(item);
      if (chars.size() == 1) {
        size_t pos = 0;
        uint32_t cp = DecodeNext(item, &pos);
        if (store.IdOfCp(cp) != kInvalidId) {
          current_comps.push_back(item);
        } else {
          current_comps.push_back(Canon(item));
        }
      } else {
        current_comps.push_back(Canon(item));
      }
    } else if (step.op == '-') {
      has_sub = true;
      std::string norm_item = Canon(item);
      ApplySingleSubtraction(store, &current_comps, norm_item);
    }
  }

  // 减法残余部件（第 1 梯队置顶候选）
  std::vector<uint32_t> sub_results;
  hash_set<uint32_t> sub_seen;

  if (has_sub && !current_comps.empty()) {
    for (const auto& c : current_comps) {
      std::string std_c = ToStandardChar(c);
      size_t pos = 0;
      uint32_t cp = DecodeNext(std_c, &pos);
      uint32_t id = store.IdOfCp(cp);
      if (id == kInvalidId) {
        pos = 0;
        cp = DecodeNext(c, &pos);
        id = store.IdOfCp(cp);
      }
      if (id != kInvalidId && sub_seen.insert(id).second) {
        sub_results.push_back(id);
      }
    }
  }

  if (current_comps.empty())
    return sub_results;

  // 检索“包含剩下部件的字”（第 2 梯队衍生候选）
  std::vector<std::string> normalized;
  for (const auto& raw : current_comps) {
    std::string n = NormalizeComponent(store, raw, mode);
    if (!n.empty())
      normalized.push_back(n);
  }
  if (normalized.empty())
    return sub_results;

  std::vector<std::string> order;
  std::vector<int> counts;
  std::map<std::string, int> count_map;
  for (const auto& n : normalized) {
    auto it = count_map.find(n);
    if (it == count_map.end()) {
      count_map[n] = 1;
      order.push_back(n);
      counts.push_back(1);
    } else {
      it->second += 1;
      for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == n) {
          counts[i] = it->second;
          break;
        }
      }
    }
  }

  std::vector<uint32_t> cands = IntersectPostings(
      store, order, counts, std::max<size_t>(5000, max_results));
  if (disabled_blocks) {
    cands.erase(std::remove_if(cands.begin(), cands.end(),
                               [&](uint32_t id) {
                                 return disabled_blocks->count(static_cast<int>(
                                            store.Hot(id).block)) > 0;
                               }),
                cands.end());
  }
  cands = RankHybrid(store, std::move(cands), normalized);

  // 组合最终候选：残余部件置顶 -> 包含剩下部件的衍生字紧随其后（按黄金打分高匹配度优先）
  std::vector<uint32_t> final_results = sub_results;
  for (uint32_t id : cands) {
    if (!sub_seen.count(id)) {
      final_results.push_back(id);
    }
  }

  if (final_results.size() > max_results)
    final_results.resize(max_results);
  return final_results;
}

std::vector<uint32_t> GetComponentCandidates(IdsStore& store,
                                             const std::string& raw_code,
                                             const std::string& mode,
                                             size_t limit) {
  std::string code = StripSpace(raw_code);
  while (!code.empty() && (code.front() == '-' || code.front() == '+'))
    code.erase(code.begin());
  if (code.empty())
    return {};
  code = AsciiLower(code);
  if (!IsAsciiAlphaOnly(code))
    return {};

  std::string lookup = code;
  if (!store.py_map().count(lookup) && lookup.size() == 2) {
    std::string decoded = DecodeShuangpin(store, lookup, mode);
    if (decoded != lookup)
      lookup = decoded;
  }

  std::vector<uint16_t> tokens;
  auto pit = store.py_map().find(lookup);
  if (pit != store.py_map().end()) {
    tokens = pit->second;
  } else {
    // 前缀匹配：输入 mu 时同时给出木/目/母等，输入 m 时给出全部 m 开头部件。
    hash_set<uint16_t> seen;
    for (const auto& kv : store.py_map()) {
      if (!StartsWith(kv.first, lookup))
        continue;
      for (uint16_t t : kv.second) {
        if (seen.insert(t).second)
          tokens.push_back(t);
      }
    }
  }

  std::vector<uint32_t> ids;
  hash_set<uint32_t> seen_ids;
  for (uint16_t t : tokens) {
    uint32_t cp = store.TokenCp(t);
    if (!cp)
      continue;
    uint32_t id = store.IdOfCp(cp);
    if (id == kInvalidId || !seen_ids.insert(id).second)
      continue;
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end(), [&](uint32_t a, uint32_t b) {
    return store.Hot(a).freq > store.Hot(b).freq;
  });
  if (ids.size() > limit)
    ids.resize(limit);
  return ids;
}

}  // namespace ids_search

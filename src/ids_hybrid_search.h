//
// ids_hybrid_search.h — O 模式混合检字（部件/拼音/双拼 + 加减法）。
//
#ifndef RIME_IDS_SEARCH_IDS_HYBRID_SEARCH_H_
#define RIME_IDS_SEARCH_IDS_HYBRID_SEARCH_H_

#include <cstdint>
#include <string>
#include <vector>

#include <rime/common.h>

namespace ids_search {

class IdsStore;

// components 为已选部件文本（可能带前导 - / +），按输入顺序排列。
std::vector<uint32_t> SearchHybrid(IdsStore& store,
                                   const std::vector<std::string>& components,
                                   const std::string& mode,
                                   size_t max_results,
                                   const rime::hash_set<int>* disabled_blocks);

// pending 编码对应的部件单字候选（自包含，不依赖外部拼音翻译器）。
std::vector<uint32_t> GetComponentCandidates(IdsStore& store,
                                             const std::string& code,
                                             const std::string& mode,
                                             size_t limit);

}  // namespace ids_search

#endif  // RIME_IDS_SEARCH_IDS_HYBRID_SEARCH_H_

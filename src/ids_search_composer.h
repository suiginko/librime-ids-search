//
// ids_search_composer.h — I 模式原生触发组件。
//
#ifndef RIME_IDS_SEARCH_IDS_SEARCH_COMPOSER_H_
#define RIME_IDS_SEARCH_IDS_SEARCH_COMPOSER_H_

#include <string>

#include <rime/common.h>
#include <rime/filter.h>
#include <rime/processor.h>
#include <rime/segmentor.h>

namespace ids_search {

struct SearchTriggerConfig {
  std::string prefix = "I";
  std::string tag = "ids_search";
  std::string tips = "〔部件检字〕";
};

// 接收首个大写 I，把它推入输入串。
class IdsSearchProcessor : public rime::Processor {
 public:
  explicit IdsSearchProcessor(const rime::Ticket& ticket);

  rime::ProcessResult ProcessKeyEvent(const rime::KeyEvent& key_event) override;

 private:
  SearchTriggerConfig config_;
};

// 给以 I 开头的输入打上 ids_search 标签并停止后续 segmentor。
class IdsSearchSegmentor : public rime::Segmentor {
 public:
  explicit IdsSearchSegmentor(const rime::Ticket& ticket);

  bool Proceed(rime::Segmentation* segmentation) override;

 private:
  SearchTriggerConfig config_;
};

// 仅前缀段（单独 I）不产生候选，避免 echo/提示候选。
class IdsSearchFilter : public rime::Filter {
 public:
  explicit IdsSearchFilter(const rime::Ticket& ticket);

  bool AppliesToSegment(rime::Segment* segment) override;
  rime::an<rime::Translation> Apply(rime::an<rime::Translation> translation,
                                    rime::CandidateList* candidates) override;

 private:
  SearchTriggerConfig config_;
  bool suppress_ = false;
};

}  // namespace ids_search

#endif  // RIME_IDS_SEARCH_IDS_SEARCH_COMPOSER_H_

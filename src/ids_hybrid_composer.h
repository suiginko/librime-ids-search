//
// ids_hybrid_composer.h — 原生 C++ O 模式混合检字组件。
//
#ifndef RIME_IDS_SEARCH_IDS_HYBRID_COMPOSER_H_
#define RIME_IDS_SEARCH_IDS_HYBRID_COMPOSER_H_

#include <string>

#include <rime/common.h>
#include <rime/context.h>
#include <rime/filter.h>
#include <rime/processor.h>
#include <rime/segmentor.h>
#include <rime/translation.h>
#include <rime/translator.h>

namespace ids_search {

class IdsStore;

struct HybridConfig {
  std::string tag = "ids_search_hybrid";
  std::string prefix = "O";
  std::string tips = "〔汉字部件检字〕";
  std::string data_path = "ids_data.bin";
  std::string custom_path;
  std::string shuangpin_scheme = "flypy";
  std::string ids_format = "unicode";
  int max_results = 60;
  bool show_pinyin = true;
  bool show_ids = true;
  bool show_unicode = true;
  bool show_block = true;
  bool has_sw_pinyin = false;
  bool has_sw_ids = false;
  bool has_sw_unicode = false;
  bool has_sw_block = false;
  rime::hash_set<int> disabled_blocks;
  bool disabled_blocks_enabled = false;
};

class IdsHybridProcessor : public rime::Processor {
 public:
  explicit IdsHybridProcessor(const rime::Ticket& ticket);
  ~IdsHybridProcessor() override;

  rime::ProcessResult ProcessKeyEvent(const rime::KeyEvent& key_event) override;

 private:
  void OnSelectBefore(rime::Context* ctx);
  void OnSelectAfter(rime::Context* ctx);
  void OnUpdate(rime::Context* ctx);
  bool HandleIdsHotkey(const rime::KeyEvent& key_event, rime::Context* ctx);
  bool HandleSelectionKey(int digit, rime::Context* ctx);

  HybridConfig config_;
  rime::connection select_before_;
  rime::connection select_after_;
  rime::connection update_connection_;

  bool has_selection_ = false;
  bool selected_is_result_ = false;
  std::string selected_text_;
  std::string selected_component_;
  bool saved_auto_commit_ = false;
  bool auto_commit_managed_ = false;
};

class IdsHybridSegmentor : public rime::Segmentor {
 public:
  explicit IdsHybridSegmentor(const rime::Ticket& ticket);
  bool Proceed(rime::Segmentation* segmentation) override;

 private:
  HybridConfig config_;
};

class IdsHybridTranslator : public rime::Translator {
 public:
  explicit IdsHybridTranslator(const rime::Ticket& ticket);

  rime::an<rime::Translation> Query(const std::string& input,
                                    const rime::Segment& segment) override;

 private:
  bool EnsureLoaded();
  std::string PinyinMode();
  void BuildResultCandidates(IdsStore& store,
                             const std::vector<uint32_t>& results,
                             const rime::Segment& segment,
                             const std::string& pending,
                             rime::FifoTranslation* translation);
  void BuildComponentCandidates(IdsStore& store,
                                const std::vector<uint32_t>& results,
                                const rime::Segment& segment,
                                rime::FifoTranslation* translation);

  HybridConfig config_;
};

class IdsHybridFilter : public rime::Filter {
 public:
  explicit IdsHybridFilter(const rime::Ticket& ticket);

  rime::an<rime::Translation> Apply(rime::an<rime::Translation> translation,
                                    rime::CandidateList* candidates) override;

 private:
  HybridConfig config_;
  bool cleaner_ = false;
};

}  // namespace ids_search

#endif  // RIME_IDS_SEARCH_IDS_HYBRID_COMPOSER_H_

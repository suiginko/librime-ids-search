//
// ids_search_module.cc — 插件模块注册入口。
//
#include <rime/component.h>
#include <rime/registry.h>
#include <rime_api.h>

#include "ids_hybrid_composer.h"
#include "ids_search_translator.h"

using namespace rime;

static void rime_ids_search_initialize() {
  Registry& r = Registry::instance();
  r.Register("ids_search_translator",
             new Component<ids_search::IdsSearchTranslator>);
  r.Register("ids_hybrid_processor",
             new Component<ids_search::IdsHybridProcessor>);
  r.Register("ids_hybrid_segmentor",
             new Component<ids_search::IdsHybridSegmentor>);
  r.Register("ids_hybrid_translator",
             new Component<ids_search::IdsHybridTranslator>);
  r.Register("ids_hybrid_filter",
             new Component<ids_search::IdsHybridFilter>);
  r.Register("ids_hybrid_cleaner",
             new Component<ids_search::IdsHybridFilter>);
}

static void rime_ids_search_finalize() {}

RIME_REGISTER_MODULE(ids_search)

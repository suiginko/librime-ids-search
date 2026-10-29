//
// ids_hybrid_composer.cc — 原生 C++ O 模式混合检字组件。
//
#include "ids_hybrid_composer.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#include <rime/candidate.h>
#include <rime/config.h>
#include <rime/engine.h>
#include <rime/key_event.h>
#include <rime/key_table.h>
#include <rime/menu.h>
#include <rime/schema.h>
#include <rime/segmentation.h>
#include <rime/service.h>

#include "ids_hybrid_search.h"
#include "ids_store.h"
#include "ids_text.h"

namespace ids_search {

namespace {

using rime::an;
using rime::Candidate;
using rime::Config;
using rime::Context;
using rime::Engine;
using rime::New;
using rime::Segment;
using rime::Segmentation;

constexpr char kStateProperty[] = "ids_hybrid_components";

bool StartsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string DefaultNamespace(const rime::Ticket& ticket,
                             const std::string& fallback) {
  if (ticket.name_space.empty() || ticket.name_space == "processor" ||
      ticket.name_space == "segmentor" || ticket.name_space == "translator" ||
      ticket.name_space == "filter") {
    return fallback;
  }
  return ticket.name_space;
}

void LoadDisabledBlocks(Config* config,
                        const std::string& ns,
                        HybridConfig* cfg) {
  if (!config)
    return;
  if (auto list = config->GetList(ns + "/disabled_blocks")) {
    for (size_t i = 0; i < list->size(); ++i) {
      if (auto value = rime::As<rime::ConfigValue>(list->GetValueAt(i))) {
        std::string name;
        if (value->GetString(&name)) {
          auto it = BlockAliasMap().find(name);
          if (it != BlockAliasMap().end())
            cfg->disabled_blocks.insert(it->second);
        }
      }
    }
  }
  std::string str;
  if (config->GetString(ns + "/disabled_blocks", &str) && !str.empty()) {
    size_t i = 0;
    while (i < str.size()) {
      while (i < str.size() &&
             (str[i] == ',' || str[i] == ' ' || str[i] == '\t' ||
              str[i] == '\r' || str[i] == '\n'))
        ++i;
      size_t j = i;
      while (j < str.size() && str[j] != ',' && str[j] != ' ' &&
             str[j] != '\t' && str[j] != '\r' && str[j] != '\n')
        ++j;
      if (j > i) {
        auto it = BlockAliasMap().find(str.substr(i, j - i));
        if (it != BlockAliasMap().end())
          cfg->disabled_blocks.insert(it->second);
      }
      i = j;
    }
  }
  cfg->disabled_blocks_enabled = !cfg->disabled_blocks.empty();
}

void LoadConfig(const rime::Ticket& ticket,
                const std::string& fallback_ns,
                std::string* name_space,
                HybridConfig* cfg) {
  *name_space = DefaultNamespace(ticket, fallback_ns);
  const std::string ns = *name_space;
  Engine* engine = ticket.engine;
  if (!engine || !engine->schema())
    return;
  Config* config = engine->schema()->config();
  if (!config)
    return;

  config->GetString(ns + "/tag", &cfg->tag);
  config->GetString(ns + "/prefix", &cfg->prefix);
  config->GetString(ns + "/tips", &cfg->tips);
  config->GetString(ns + "/data_file", &cfg->data_path);
  config->GetString(ns + "/custom_file", &cfg->custom_path);
  config->GetString(ns + "/shuangpin_scheme", &cfg->shuangpin_scheme);
  config->GetString(ns + "/ids_format", &cfg->ids_format);
  config->GetInt(ns + "/max_results", &cfg->max_results);

  if (cfg->tag.empty())
    cfg->tag = "ids_search_hybrid";
  if (cfg->prefix.empty())
    cfg->prefix = "O";
  if (cfg->tips.empty())
    cfg->tips = "〔汉字部件检字〕";
  if (cfg->data_path.empty())
    cfg->data_path = "ids_data.bin";
  if (cfg->shuangpin_scheme != "zrm" && cfg->shuangpin_scheme != "quanpin")
    cfg->shuangpin_scheme = "flypy";
  if (cfg->max_results <= 0)
    cfg->max_results = 60;

  bool v;
  if (config->GetBool(ns + "/show_pinyin", &v))
    cfg->show_pinyin = v;
  if (config->GetBool(ns + "/show_ids", &v))
    cfg->show_ids = v;
  if (config->GetBool(ns + "/show_unicode", &v))
    cfg->show_unicode = v;
  if (config->GetBool(ns + "/show_block", &v))
    cfg->show_block = v;

  for (size_t i = 0; i < 64; ++i) {
    std::string s_name;
    if (config->GetString("switches/@" + std::to_string(i) + "/name",
                          &s_name)) {
      if (s_name == "show_pinyin")
        cfg->has_sw_pinyin = true;
      else if (s_name == "show_ids")
        cfg->has_sw_ids = true;
      else if (s_name == "show_unicode")
        cfg->has_sw_unicode = true;
      else if (s_name == "show_block")
        cfg->has_sw_block = true;
    } else if (config->GetItem("switches/@" + std::to_string(i)) == nullptr) {
      break;
    }
  }

  LoadDisabledBlocks(config, ns, cfg);
}

bool IsActive(Context* ctx, const std::string& prefix) {
  if (!ctx || prefix.empty())
    return false;
  if (ctx->get_option("ascii_mode"))
    return false;
  return StartsWith(ctx->input(), prefix);
}

struct HybridState {
  std::vector<std::string> components;
  std::vector<std::string> codes;
  std::string pending;
};

HybridState ReadState(Context* ctx, const std::string& prefix) {
  HybridState st;
  if (!ctx)
    return st;
  std::string prop = ctx->get_property(kStateProperty);
  size_t pos = 0;
  while (pos < prop.size()) {
    size_t nl = prop.find('\n', pos);
    if (nl == std::string::npos)
      nl = prop.size();
    std::string line = prop.substr(pos, nl - pos);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    size_t tab = line.find('\t');
    if (tab != std::string::npos) {
      st.components.push_back(line.substr(0, tab));
      st.codes.push_back(line.substr(tab + 1));
    }
    pos = nl + 1;
  }

  std::string expected = prefix;
  for (const auto& code : st.codes)
    expected += code + "'";
  const std::string& input = ctx->input();
  if (!StartsWith(input, expected)) {
    st.components.clear();
    st.codes.clear();
    st.pending = input.size() > prefix.size() ? input.substr(prefix.size())
                                              : std::string();
    return st;
  }
  st.pending = input.substr(expected.size());
  return st;
}

void SaveState(Context* ctx,
               const std::vector<std::string>& components,
               const std::vector<std::string>& codes) {
  std::string prop;
  for (size_t i = 0; i < components.size() && i < codes.size(); ++i) {
    prop += components[i];
    prop += '\t';
    prop += codes[i];
    prop += '\n';
  }
  ctx->set_property(kStateProperty, prop);
}

std::string PinyinMode(Engine* engine, const HybridConfig& cfg) {
  std::string mode = cfg.shuangpin_scheme;
  if (engine && engine->context()) {
    Context* ctx = engine->context();
    if (ctx->get_option("shuangpin_flypy") ||
        ctx->get_option("double_pinyin")) {
      mode = "flypy";
    } else if (ctx->get_option("shuangpin_zrm")) {
      mode = "zrm";
    } else if (ctx->get_option("pinyin_quanpin")) {
      mode = "quanpin";
    }
  }
  return mode;
}

bool EnsureStoreLoaded(const HybridConfig& cfg) {
  IdsStore& store = IdsStore::Instance();
  if (store.loaded()) {
    return true;
  }
  rime::Deployer& deployer = rime::Service::instance().deployer();
  auto resolve = [&](const std::string& filename) -> fs::path {
    fs::path p(filename);
    if (p.is_absolute())
      return p;
    fs::path ud = deployer.user_data_dir / filename;
    if (fs::exists(ud))
      return ud;
    fs::path sd = deployer.shared_data_dir / filename;
    if (fs::exists(sd))
      return sd;
    return ud;
  };
  fs::path data =
      resolve(cfg.data_path.empty() ? "ids_data.bin" : cfg.data_path);
  fs::path custom;
  if (!cfg.custom_path.empty())
    custom = resolve(cfg.custom_path);
  bool ok = store.Load(data, custom);
  if (!ok) {
    LOG(ERROR) << "ids_hybrid: cannot load data file: '" << data << "'";
    return false;
  }
  return true;
}

bool IsSingleChar(const std::string& text) {
  return Utf8Length(text) == 1 && !HasWhitespace(text);
}

void ApplyCustomDisabledBlocks(rime::hash_set<int>* disabled, bool* enabled) {
  IdsStore& store = IdsStore::Instance();
  for (int b : store.custom_disabled_blocks()) {
    if (disabled->insert(b).second)
      *enabled = true;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Processor
// ---------------------------------------------------------------------------

IdsHybridProcessor::IdsHybridProcessor(const rime::Ticket& ticket)
    : Processor(ticket) {
  LoadConfig(ticket, "ids_hybrid", &name_space_, &config_);
  Context* ctx = engine_ ? engine_->context() : nullptr;
  if (ctx) {
    select_before_ = ctx->select_notifier().connect(
        [this](Context* c) { OnSelectBefore(c); }, boost::signals2::at_front);
    select_after_ = ctx->select_notifier().connect(
        [this](Context* c) { OnSelectAfter(c); }, boost::signals2::at_back);
    update_connection_ =
        ctx->update_notifier().connect([this](Context* c) { OnUpdate(c); });
  }
}

IdsHybridProcessor::~IdsHybridProcessor() {
  select_before_.disconnect();
  select_after_.disconnect();
  update_connection_.disconnect();
}

void IdsHybridProcessor::OnSelectBefore(Context* ctx) {
  if (!IsActive(ctx, config_.prefix))
    return;
  if (ctx->composition().empty())
    return;
  an<Candidate> cand = ctx->GetSelectedCandidate();
  if (!cand)
    return;
  an<Candidate> genuine = Candidate::GetGenuineCandidate(cand);
  const std::string& type = genuine ? genuine->type() : cand->type();
  has_selection_ = true;
  selected_is_result_ = (type == "ids_search" || type == "hanzi_result");

  selected_text_ = cand->text();
  selected_component_ = genuine ? genuine->text() : cand->text();
  // 选择期间临时关闭自动上屏，交给 OnSelectAfter 统一处理。
  saved_auto_commit_ = ctx->get_option("_auto_commit");
  if (saved_auto_commit_)
    ctx->set_option("_auto_commit", false);
}

void IdsHybridProcessor::OnSelectAfter(Context* ctx) {
  if (!has_selection_)
    return;
  has_selection_ = false;
  const bool restore_auto_commit = saved_auto_commit_;
  saved_auto_commit_ = false;

  if (selected_is_result_) {
    engine_->CommitText(selected_text_);
    ctx->Clear();
  } else {
    HybridState st = ReadState(ctx, config_.prefix);
    std::string text = selected_component_;
    if (!text.empty() && !st.pending.empty() && st.pending[0] == '-' &&
        text[0] != '-') {
      text = "-" + text;
    } else if (!text.empty() && !st.pending.empty() && st.pending[0] == '+' &&
               text[0] != '+') {
      text = "+" + text;
    }
    st.components.push_back(text);
    st.codes.push_back(st.pending);
    SaveState(ctx, st.components, st.codes);
    ctx->PushInput("'");
  }

  if (restore_auto_commit && !ctx->get_option("_auto_commit"))
    ctx->set_option("_auto_commit", true);
}

void IdsHybridProcessor::OnUpdate(Context* ctx) {
  if (!IsActive(ctx, config_.prefix))
    return;
  if (!ctx->composition().empty())
    ctx->composition().back().prompt = config_.tips;
}

bool IdsHybridProcessor::HandleIdsHotkey(const rime::KeyEvent& key_event,
                                         Context* ctx) {
  if (!ctx || ctx->composition().empty())
    return false;
  const int code = key_event.keycode();
  const bool ctrl = key_event.ctrl();
  const bool alt = key_event.alt();
  const bool shift = key_event.shift();
  const bool is_return = (code == XK_Return || code == XK_KP_Enter);

  int digit = 0;
  if (code >= XK_1 && code <= XK_6)
    digit = code - XK_0;

  bool action = false;
  int ids_digit = 0;
  if (ctrl && !shift && !alt && is_return)
    action = true;
  else if (alt && !shift && !ctrl && is_return)
    action = true;
  else if (alt && !ctrl && (code == XK_i || code == XK_I))
    action = true;
  else if (ctrl && shift && (code == XK_i || code == XK_I))
    action = true;
  else if (ctrl && !shift && !alt && digit) {
    action = true;
    ids_digit = digit;
  } else if (alt && !shift && !ctrl && digit) {
    action = true;
    ids_digit = digit;
  }
  if (!action)
    return false;

  Segment& seg = ctx->composition().back();
  if (!seg.menu)
    return false;
  size_t index = seg.selected_index;
  if (ids_digit) {
    int page_size = engine_->schema() ? engine_->schema()->page_size() : 5;
    if (page_size <= 0)
      page_size = 5;
    size_t page_start =
        (seg.selected_index / static_cast<size_t>(page_size)) * page_size;
    index = page_start + static_cast<size_t>(ids_digit) - 1;
  }
  seg.menu->Prepare(index + 1);
  an<Candidate> cand = seg.GetCandidateAt(index);
  if (!cand || cand->text().empty())
    return false;
  if (!EnsureStoreLoaded(config_))
    return false;
  IdsStore& store = IdsStore::Instance();
  std::string ids = IdsOfText(store, cand->text());
  if (ids.empty())
    return false;
  engine_->CommitText(ids);
  ctx->Clear();
  return true;
}

bool IdsHybridProcessor::HandleSelectionKey(int digit, Context* ctx) {
  if (!ctx || ctx->composition().empty())
    return false;
  Segment& seg = ctx->composition().back();
  if (!seg.menu)
    return false;
  int page_size = engine_->schema() ? engine_->schema()->page_size() : 5;
  if (page_size <= 0)
    page_size = 5;
  size_t page_start =
      (seg.selected_index / static_cast<size_t>(page_size)) * page_size;
  size_t target = page_start + static_cast<size_t>(digit) - 1;
  size_t count = seg.menu->Prepare(target + 1);
  if (target >= count)
    return true;
  ctx->Select(target);
  return true;
}

rime::ProcessResult IdsHybridProcessor::ProcessKeyEvent(
    const rime::KeyEvent& key_event) {
  if (key_event.release())
    return rime::kNoop;
  Context* ctx = engine_ ? engine_->context() : nullptr;
  if (!ctx)
    return rime::kNoop;

  if (HandleIdsHotkey(key_event, ctx))
    return rime::kAccepted;

  const std::string repr = key_event.repr();
  const int code = key_event.keycode();
  const bool ctrl = key_event.ctrl();
  const bool alt = key_event.alt();

  if (!IsActive(ctx, config_.prefix)) {
    if (!ctx->get_option("ascii_mode") && !ctrl && !alt &&
        !ctx->IsComposing() && repr == config_.prefix) {
      ctx->PushInput(config_.prefix);
      return rime::kAccepted;
    }
    return rime::kNoop;
  }

  if (code == XK_Escape || repr == "Escape") {
    ctx->Clear();
    return rime::kAccepted;
  }

  if (code == XK_Return || code == XK_KP_Enter || repr == "Return") {
    HybridState st = ReadState(ctx, config_.prefix);
    if (st.components.empty() && st.pending.empty()) {
      engine_->CommitText(config_.prefix);
      ctx->Clear();
      return rime::kAccepted;
    }
    if (!st.pending.empty() && st.pending != "-" && st.pending != "+") {
      if (!ctx->composition().empty())
        ctx->composition().pop_back();
      st.components.push_back(st.pending);
      st.codes.push_back(st.pending);
      SaveState(ctx, st.components, st.codes);
      ctx->PushInput("'");
      return rime::kAccepted;
    }
    if (ctx->HasMenu() && ctx->GetSelectedCandidate()) {
      ctx->ConfirmCurrentSelection();
      return rime::kAccepted;
    }
    return rime::kNoop;
  }

  if (code == XK_BackSpace || repr == "BackSpace") {
    HybridState st = ReadState(ctx, config_.prefix);
    if (!st.pending.empty()) {
      if (st.pending.size() == 1 && !st.components.empty()) {
        if (!ctx->composition().empty())
          ctx->composition().pop_back();
        if (!ctx->composition().empty()) {
          Segment& sep = ctx->composition().back();
          sep.tags.clear();
          sep.tags.insert(config_.tag);
          sep.status = Segment::kVoid;
        }
      }
      ctx->PopInput(1);
    } else if (!st.components.empty()) {
      st.components.pop_back();
      st.codes.pop_back();
      SaveState(ctx, st.components, st.codes);
      if (!ctx->composition().empty())
        ctx->composition().pop_back();
      if (!ctx->composition().empty())
        ctx->composition().back().status = Segment::kGuess;
      ctx->PopInput(1);
    } else {
      ctx->Clear();
    }
    return rime::kAccepted;
  }

  if (!ctrl && !alt && code >= XK_1 && code <= XK_6) {
    if (ctx->HasMenu()) {
      HandleSelectionKey(code - XK_0, ctx);
      return rime::kAccepted;
    }
    return rime::kNoop;
  }
  if (!ctrl && !alt && (code == XK_semicolon || repr == "semicolon")) {
    if (ctx->HasMenu()) {
      HandleSelectionKey(2, ctx);
      return rime::kAccepted;
    }
    return rime::kNoop;
  }
  if (code == XK_space || repr == "space") {
    if (!key_event.shift() && ctx->HasMenu() && ctx->GetSelectedCandidate()) {
      ctx->ConfirmCurrentSelection();
      return rime::kAccepted;
    }
    return rime::kNoop;
  }

  bool is_letter = !ctrl && !alt && repr.size() == 1 &&
                   std::isalpha(static_cast<unsigned char>(repr[0]));
  bool is_minus =
      !ctrl && !alt && (code == XK_minus || repr == "minus" || repr == "-");
  bool is_plus = !ctrl && !alt &&
                 (code == XK_plus || code == XK_equal || repr == "plus" ||
                  repr == "equal" || repr == "+" || repr == "=");

  if (is_letter || is_minus || is_plus) {
    char ch = is_minus ? '-' : (is_plus ? '+' : repr[0]);
    if (ch >= 'A' && ch <= 'Z')
      ch = static_cast<char>(ch - 'A' + 'a');
    if (ctx->caret_pos() != ctx->input().size())
      ctx->set_caret_pos(ctx->input().size());
    if (!ctx->composition().empty() &&
        ctx->composition().back().HasTag(config_.tag)) {
      ctx->ClearNonConfirmedComposition();
    }
    ctx->PushInput(std::string(1, ch));
    return rime::kAccepted;
  }

  return rime::kNoop;
}

// ---------------------------------------------------------------------------
// Segmentor
// ---------------------------------------------------------------------------

IdsHybridSegmentor::IdsHybridSegmentor(const rime::Ticket& ticket)
    : Segmentor(ticket) {
  LoadConfig(ticket, "ids_hybrid", &name_space_, &config_);
}

bool IdsHybridSegmentor::Proceed(Segmentation* segmentation) {
  if (!segmentation)
    return false;
  Context* ctx = engine_ ? engine_->context() : nullptr;
  if (!IsActive(ctx, config_.prefix))
    return true;

  const std::string& input = segmentation->input();
  size_t start = segmentation->GetCurrentStartPosition();
  if (start >= input.size())
    return false;

  size_t prefix_end = (start == 0) ? config_.prefix.size() : start + 1;
  bool is_prefix =
      (start == 0) || (start < input.size() && input[start] == '\'');

  Segment seg;
  if (is_prefix && prefix_end < input.size()) {
    seg = Segment(static_cast<int>(start), static_cast<int>(prefix_end));
    seg.tags.insert("phony");
    seg.status = Segment::kConfirmed;
  } else if (is_prefix) {
    seg = Segment(static_cast<int>(start), static_cast<int>(input.size()));
    if (start == 0) {
      seg.tags.insert(config_.tag);
    } else {
      seg.tags.insert(config_.tag);
      seg.tags.insert("ids_search");
    }
  } else {
    size_t next_quote = input.find('\'', start + 1);
    char first_char = (start < input.size()) ? input[start] : '\0';
    if (first_char == '-' || first_char == '+') {
      bool has_code_after =
          (start + 1 < input.size()) &&
          (next_quote == std::string::npos || next_quote > start + 1);
      if (has_code_after) {
        seg = Segment(static_cast<int>(start), static_cast<int>(start + 1));
        seg.status = Segment::kConfirmed;
        seg.tags.insert("confirmed_component");
      } else if (start + 1 == input.size()) {
        seg = Segment(static_cast<int>(start), static_cast<int>(input.size()));
        seg.tags.insert(config_.tag);
        seg.tags.insert("ids_search");
      } else {
        seg = Segment(static_cast<int>(start), static_cast<int>(start + 1));
        seg.status = Segment::kConfirmed;
        seg.tags.insert("confirmed_component");
      }
    } else if (next_quote != std::string::npos) {
      seg = Segment(static_cast<int>(start), static_cast<int>(next_quote));
      seg.status = Segment::kConfirmed;
      seg.tags.insert("confirmed_component");
    } else {
      seg = Segment(static_cast<int>(start), static_cast<int>(input.size()));
      seg.tags.insert("abc");
      seg.tags.insert("hanzi_component");
    }
  }

  seg.prompt = config_.tips;
  segmentation->AddSegment(seg);
  return false;
}

// ---------------------------------------------------------------------------
// Translator
// ---------------------------------------------------------------------------

IdsHybridTranslator::IdsHybridTranslator(const rime::Ticket& ticket)
    : Translator(ticket) {
  LoadConfig(ticket, "ids_hybrid", &name_space_, &config_);
}

bool IdsHybridTranslator::EnsureLoaded() {
  if (!EnsureStoreLoaded(config_))
    return false;
  ApplyCustomDisabledBlocks(&config_.disabled_blocks,
                            &config_.disabled_blocks_enabled);
  return true;
}

std::string IdsHybridTranslator::PinyinMode() {
  return ids_search::PinyinMode(engine_, config_);
}

void IdsHybridTranslator::BuildResultCandidates(
    IdsStore& store,
    const std::vector<uint32_t>& results,
    const Segment& segment,
    const std::string& pending,
    rime::FifoTranslation* translation) {
  bool show_pinyin = config_.show_pinyin;
  bool show_ids = config_.show_ids;
  bool show_unicode = config_.show_unicode;
  bool show_block = config_.show_block;
  Context* ctx = engine_ ? engine_->context() : nullptr;
  if (ctx) {
    if (config_.has_sw_pinyin)
      show_pinyin = ctx->get_option("show_pinyin");
    if (config_.has_sw_ids)
      show_ids = ctx->get_option("show_ids");
    if (config_.has_sw_unicode)
      show_unicode = ctx->get_option("show_unicode");
    if (config_.has_sw_block)
      show_block = ctx->get_option("show_block");
  }
  std::string preedit = "\t";
  if (pending == "-")
    preedit = "-\t";
  else if (pending == "+")
    preedit = "+\t";

  for (uint32_t id : results) {
    uint32_t cp = store.CpOfId(id);
    std::string text = CpToUtf8(cp);
    std::string comment =
        BuildComment(store, id, show_pinyin, show_ids, show_unicode, show_block,
                     config_.ids_format);
    auto cand = New<rime::SimpleCandidate>("ids_search", segment.start,
                                           segment.end, text, comment, preedit);
    cand->set_quality(100.0);
    translation->Append(cand);
  }
}

void IdsHybridTranslator::BuildComponentCandidates(
    IdsStore& store,
    const std::vector<uint32_t>& results,
    const Segment& segment,
    rime::FifoTranslation* translation) {
  for (uint32_t id : results) {
    if (config_.disabled_blocks_enabled &&
        config_.disabled_blocks.count(static_cast<int>(store.Hot(id).block))) {
      continue;
    }
    uint32_t cp = store.CpOfId(id);
    std::string text = CpToUtf8(cp);
    auto cand = New<rime::SimpleCandidate>("hanzi_component", segment.start,
                                           segment.end, text, "");
    cand->set_quality(100.0);
    translation->Append(cand);
  }
}

an<rime::Translation> IdsHybridTranslator::Query(const std::string& input,
                                                 const Segment& segment) {
  Context* ctx = engine_ ? engine_->context() : nullptr;
  if (!ctx || !IsActive(ctx, config_.prefix))
    return nullptr;
  bool has_hybrid = segment.HasTag(config_.tag);
  bool has_component = segment.HasTag("hanzi_component");
  if (!has_hybrid && !has_component)
    return nullptr;
  if (!EnsureLoaded())
    return nullptr;

  IdsStore& store = IdsStore::Instance();
  HybridState st = ReadState(ctx, config_.prefix);
  auto translation = New<rime::FifoTranslation>();
  const std::string& pending = st.pending;

  if ((pending.empty() || pending == "-" || pending == "+") &&
      !st.components.empty()) {
    std::vector<uint32_t> results = SearchHybrid(
        store, st.components, PinyinMode(), config_.max_results,
        config_.disabled_blocks_enabled ? &config_.disabled_blocks : nullptr);
    BuildResultCandidates(store, results, segment, pending, translation.get());
  } else if (!pending.empty() && pending != "-" && pending != "+" &&
             has_component) {
    std::vector<uint32_t> results =
        GetComponentCandidates(store, pending, PinyinMode(), 80);
    BuildComponentCandidates(store, results, segment, translation.get());
  }

  if (translation->size() == 0)
    return nullptr;
  return translation;
}

// ---------------------------------------------------------------------------
// Filter / Cleaner
// ---------------------------------------------------------------------------

namespace {

class HybridFilterTranslation : public rime::PrefetchTranslation {
 public:
  HybridFilterTranslation(an<rime::Translation> translation,
                          Context* ctx,
                          bool cleaner)
      : PrefetchTranslation(translation), ctx_(ctx), cleaner_(cleaner) {}

  an<Candidate> Peek() override {
    if (exhausted())
      return nullptr;
    if (!cache_.empty())
      return cache_.front();
    if (!Replenish()) {
      // 不要把未过滤的底层候选回退出去。
      set_exhausted(true);
      return nullptr;
    }
    return cache_.front();
  }

 protected:
  bool Replenish() override {
    // 防止底层翻译器（预测、句子等）产生海量候选时长时间扫描。
    constexpr size_t kMaxScan = 2000;
    size_t scanned = 0;
    while (cache_.empty()) {
      if (scanned++ >= kMaxScan)
        break;
      an<Candidate> next = translation_->Peek();
      if (!next) {
        translation_->Next();
        if (translation_->exhausted())
          break;
        continue;
      }
      translation_->Next();
      an<Candidate> genuine = Candidate::GetGenuineCandidate(next);
      const std::string& type = genuine ? genuine->type() : next->type();
      if (type == "ids_search" || type == "hanzi_result") {
        cache_.push_back(next);
        continue;
      }
      if (!ctx_ || ctx_->composition().empty()) {
        cache_.push_back(next);
        continue;
      }
      Segment& seg = ctx_->composition().back();
      const bool component_stage = seg.HasTag("hanzi_component");
      if (!component_stage) {
        if (cleaner_)
          cache_.push_back(next);
        continue;
      }
      if (next->end() == seg.end && IsSingleChar(next->text()) &&
          seen_.insert(next->text()).second) {
        cache_.push_back(New<rime::ShadowCandidate>(next, "hanzi_component"));
      }
    }
    return !cache_.empty();
  }

 private:
  Context* ctx_;
  bool cleaner_;
  std::set<std::string> seen_;
};

}  // namespace

IdsHybridFilter::IdsHybridFilter(const rime::Ticket& ticket) : Filter(ticket) {
  LoadConfig(ticket, "ids_hybrid", &name_space_, &config_);
  cleaner_ = (ticket.klass == "ids_hybrid_cleaner");
}

an<rime::Translation> IdsHybridFilter::Apply(an<rime::Translation> translation,
                                             rime::CandidateList* candidates) {
  (void)candidates;
  Context* ctx = engine_ ? engine_->context() : nullptr;
  if (!ctx)
    return translation;

  if (!IsActive(ctx, config_.prefix))
    return translation;

  // 初始 O / 前缀段既不是部件选择段，也不是结果段。
  // 直接返回空翻译，避免遍历完整词典或预测翻译器的海量候选。
  if (ctx->composition().empty())
    return New<rime::FifoTranslation>();
  Segment& seg = ctx->composition().back();
  if (!seg.HasTag("hanzi_component") && !seg.HasTag("ids_search"))
    return New<rime::FifoTranslation>();

  return New<HybridFilterTranslation>(translation, ctx, cleaner_);
}

}  // namespace ids_search

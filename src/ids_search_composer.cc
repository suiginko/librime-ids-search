//
// ids_search_composer.cc — I 模式原生触发组件。
//
#include "ids_search_composer.h"

#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/key_event.h>
#include <rime/schema.h>
#include <rime/segmentation.h>
#include <rime/translation.h>

namespace ids_search {

namespace {

bool StartsWith(const std::string& s, const std::string& prefix) {
  return !prefix.empty() && s.size() >= prefix.size() &&
         s.compare(0, prefix.size(), prefix) == 0;
}

std::string DefaultNamespace(const rime::Ticket& ticket) {
  if (ticket.name_space.empty() || ticket.name_space == "processor" ||
      ticket.name_space == "segmentor") {
    return "ids_search";
  }
  return ticket.name_space;
}

void LoadConfig(const rime::Ticket& ticket,
                std::string* name_space,
                SearchTriggerConfig* config) {
  *name_space = DefaultNamespace(ticket);
  const std::string ns = *name_space;
  if (!ticket.engine || !ticket.engine->schema())
    return;
  rime::Config* conf = ticket.engine->schema()->config();
  if (!conf)
    return;

  conf->GetString(ns + "/prefix", &config->prefix);
  conf->GetString(ns + "/tag", &config->tag);
  conf->GetString(ns + "/tips", &config->tips);

  if (config->prefix.empty())
    config->prefix = "I";
  if (config->tag.empty())
    config->tag = "ids_search";
  if (config->tips.empty())
    config->tips = "〔部件检字〕";
}

}  // namespace

IdsSearchProcessor::IdsSearchProcessor(const rime::Ticket& ticket)
    : Processor(ticket) {
  LoadConfig(ticket, &name_space_, &config_);
}

rime::ProcessResult IdsSearchProcessor::ProcessKeyEvent(
    const rime::KeyEvent& key_event) {
  if (key_event.release() || key_event.ctrl() || key_event.alt() ||
      key_event.super()) {
    return rime::kNoop;
  }

  rime::Context* ctx = engine_ ? engine_->context() : nullptr;
  if (!ctx || ctx->get_option("ascii_mode") || ctx->IsComposing())
    return rime::kNoop;

  if (key_event.repr() != config_.prefix)
    return rime::kNoop;

  ctx->PushInput(config_.prefix);
  return rime::kAccepted;
}

IdsSearchSegmentor::IdsSearchSegmentor(const rime::Ticket& ticket)
    : Segmentor(ticket) {
  LoadConfig(ticket, &name_space_, &config_);
}

bool IdsSearchSegmentor::Proceed(rime::Segmentation* segmentation) {
  if (!segmentation)
    return false;
  rime::Context* ctx = engine_ ? engine_->context() : nullptr;
  if (!ctx || ctx->get_option("ascii_mode"))
    return true;

  const std::string& input = segmentation->input();
  if (segmentation->GetCurrentStartPosition() != 0)
    return true;
  if (!StartsWith(input, config_.prefix))
    return true;

  rime::Segment seg(0, static_cast<int>(input.size()));
  seg.tags.insert(config_.tag);
  seg.prompt = config_.tips;

  if (!segmentation->AddSegment(seg))
    return true;

  if (!segmentation->empty())
    segmentation->back().prompt = config_.tips;
  return false;
}

IdsSearchFilter::IdsSearchFilter(const rime::Ticket& ticket) : Filter(ticket) {
  LoadConfig(ticket, &name_space_, &config_);
}

bool IdsSearchFilter::AppliesToSegment(rime::Segment* segment) {
  suppress_ = false;
  rime::Context* ctx = engine_ ? engine_->context() : nullptr;
  if (!ctx || !segment || ctx->get_option("ascii_mode"))
    return false;
  if (ctx->input() != config_.prefix)
    return false;
  if (segment->start != 0 ||
      segment->end != static_cast<size_t>(config_.prefix.size())) {
    return false;
  }
  suppress_ = true;
  return true;
}

rime::an<rime::Translation> IdsSearchFilter::Apply(
    rime::an<rime::Translation> translation,
    rime::CandidateList* candidates) {
  (void)candidates;
  if (!suppress_)
    return translation;
  suppress_ = false;
  return rime::New<rime::FifoTranslation>();
}

}  // namespace ids_search

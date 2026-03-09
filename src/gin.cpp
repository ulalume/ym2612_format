#include "ym2612_format/gin.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace ym2612_format::gin {

FormatInfo info() { return {Format::Gin, "GIN (JSON)", "gin", true, true, false}; }

namespace {

using json = nlohmann::json;

Operator op_from_json(const json &j) {
  Operator op;
  op.ar = j.at("attack_rate").get<uint8_t>();
  op.dr = j.at("decay_rate").get<uint8_t>();
  op.sr = j.at("sustain_rate").get<uint8_t>();
  op.rr = j.at("release_rate").get<uint8_t>();
  op.sl = j.at("sustain_level").get<uint8_t>();
  op.tl = j.at("total_level").get<uint8_t>();
  op.ks = j.at("key_scale").get<uint8_t>();
  op.ml = j.at("multiple").get<uint8_t>();
  op.dt = j.at("detune").get<uint8_t>();
  op.ssg = j.at("ssg_type_envelope_control").get<uint8_t>();
  op.ssg_enable = j.at("ssg_enable").get<bool>();
  op.am = j.at("amplitude_modulation_enable").get<bool>();
  if (j.contains("enable"))
    op.enable = j.at("enable").get<bool>();
  return op;
}

json op_to_json(const Operator &op) {
  return json{
      {"attack_rate", op.ar},
      {"decay_rate", op.dr},
      {"sustain_rate", op.sr},
      {"release_rate", op.rr},
      {"sustain_level", op.sl},
      {"total_level", op.tl},
      {"key_scale", op.ks},
      {"multiple", op.ml},
      {"detune", op.dt},
      {"ssg_type_envelope_control", op.ssg},
      {"ssg_enable", op.ssg_enable},
      {"amplitude_modulation_enable", op.am},
      {"enable", op.enable},
  };
}

// ---- Macro JSON support ----

json macro_to_json(const Macro &m) {
  json j;
  j["values"] = m.values;
  if (m.loop != 255) j["loop"] = m.loop;
  if (m.release != 255) j["release"] = m.release;
  if (m.speed != 1) j["speed"] = m.speed;
  if (m.delay != 0) j["delay"] = m.delay;
  if (m.type != MacroType::Sequence)
    j["type"] = static_cast<uint8_t>(m.type);
  return j;
}

Macro macro_from_json(const json &j) {
  Macro m;
  m.values = j.at("values").get<std::vector<int32_t>>();
  m.loop = j.contains("loop") ? j["loop"].get<uint8_t>() : 255;
  m.release = j.contains("release") ? j["release"].get<uint8_t>() : 255;
  m.speed = j.contains("speed") ? j["speed"].get<uint8_t>() : 1;
  m.delay = j.contains("delay") ? j["delay"].get<uint8_t>() : 0;
  m.type = j.contains("type")
      ? static_cast<MacroType>(j["type"].get<uint8_t>())
      : MacroType::Sequence;
  return m;
}

void write_macro_if(json &obj, const char *key, const Macro &m) {
  if (!m.empty()) obj[key] = macro_to_json(m);
}

void read_macro_if(const json &obj, const char *key, Macro &m) {
  if (obj.contains(key)) m = macro_from_json(obj.at(key));
}

json channel_macros_to_json(const ChannelMacros &cm) {
  json j = json::object();
  write_macro_if(j, "volume", cm.volume);
  write_macro_if(j, "arpeggio", cm.arpeggio);
  write_macro_if(j, "duty", cm.duty);
  write_macro_if(j, "wave", cm.wave);
  write_macro_if(j, "pitch", cm.pitch);
  write_macro_if(j, "ex1", cm.ex1);
  write_macro_if(j, "ex2", cm.ex2);
  write_macro_if(j, "ex3", cm.ex3);
  write_macro_if(j, "algorithm", cm.algorithm);
  write_macro_if(j, "feedback", cm.feedback);
  write_macro_if(j, "fms", cm.fms);
  write_macro_if(j, "ams", cm.ams);
  write_macro_if(j, "pan_left", cm.pan_left);
  write_macro_if(j, "pan_right", cm.pan_right);
  write_macro_if(j, "phase_reset", cm.phase_reset);
  return j;
}

ChannelMacros channel_macros_from_json(const json &j) {
  ChannelMacros cm;
  read_macro_if(j, "volume", cm.volume);
  read_macro_if(j, "arpeggio", cm.arpeggio);
  read_macro_if(j, "duty", cm.duty);
  read_macro_if(j, "wave", cm.wave);
  read_macro_if(j, "pitch", cm.pitch);
  read_macro_if(j, "ex1", cm.ex1);
  read_macro_if(j, "ex2", cm.ex2);
  read_macro_if(j, "ex3", cm.ex3);
  read_macro_if(j, "algorithm", cm.algorithm);
  read_macro_if(j, "feedback", cm.feedback);
  read_macro_if(j, "fms", cm.fms);
  read_macro_if(j, "ams", cm.ams);
  read_macro_if(j, "pan_left", cm.pan_left);
  read_macro_if(j, "pan_right", cm.pan_right);
  read_macro_if(j, "phase_reset", cm.phase_reset);
  return cm;
}

json op_macros_to_json(const OperatorMacros &om) {
  json j = json::object();
  write_macro_if(j, "tl", om.tl);
  write_macro_if(j, "ar", om.ar);
  write_macro_if(j, "dr", om.dr);
  write_macro_if(j, "d2r", om.d2r);
  write_macro_if(j, "rr", om.rr);
  write_macro_if(j, "sl", om.sl);
  write_macro_if(j, "dt", om.dt);
  write_macro_if(j, "ml", om.ml);
  write_macro_if(j, "rs", om.rs);
  write_macro_if(j, "ssg", om.ssg);
  write_macro_if(j, "am", om.am);
  return j;
}

OperatorMacros op_macros_from_json(const json &j) {
  OperatorMacros om;
  read_macro_if(j, "tl", om.tl);
  read_macro_if(j, "ar", om.ar);
  read_macro_if(j, "dr", om.dr);
  read_macro_if(j, "d2r", om.d2r);
  read_macro_if(j, "rr", om.rr);
  read_macro_if(j, "sl", om.sl);
  read_macro_if(j, "dt", om.dt);
  read_macro_if(j, "ml", om.ml);
  read_macro_if(j, "rs", om.rs);
  read_macro_if(j, "ssg", om.ssg);
  read_macro_if(j, "am", om.am);
  return om;
}

} // namespace

ParseResult parse(const uint8_t *data, size_t size, const std::string &name) {
  if (!data || size == 0)
    return Error{"Empty data"};

  try {
    auto j = json::parse(data, data + size);

    Patch patch;
    patch.name = j.at("name").get<std::string>();

    // Device / global
    auto &dev = j.at("device");
    patch.dac_enable = dev.at("dac_enable").get<bool>();
    patch.lfo_enable = dev.at("lfo_enable").get<bool>();
    patch.lfo_frequency = dev.at("lfo_frequency").get<uint8_t>();

    // Channel
    auto &ch = j.at("channel");
    patch.left = ch.at("left_speaker").get<bool>();
    patch.right = ch.at("right_speaker").get<bool>();
    patch.ams =
        ch.at("amplitude_modulation_sensitivity").get<uint8_t>();
    patch.fms =
        ch.at("frequency_modulation_sensitivity").get<uint8_t>();

    // Instrument
    auto &inst = j.at("instrument");
    patch.feedback = inst.at("feedback").get<uint8_t>();
    patch.algorithm = inst.at("algorithm").get<uint8_t>();
    auto &ops = inst.at("operators");
    for (size_t i = 0; i < 4; ++i)
      patch.operators[i] = op_from_json(ops[i]);

    // Macros (optional — missing = no macros, backward compatible)
    if (j.contains("macros")) {
      auto &mc = j.at("macros");
      if (mc.contains("channel"))
        patch.macros = channel_macros_from_json(mc.at("channel"));
      if (mc.contains("operators")) {
        auto &ops_macros = mc.at("operators");
        for (size_t i = 0; i < 4 && i < ops_macros.size(); ++i)
          patch.operator_macros[i] = op_macros_from_json(ops_macros[i]);
      }
    }

    if (patch.name.empty() && !name.empty())
      patch.name = name;

    return ParseOk{{patch}, {}};
  } catch (const std::exception &e) {
    return Error{std::string("JSON parse error: ") + e.what()};
  }
}

SerializeResult serialize(const Patch &patch) {
  try {
    json ops = json::array();
    for (const auto &op : patch.operators)
      ops.push_back(op_to_json(op));

    json j = {
        {"name", patch.name},
        {"device",
         {{"dac_enable", patch.dac_enable},
          {"lfo_enable", patch.lfo_enable},
          {"lfo_frequency", patch.lfo_frequency}}},
        {"channel",
         {{"left_speaker", patch.left},
          {"right_speaker", patch.right},
          {"amplitude_modulation_sensitivity", patch.ams},
          {"frequency_modulation_sensitivity", patch.fms}}},
        {"instrument",
         {{"feedback", patch.feedback},
          {"algorithm", patch.algorithm},
          {"operators", ops}}},
    };

    // Macros (only written when present, backward compatible)
    if (patch.has_macros()) {
      json macros = json::object();

      auto ch_json = channel_macros_to_json(patch.macros);
      if (!ch_json.empty())
        macros["channel"] = std::move(ch_json);

      // Operator macros: always write an array of 4 objects
      bool any_op_macros = false;
      for (const auto &om : patch.operator_macros) {
        if (!om.empty()) { any_op_macros = true; break; }
      }
      if (any_op_macros) {
        json ops_arr = json::array();
        for (const auto &om : patch.operator_macros)
          ops_arr.push_back(op_macros_to_json(om));
        macros["operators"] = std::move(ops_arr);
      }

      if (!macros.empty())
        j["macros"] = std::move(macros);
    }

    std::string text = j.dump(2);
    return std::vector<uint8_t>(text.begin(), text.end());
  } catch (const std::exception &e) {
    return Error{std::string("JSON serialize error: ") + e.what()};
  }
}

} // namespace ym2612_format::gin

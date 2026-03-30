#include "ym2612_format/gin.hpp"

#include "json_minimal.hpp"
#include <string>

namespace ym2612_format::gin {

FormatInfo info() { return {Format::Gin, "GIN (JSON)", "gin", true, true, false}; }

namespace {

using Json = json_minimal::Value;

Operator op_from_json(const Json &j) {
  Operator op;
  op.ar = j.at("attack_rate").get_uint8();
  op.dr = j.at("decay_rate").get_uint8();
  op.sr = j.at("sustain_rate").get_uint8();
  op.rr = j.at("release_rate").get_uint8();
  op.sl = j.at("sustain_level").get_uint8();
  op.tl = j.at("total_level").get_uint8();
  op.ks = j.at("key_scale").get_uint8();
  op.ml = j.at("multiple").get_uint8();
  op.dt = j.at("detune").get_uint8();
  op.ssg = j.at("ssg_type_envelope_control").get_uint8();
  op.ssg_enable = j.at("ssg_enable").get_bool();
  op.am = j.at("amplitude_modulation_enable").get_bool();
  if (j.contains("enable"))
    op.enable = j.at("enable").get_bool();
  return op;
}

Json op_to_json(const Operator &op) {
  Json j = Json::object();
  j["attack_rate"] = Json(static_cast<int>(op.ar));
  j["decay_rate"] = Json(static_cast<int>(op.dr));
  j["sustain_rate"] = Json(static_cast<int>(op.sr));
  j["release_rate"] = Json(static_cast<int>(op.rr));
  j["sustain_level"] = Json(static_cast<int>(op.sl));
  j["total_level"] = Json(static_cast<int>(op.tl));
  j["key_scale"] = Json(static_cast<int>(op.ks));
  j["multiple"] = Json(static_cast<int>(op.ml));
  j["detune"] = Json(static_cast<int>(op.dt));
  j["ssg_type_envelope_control"] = Json(static_cast<int>(op.ssg));
  j["ssg_enable"] = Json(op.ssg_enable);
  j["amplitude_modulation_enable"] = Json(op.am);
  j["enable"] = Json(op.enable);
  return j;
}

// ---- Macro JSON support ----

Json macro_to_json(const Macro &m) {
  Json j = Json::object();
  Json vals = Json::array();
  for (auto v : m.values) vals.push_back(Json(static_cast<int64_t>(v)));
  j["values"] = std::move(vals);
  if (m.loop != 255) j["loop"] = Json(static_cast<int>(m.loop));
  if (m.release != 255) j["release"] = Json(static_cast<int>(m.release));
  if (m.speed != 1) j["speed"] = Json(static_cast<int>(m.speed));
  if (m.delay != 0) j["delay"] = Json(static_cast<int>(m.delay));
  if (m.type != MacroType::Sequence)
    j["type"] = Json(static_cast<int>(static_cast<uint8_t>(m.type)));
  return j;
}

Macro macro_from_json(const Json &j) {
  Macro m;
  m.values = j.at("values").get_int32_vec();
  m.loop = j.contains("loop") ? j.at("loop").get_uint8() : 255;
  m.release = j.contains("release") ? j.at("release").get_uint8() : 255;
  m.speed = j.contains("speed") ? j.at("speed").get_uint8() : 1;
  m.delay = j.contains("delay") ? j.at("delay").get_uint8() : 0;
  m.type = j.contains("type")
      ? static_cast<MacroType>(j.at("type").get_uint8())
      : MacroType::Sequence;
  return m;
}

void write_macro_if(Json &obj, const char *key, const Macro &m) {
  if (!m.empty()) obj[key] = macro_to_json(m);
}

void read_macro_if(const Json &obj, const char *key, Macro &m) {
  if (obj.contains(key)) m = macro_from_json(obj.at(key));
}

Json channel_macros_to_json(const ChannelMacros &cm) {
  Json j = Json::object();
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

ChannelMacros channel_macros_from_json(const Json &j) {
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

Json op_macros_to_json(const OperatorMacros &om) {
  Json j = Json::object();
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

OperatorMacros op_macros_from_json(const Json &j) {
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
    auto j = Json::parse(data, size);

    Patch patch;
    patch.name = j.at("name").get_string();

    // Device / global
    const auto &dev = j.at("device");
    patch.dac_enable = dev.at("dac_enable").get_bool();
    patch.lfo_enable = dev.at("lfo_enable").get_bool();
    patch.lfo_frequency = dev.at("lfo_frequency").get_uint8();

    // Channel
    const auto &ch = j.at("channel");
    patch.left = ch.at("left_speaker").get_bool();
    patch.right = ch.at("right_speaker").get_bool();
    patch.ams =
        ch.at("amplitude_modulation_sensitivity").get_uint8();
    patch.fms =
        ch.at("frequency_modulation_sensitivity").get_uint8();

    // Instrument
    const auto &inst = j.at("instrument");
    patch.feedback = inst.at("feedback").get_uint8();
    patch.algorithm = inst.at("algorithm").get_uint8();
    const auto &ops = inst.at("operators");
    for (size_t i = 0; i < 4; ++i)
      patch.operators[i] = op_from_json(ops[i]);

    // Macros (optional — missing = no macros, backward compatible)
    if (j.contains("macros")) {
      const auto &mc = j.at("macros");
      if (mc.contains("channel"))
        patch.macros = channel_macros_from_json(mc.at("channel"));
      if (mc.contains("operators")) {
        const auto &ops_macros = mc.at("operators");
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
    Json ops = Json::array();
    for (const auto &op : patch.operators)
      ops.push_back(op_to_json(op));

    Json inst = Json::object();
    inst["feedback"] = Json(static_cast<int>(patch.feedback));
    inst["algorithm"] = Json(static_cast<int>(patch.algorithm));
    inst["operators"] = std::move(ops);

    Json dev = Json::object();
    dev["dac_enable"] = Json(patch.dac_enable);
    dev["lfo_enable"] = Json(patch.lfo_enable);
    dev["lfo_frequency"] = Json(static_cast<int>(patch.lfo_frequency));

    Json ch = Json::object();
    ch["left_speaker"] = Json(patch.left);
    ch["right_speaker"] = Json(patch.right);
    ch["amplitude_modulation_sensitivity"] = Json(static_cast<int>(patch.ams));
    ch["frequency_modulation_sensitivity"] = Json(static_cast<int>(patch.fms));

    Json j = Json::object();
    j["name"] = Json(patch.name);
    j["device"] = std::move(dev);
    j["channel"] = std::move(ch);
    j["instrument"] = std::move(inst);

    // Macros (only written when present, backward compatible)
    if (patch.has_macros()) {
      Json macros = Json::object();

      auto ch_json = channel_macros_to_json(patch.macros);
      if (!ch_json.empty())
        macros["channel"] = std::move(ch_json);

      // Operator macros: always write an array of 4 objects
      bool any_op_macros = false;
      for (const auto &om : patch.operator_macros) {
        if (!om.empty()) { any_op_macros = true; break; }
      }
      if (any_op_macros) {
        Json ops_arr = Json::array();
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

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

    std::string text = j.dump(2);
    return std::vector<uint8_t>(text.begin(), text.end());
  } catch (const std::exception &e) {
    return Error{std::string("JSON serialize error: ") + e.what()};
  }
}

} // namespace ym2612_format::gin

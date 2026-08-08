#include "ClassifierGroupEditor.h"

#include "UIAutocomplete.h"

namespace RPP::ClassifierEditor
{
	namespace
	{
		std::vector<std::string> ToStringList(const nlohmann::ordered_json& a_value)
		{
			std::vector<std::string> result;
			if (a_value.is_array()) {
				for (auto& v : a_value) {
					if (v.is_string()) {
						result.push_back(v.get<std::string>());
					}
				}
			} else if (a_value.is_string()) {
				result.push_back(a_value.get<std::string>());
			}
			return result;
		}

		template <std::size_t N>
		std::vector<std::array<char, N>> ToBufferList(const std::vector<std::string>& a_values)
		{
			std::vector<std::array<char, N>> result;
			result.reserve(a_values.size());
			for (const auto& v : a_values) {
				std::array<char, N> buf{};
				UI::CopyIntoBuffer(buf, v);
				result.push_back(buf);
			}
			return result;
		}

		template <std::size_t N>
		std::vector<std::string> FromBufferList(const std::vector<std::array<char, N>>& a_buffers)
		{
			std::vector<std::string> result;
			result.reserve(a_buffers.size());
			for (const auto& buf : a_buffers) {
				if (buf[0] != '\0') {
					result.emplace_back(buf.data());
				}
			}
			return result;
		}

		EditableClassifierRule ParseRule(const nlohmann::ordered_json& a_ruleJson)
		{
			EditableClassifierRule rule{};
			UI::CopyIntoBuffer(rule.comment, a_ruleJson.value("comment", ""));

			if (a_ruleJson.contains("match")) {
				rule.match = ParseMatch(a_ruleJson["match"]);
			}

			if (a_ruleJson.contains("conditions") && a_ruleJson["conditions"].is_array()) {
				for (auto& c : a_ruleJson["conditions"]) {
					ConditionSpec spec{};
					spec.function = c.value("function", std::string{ "HasPerk" });
					spec.param1 = c.value("param1", "");
					spec.param2 = c.value("param2", "");
					spec.op = c.value("operator", std::string{ "==" });
					spec.value = c.value("value", std::string{ "1" });
					spec.runOn = c.value("runOn", 0);
					spec.logic = c.value("logic", std::string{ "AND" });
					rule.conditions.push_back(UI::FromSpec(spec));
				}
			}

			if (a_ruleJson.contains("setGlobal")) {
				for (const auto& name : ToStringList(a_ruleJson["setGlobal"])) {
					ConditionSpec spec{};
					spec.function = "GetGlobalValue";
					spec.param1 = name;
					spec.op = "==";
					spec.value = "1";
					rule.conditions.push_back(UI::FromSpec(spec));
				}
			}

			return rule;
		}

		nlohmann::ordered_json SerializeRule(const EditableClassifierRule& a_rule)
		{
			nlohmann::ordered_json j;

			const std::string comment{ a_rule.comment.data() };
			if (!comment.empty()) {
				j["comment"] = comment;
			}

			nlohmann::ordered_json matchJson = SerializeMatch(a_rule.match);
			if (!matchJson.is_null()) {
				j["match"] = std::move(matchJson);
			}

			if (!a_rule.conditions.empty()) {
				nlohmann::ordered_json conditions = nlohmann::ordered_json::array();
				for (const auto& c : a_rule.conditions) {
					const ConditionSpec spec = UI::EditableCondition::ToSpec(c);
					nlohmann::ordered_json cj;
					cj["function"] = spec.function;
					cj["param1"] = spec.param1;
					if (!spec.param2.empty()) {
						cj["param2"] = spec.param2;
					}
					cj["operator"] = spec.op;
					cj["value"] = spec.value;
					cj["runOn"] = spec.runOn;
					cj["logic"] = spec.logic;
					conditions.push_back(std::move(cj));
				}
				j["conditions"] = std::move(conditions);
			}

			return j;
		}
	}

	std::vector<EditableClassifierGroup> ParseClassifierGroups(const nlohmann::ordered_json& a_config)
	{
		std::vector<EditableClassifierGroup> result;

		if (!a_config.contains("classifiers") || !a_config["classifiers"].is_array()) {
			return result;
		}

		for (auto& groupJson : a_config["classifiers"]) {
			EditableClassifierGroup group{};
			UI::CopyIntoBuffer(group.comment, groupJson.value("comment", ""));

			if (groupJson.contains("benchKeyword")) {
				group.benchKeywords = ToBufferList<64>(ToStringList(groupJson["benchKeyword"]));
			}
			if (groupJson.contains("when")) {
				group.when = ParseMatch(groupJson["when"]);
			}

			if (groupJson.contains("rules") && groupJson["rules"].is_array()) {
				for (auto& ruleJson : groupJson["rules"]) {
					group.rules.push_back(ParseRule(ruleJson));
				}
			}

			result.push_back(std::move(group));
		}

		return result;
	}

	nlohmann::ordered_json SerializeClassifierGroups(const std::vector<EditableClassifierGroup>& a_groups)
	{
		nlohmann::ordered_json arr = nlohmann::ordered_json::array();

		for (const auto& group : a_groups) {
			nlohmann::ordered_json j;

			const std::string comment{ group.comment.data() };
			if (!comment.empty()) {
				j["comment"] = comment;
			}

			const auto benches = FromBufferList(group.benchKeywords);
			if (!benches.empty()) {
				if (benches.size() == 1) {
					j["benchKeyword"] = benches.front();
				} else {
					j["benchKeyword"] = benches;
				}
			}

			nlohmann::ordered_json whenJson = SerializeMatch(group.when);
			if (!whenJson.is_null()) {
				j["when"] = std::move(whenJson);
			}

			nlohmann::ordered_json rules = nlohmann::ordered_json::array();
			for (const auto& rule : group.rules) {
				rules.push_back(SerializeRule(rule));
			}
			j["rules"] = std::move(rules);

			arr.push_back(std::move(j));
		}

		return arr;
	}
}

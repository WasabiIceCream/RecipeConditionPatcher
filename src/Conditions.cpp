#include "Conditions.h"

#include "ActorValueTable.h"
#include "ConditionFunctionTable.h"
#include "EditorIDCache.h"
#include "Identifiers.h"

namespace RPP
{
	namespace
	{
		bool TryParseNumber(const std::string& a_text, float& a_out)
		{
			if (a_text.empty()) {
				return false;
			}

			// Fast reject before touching std::stof. Params are usually
			// EditorIDs ("EbonySmithing"), and std::stof signals "not a
			// number" by THROWING, which is expensive (stack unwinding,
			// RTTI) and was happening on essentially every param lookup,
			// tens of thousands of times per patch pass. A real numeric
			// literal can only begin with a digit, sign, or decimal point
			// (stof also skips leading whitespace, so let that through and
			// have stof do the real work). The try/catch below still
			// covers the genuine edge cases this check can't: a lone "+",
			// or a value too large to represent.
			const unsigned char first = static_cast<unsigned char>(a_text.front());
			const bool couldBeNumber =
				(first >= '0' && first <= '9') ||
				first == '+' || first == '-' || first == '.' ||
				std::isspace(first) != 0;
			if (!couldBeNumber) {
				return false;
			}

			try {
				std::size_t consumed = 0;
				a_out = std::stof(a_text, &consumed);
				return consumed == a_text.size();
			} catch (const std::exception&) {
				return false;
			}
		}

		// Same as TryParseNumber, but also accepts "true"/"false"
		// (case-insensitive) as 1/0, for hand-edited JSON on the "value"
		// field specifically. Not used for param1/param2, which have no
		// reason to accept true/false as a valid value.
		bool TryParseNumberOrBool(const std::string& a_text, float& a_out)
		{
			if (TryParseNumber(a_text, a_out)) {
				return true;
			}

			std::string lower = a_text;
			std::transform(lower.begin(), lower.end(), lower.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			if (lower == "true") {
				a_out = 1.0f;
				return true;
			}
			if (lower == "false") {
				a_out = 0.0f;
				return true;
			}
			return false;
		}

		// Resolves "function" to a numeric FunctionID: try it as a raw
		// number first (unambiguous), then look it up by name in the
		// mechanically-extracted table. Cached: AddConditionIfMissing is
		// called once per recipe that shares a mapped material, so the
		// same function name (e.g. "HasPerk") can be looked up thousands
		// of times in a single pass; a linear scan over 736 entries
		// every single time is wasted work once the answer's already
		// known.
		bool ResolveFunctionID(const std::string& a_function, int& a_out)
		{
			static std::unordered_map<std::string, int> cache;

			if (const auto it = cache.find(a_function); it != cache.end()) {
				a_out = it->second;
				return true;
			}

			float asNumber = 0.0f;
			if (TryParseNumber(a_function, asNumber)) {
				a_out = static_cast<int>(asNumber);
				cache[a_function] = a_out;
				return true;
			}

			for (std::size_t i = 0; i < ConditionFunctions::kTableSize; ++i) {
				if (a_function == ConditionFunctions::kTable[i].name) {
					a_out = ConditionFunctions::kTable[i].id;
					cache[a_function] = a_out;
					return true;
				}
			}
			return false;
		}

		// A param slot is either empty (unused, resolves to nullptr,
		// always "success"), a plain number (stored as a raw 32-bit
		// integer in the pointer slot), or an identifier (resolved via
		// the same TESForm lookup used for materials/perks/recipes
		// everywhere else in this plugin).
		bool ResolveParam(const std::string& a_param, void*& a_out)
		{
			if (a_param.empty()) {
				a_out = nullptr;
				return true;
			}

			float asNumber = 0.0f;
			if (TryParseNumber(a_param, asNumber)) {
				a_out = reinterpret_cast<void*>(static_cast<std::intptr_t>(static_cast<std::int32_t>(asNumber)));
				return true;
			}

			auto* form = ResolveIdentifier<RE::TESForm>(a_param);
			if (!form) {
				return false;
			}
			a_out = static_cast<void*>(form);
			return true;
		}

		// GetActorValue's param1 specifically: a raw number (0-163,
		// confirmed against a live game install and the Creation Kit
		// wiki's own Actor Value ID list) or an Actor Value name from
		// ActorValueTable.h (e.g. "Smithing"). Deliberately does NOT fall
		// through to generic TESForm resolution the way ResolveParam does.
		// Actor Values aren't forms, so attempting that risks a
		// misleading match against an unrelated form that happens to
		// share the same EditorID text.
		bool ResolveActorValueParam(const std::string& a_param, void*& a_out)
		{
			if (a_param.empty()) {
				a_out = nullptr;
				return true;
			}

			float asNumber = 0.0f;
			if (TryParseNumber(a_param, asNumber)) {
				a_out = reinterpret_cast<void*>(static_cast<std::intptr_t>(static_cast<std::int32_t>(asNumber)));
				return true;
			}

			int avID = 0;
			if (ActorValues::TryResolve(a_param, avID)) {
				a_out = reinterpret_cast<void*>(static_cast<std::intptr_t>(avID));
				return true;
			}

			return false;
		}

		bool ResolveOpCode(const std::string& a_op, RE::CONDITION_ITEM_DATA::OpCode& a_out)
		{
			using OpCode = RE::CONDITION_ITEM_DATA::OpCode;
			if (a_op == "==") { a_out = OpCode::kEqualTo; return true; }
			if (a_op == "!=") { a_out = OpCode::kNotEqualTo; return true; }
			if (a_op == ">") { a_out = OpCode::kGreaterThan; return true; }
			if (a_op == ">=") { a_out = OpCode::kGreaterThanOrEqualTo; return true; }
			if (a_op == "<") { a_out = OpCode::kLessThan; return true; }
			if (a_op == "<=") { a_out = OpCode::kLessThanOrEqualTo; return true; }
			return false;
		}

		// RunOn:: only defines 0-8 (kSubject..kCommandTarget). Unlike
		// "operator", which is a validated string, "runOn" is stored as a
		// raw int straight from JSON; a typo (e.g. "runOn": 99) would
		// otherwise silently become an out-of-range CONDITIONITEMOBJECT
		// value with no validation catching it.
		bool ValidateRunOn(int a_runOn)
		{
			return a_runOn >= RunOn::kSubject && a_runOn <= RunOn::kCommandTarget;
		}

		// Resolves the shared function+param1+param2 part of a spec, up
		// front, so AddConditionIfMissing only has to do this lookup once
		// per call instead of once for the dedup check and again for the
		// add.
		bool ResolveFunctionAndParams(
			const ConditionSpec& a_spec,
			RE::FUNCTION_DATA::FunctionID& a_outFunc,
			void*& a_outP1,
			void*& a_outP2,
			std::string& a_failureReason)
		{
			int functionID = 0;
			if (!ResolveFunctionID(a_spec.function, functionID)) {
				a_failureReason = "could not resolve function '" + a_spec.function + "'";
				return false;
			}
			a_outFunc = static_cast<RE::FUNCTION_DATA::FunctionID>(functionID);

			// GetActorValue's param1 needs its own resolution path (a
			// number 0-163 or an Actor Value name) rather than the
			// generic form/number handling every other function's params
			// use; compared against the resolved numeric ID rather than
			// the raw "function" string, so this correctly applies
			// whether someone wrote "GetActorValue" or "14".
			constexpr int kGetActorValueID = 14;
			const bool isActorValueFunction = (functionID == kGetActorValueID);

			const bool param1Ok = isActorValueFunction ?
			                           ResolveActorValueParam(a_spec.param1, a_outP1) :
			                           ResolveParam(a_spec.param1, a_outP1);
			// When the EditorID table is unavailable nothing can resolve by
			// name, so say that instead of leaving a bare "could not
			// resolve" that reads like the ID itself is wrong.
			const std::string unresolvedCause = EditorIDTableMissingRecords() ?
			                                        " (EditorID table unavailable, see the warning above)" :
			                                        "";
			if (!param1Ok) {
				a_failureReason = "could not resolve param1 '" + a_spec.param1 + "'" +
				                   (isActorValueFunction ? " (expected a number 0-163 or an Actor Value name like \"Smithing\")" : unresolvedCause);
				return false;
			}
			if (!ResolveParam(a_spec.param2, a_outP2)) {
				a_failureReason = "could not resolve param2 '" + a_spec.param2 + "'" + unresolvedCause;
				return false;
			}

			return true;
		}
	}

	bool AddConditionIfMissing(RE::TESCondition& a_conditions, const ConditionSpec& a_spec, bool& a_outAlreadyPresent, std::string& a_failureReason)
	{
		a_outAlreadyPresent = false;

		RE::FUNCTION_DATA::FunctionID func{};
		void* p1 = nullptr;
		void* p2 = nullptr;
		if (!ResolveFunctionAndParams(a_spec, func, p1, p2, a_failureReason)) {
			return false;
		}

		for (auto* item = a_conditions.head; item; item = item->next) {
			const auto& data = item->data;
			if (data.functionData.function.get() == func &&
				data.functionData.params[0] == p1 &&
				data.functionData.params[1] == p2) {
				a_outAlreadyPresent = true;
				return true;
			}
		}

		RE::CONDITION_ITEM_DATA::OpCode opCode{};
		if (!ResolveOpCode(a_spec.op, opCode)) {
			a_failureReason = "unrecognized operator '" + a_spec.op + "' (expected ==, !=, >, >=, <, or <=)";
			return false;
		}

		if (!ValidateRunOn(a_spec.runOn)) {
			a_failureReason = "runOn value " + std::to_string(a_spec.runOn) + " is out of range (expected 0-8)";
			return false;
		}

		// value: a literal number, "true"/"false", or a Global variable
		// reference.
		float literalValue = 0.0f;
		RE::TESGlobal* globalValue = nullptr;
		const bool isLiteral = TryParseNumberOrBool(a_spec.value, literalValue);
		if (!isLiteral) {
			globalValue = ResolveIdentifier<RE::TESGlobal>(a_spec.value);
			if (!globalValue) {
				a_failureReason = "could not resolve value '" + a_spec.value + "' as a number, true/false, or a Global variable";
				return false;
			}
		}

		bool isOR = false;
		if (a_spec.logic == "OR" || a_spec.logic == "or") {
			isOR = true;
		} else if (a_spec.logic != "AND" && a_spec.logic != "and") {
			a_failureReason = "unrecognized logic '" + a_spec.logic + "' (expected AND or OR)";
			return false;
		}

		auto* item = new RE::TESConditionItem();

		item->data.functionData.function = func;
		item->data.functionData.params[0] = p1;
		item->data.functionData.params[1] = p2;

		if (isLiteral) {
			item->data.comparisonValue.f = literalValue;
			item->data.flags.global = false;
		} else {
			item->data.comparisonValue.g = globalValue;
			item->data.flags.global = true;
		}

		item->data.flags.opCode = opCode;
		item->data.flags.isOR = isOR;
		item->data.object = static_cast<RE::CONDITIONITEMOBJECT>(a_spec.runOn);

		item->next = a_conditions.head;
		a_conditions.head = item;

		return true;
	}
}

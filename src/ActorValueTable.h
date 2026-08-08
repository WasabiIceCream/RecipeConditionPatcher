#pragma once

namespace RPP::ActorValues
{
	// Actor Value names and their numeric IDs (0-163), extracted directly
	// from the Creation Kit wiki's ActorValueInfo_Script page
	// (https://ck.uesp.net/wiki/ActorValueInfo_Script#Actor_Value_IDs) and
	// confirmed against a live game install. Used so GetActorValue's
	// param1 can be written/typed as a readable name (e.g. "Smithing")
	// instead of a magic number. See Conditions.cpp's
	// ResolveActorValueParam for where this table gets consulted.
	//
	// ID 162 has no confirmed name (the wiki itself lists it as unknown),
	// so it's omitted here. It can still be reached by typing the raw
	// number "162" directly, same as any function/value not in a curated
	// table elsewhere in this project.
	struct Entry
	{
		const char* name;
		int id;
	};

	inline constexpr Entry kTable[] = {
		{ "Aggression", 0 },
		{ "Confidence", 1 },
		{ "Energy", 2 },
		{ "Morality", 3 },
		{ "Mood", 4 },
		{ "Assistance", 5 },
		{ "OneHanded", 6 },
		{ "TwoHanded", 7 },
		{ "Marksman", 8 },
		{ "Block", 9 },
		{ "Smithing", 10 },
		{ "HeavyArmor", 11 },
		{ "LightArmor", 12 },
		{ "Pickpocket", 13 },
		{ "Lockpicking", 14 },
		{ "Sneak", 15 },
		{ "Alchemy", 16 },
		{ "Speechcraft", 17 },
		{ "Alteration", 18 },
		{ "Conjuration", 19 },
		{ "Destruction", 20 },
		{ "Illusion", 21 },
		{ "Restoration", 22 },
		{ "Enchanting", 23 },
		{ "Health", 24 },
		{ "Magicka", 25 },
		{ "Stamina", 26 },
		{ "HealRate", 27 },
		{ "MagickaRate", 28 },
		{ "StaminaRate", 29 },
		{ "SpeedMult", 30 },
		{ "InventoryWeight", 31 },
		{ "CarryWeight", 32 },
		{ "CritChance", 33 },
		{ "MeleeDamage", 34 },
		{ "UnarmedDamage", 35 },
		{ "Mass", 36 },
		{ "VoicePoints", 37 },
		{ "VoiceRate", 38 },
		{ "DamageResist", 39 },
		{ "PoisonResist", 40 },
		{ "FireResist", 41 },
		{ "ElectricResist", 42 },
		{ "FrostResist", 43 },
		{ "MagicResist", 44 },
		{ "NormalWeaponsResist", 45 },
		{ "PerceptionCondition", 46 },
		{ "EnduranceCondition", 47 },
		{ "LeftAttackCondition", 48 },
		{ "RightAttackCondition", 49 },
		{ "LeftMobilityCondition", 50 },
		{ "RightMobilityCondition", 51 },
		{ "BrainCondition", 52 },
		{ "Paralysis", 53 },
		{ "Invisibility", 54 },
		{ "NightEye", 55 },
		{ "DetectLifeRange", 56 },
		{ "WaterBreathing", 57 },
		{ "WaterWalking", 58 },
		{ "IgnoreCrippleLimbs", 59 },
		{ "Fame", 60 },
		{ "Infamy", 61 },
		{ "JumpingBonus", 62 },
		{ "WardPower", 63 },
		{ "EquippedItemCharge", 64 },
		{ "ArmorPerks", 65 },
		{ "ShieldPerks", 66 },
		{ "WardDeflection", 67 },
		{ "Variable01", 68 },
		{ "Variable02", 69 },
		{ "Variable03", 70 },
		{ "Variable04", 71 },
		{ "Variable05", 72 },
		{ "Variable06", 73 },
		{ "Variable07", 74 },
		{ "Variable08", 75 },
		{ "Variable09", 76 },
		{ "Variable10", 77 },
		{ "BowSpeedBonus", 78 },
		{ "FavorActive", 79 },
		{ "FavorsPerDay", 80 },
		{ "FavorsPerDayTimer", 81 },
		{ "EquippedStaffCharge", 82 },
		{ "AbsorbChance", 83 },
		{ "Blindness", 84 },
		{ "WeaponSpeedMult", 85 },
		{ "ShoutRecoveryMult", 86 },
		{ "BowStaggerBonus", 87 },
		{ "Telekinesis", 88 },
		{ "FavorPointsBonus", 89 },
		{ "LastBribedIntimidated", 90 },
		{ "LastFlattered", 91 },
		{ "Muffled", 92 },
		{ "BypassVendorStolenCheck", 93 },
		{ "BypassVendorKeywordCheck", 94 },
		{ "WaitingForPlayer", 95 },
		{ "OneHandedMod", 96 },
		{ "TwoHandedMod", 97 },
		{ "MarksmanMod", 98 },
		{ "BlockMod", 99 },
		{ "SmithingMod", 100 },
		{ "HeavyArmorMod", 101 },
		{ "LightArmorMod", 102 },
		{ "PickPocketMod", 103 },
		{ "LockPickingMod", 104 },
		{ "SneakMod", 105 },
		{ "AlchemyMod", 106 },
		{ "SpeechcraftMod", 107 },
		{ "AlterationMod", 108 },
		{ "ConjurationMod", 109 },
		{ "DestructionMod", 110 },
		{ "IllusionMod", 111 },
		{ "RestorationMod", 112 },
		{ "EnchantingMod", 113 },
		{ "OneHandedSkillAdvance", 114 },
		{ "TwoHandedSkillAdvance", 115 },
		{ "MarksmanSkillAdvance", 116 },
		{ "BlockSkillAdvance", 117 },
		{ "SmithingSkillAdvance", 118 },
		{ "HeavyArmorSkillAdvance", 119 },
		{ "LightArmorSkillAdvance", 120 },
		{ "PickPocketSkillAdvance", 121 },
		{ "LockPickingSkillAdvance", 122 },
		{ "SneakSkillAdvance", 123 },
		{ "AlchemySkillAdvance", 124 },
		{ "SpeechcraftSkillAdvance", 125 },
		{ "AlterationSkillAdvance", 126 },
		{ "ConjurationSkillAdvance", 127 },
		{ "DestructionSkillAdvance", 128 },
		{ "IllusionSkillAdvance", 129 },
		{ "RestorationSkillAdvance", 130 },
		{ "EnchantingSkillAdvance", 131 },
		{ "LeftWeaponSpeedMult", 132 },
		{ "DragonSouls", 133 },
		{ "CombatHealthRegenMult", 134 },
		{ "OneHandedPowerMod", 135 },
		{ "TwoHandedPowerMod", 136 },
		{ "MarksmanPowerMod", 137 },
		{ "BlockPowerMod", 138 },
		{ "SmithingPowerMod", 139 },
		{ "HeavyArmorPowerMod", 140 },
		{ "LightArmorPowerMod", 141 },
		{ "PickPocketPowerMod", 142 },
		{ "LockPickingPowerMod", 143 },
		{ "SneakPowerMod", 144 },
		{ "AlchemyPowerMod", 145 },
		{ "SpeechcraftPowerMod", 146 },
		{ "AlterationPowerMod", 147 },
		{ "ConjurationPowerMod", 148 },
		{ "DestructionPowerMod", 149 },
		{ "IllusionPowerMod", 150 },
		{ "RestorationPowerMod", 151 },
		{ "EnchantingPowerMod", 152 },
		{ "DragonRend", 153 },
		{ "AttackDamageMult", 154 },
		{ "CombatHealthRegenMultMod", 155 },
		{ "CombatHealthRegenMultPowerMod", 156 },
		{ "StaminaRateMult", 157 },
		{ "HealRatePowerMod", 158 },
		{ "MagickaRateMod", 159 },
		{ "GrabActorOffset", 160 },
		{ "Grabbed", 161 },
		// 162 intentionally omitted, unnamed on the source wiki page.
		{ "ReflectDamage", 163 },
	};

	inline constexpr std::size_t kTableSize = sizeof(kTable) / sizeof(kTable[0]);

	// Resolves a_name (case-sensitive, matching the wiki's own casing) to
	// its Actor Value ID. Returns false if not found.
	constexpr bool TryResolve(std::string_view a_name, int& a_out)
	{
		for (std::size_t i = 0; i < kTableSize; ++i) {
			if (a_name == kTable[i].name) {
				a_out = kTable[i].id;
				return true;
			}
		}
		return false;
	}
}

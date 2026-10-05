/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2SkillImporter.h"
#include "Importers/MT2MotionScriptParser.h"

#include "Animation/AnimSequence.h"
#include "Animation/MT2AnimationMotionData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Particles/ParticleSystem.h"
#include "Skills/MT2SkillDefinition.h"
#include "Skills/MT2SkillSet.h"
#include "Skills/MT2SkillTypes.h"
#include "UObject/Package.h"

namespace
{
	// SKILL_POWER_BY_LEVEL from the server database dump (db.sql, identical for all types):
	// index = skill level, value = the percentage the UI shows and the k formula consumes.
	constexpr int32 SkillPowerByLevel[41] = {
		0, 5, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40,
		50, 52, 54, 56, 58, 60, 63, 66, 69, 72, 82, 85, 88, 91, 94, 98, 102, 106, 110, 115, 125};

	// playersettingmodule.py SKILL_INDEX_DICT (non-YMIR branch) - the 8 sets' active skills plus
	// the shared support list. Horse skills (137-140) ride along like the old slot layout.
	struct FSkillSetLayout
	{
		EMT2CharacterRace Race;
		int32 Group;
		const TCHAR* GroupName;
		std::initializer_list<int32> ActiveVnums;
	};

	const FSkillSetLayout SkillSetLayouts[] = {
		{EMT2CharacterRace::Warrior, 1, TEXT("Body"), {1, 2, 3, 4, 5, 137, 138, 139}},
		{EMT2CharacterRace::Warrior, 2, TEXT("Mental"), {16, 17, 18, 19, 20, 137, 138, 139}},
		{EMT2CharacterRace::Assassin, 1, TEXT("Blade Fight"), {31, 32, 33, 34, 35, 137, 138, 139, 140}},
		{EMT2CharacterRace::Assassin, 2, TEXT("Archery"), {46, 47, 48, 49, 50, 137, 138, 139, 140}},
		{EMT2CharacterRace::Sura, 1, TEXT("Weaponry"), {61, 62, 63, 64, 65, 66, 137, 138, 139}},
		{EMT2CharacterRace::Sura, 2, TEXT("Black Magic"), {76, 77, 78, 79, 80, 81, 137, 138, 139}},
		{EMT2CharacterRace::Shaman, 1, TEXT("Dragon"), {91, 92, 93, 94, 95, 96, 137, 138, 139}},
		{EMT2CharacterRace::Shaman, 2, TEXT("Healing"), {106, 107, 108, 109, 110, 111, 137, 138, 139}},
	};

	const int32 SupportSkillVnums[] = {122, 123, 121, 124, 125, 129, 130, 131};

	// playersettingmodule.py EFFECT_AFFECT registrations, joined with PythonPlayer.cpp's
	// affect-to-skill table. Aura (vnum 4) is handled by the old client's dedicated
	// __Warrior_SetGeomgyeongAffect branch and selects its loop from the weapon mode.
	struct FSkillPersistentVisualRule
	{
		int32 SkillVnum;
		const TCHAR* LegacyEffectPath;
		const TCHAR* AttachSocket;
		EMT2SkillVisualWeaponMode WeaponMode = EMT2SkillVisualWeaponMode::Any;
		bool bOnlyWhileMoving = false;
		bool bAttachToWeaponMesh = false;
	};

	const FSkillPersistentVisualRule SkillPersistentVisualRules[] = {
		{4, TEXT("d:/ymir work/pc/warrior/effect/geom_sword_loop.mse"),
			TEXT("equip_right_hand"), EMT2SkillVisualWeaponMode::OneHanded},
		{4, TEXT("d:/ymir work/pc/warrior/effect/geom_spear_loop.mse"),
			TEXT("equip_right_hand"), EMT2SkillVisualWeaponMode::TwoHanded},
		{19, TEXT("d:/ymir work/pc/warrior/effect/gyeokgongjang_loop.mse"), TEXT("")},
		{49, TEXT("d:/ymir work/pc/assassin/effect/gyeonggong_loop.mse"), TEXT(""),
			EMT2SkillVisualWeaponMode::Any, true},
		{63, TEXT("d:/ymir work/pc/sura/effect/gwigeom_loop.mse"), TEXT("Bip01_R_Finger2")},
		{64, TEXT("d:/ymir work/pc/sura/effect/fear_loop.mse"), TEXT("")},
		{65, TEXT("d:/ymir work/pc/sura/effect/jumagap_loop.mse"), TEXT("")},
		{78, TEXT("d:/ymir work/pc/sura/effect/muyeong_loop.mse"), TEXT("")},
		{79, TEXT("d:/ymir work/pc/sura/effect/heuksin_loop.mse"), TEXT("")},
		{94, TEXT("d:/ymir work/pc/shaman/effect/3hosin_loop.mse"), TEXT("")},
		{95, TEXT("d:/ymir work/pc/shaman/effect/boho_loop.mse"), TEXT("")},
		{96, TEXT("d:/ymir work/pc/shaman/effect/6gicheon_hand.mse"), TEXT("Bip01_R_Hand")},
		{110, TEXT("d:/ymir work/pc/shaman/effect/10kwaesok_loop.mse"), TEXT(""),
			EMT2SkillVisualWeaponMode::Any, true},
		{111, TEXT("d:/ymir work/pc/shaman/effect/jeungryeok_hand.mse"), TEXT("Bip01_L_Hand")},
		{66, TEXT("d:/ymir work/pc/sura/effect/pabeop_loop.mse"), TEXT("Bip01_Head")},
	};

	void ResolvePersistentVisuals(
		const FString& DestinationRoot, UMT2SkillDefinition* Definition)
	{
		Definition->PersistentVisuals.Reset();
		for (const FSkillPersistentVisualRule& Rule : SkillPersistentVisualRules)
		{
			if (Rule.SkillVnum != Definition->Vnum)
			{
				continue;
			}

			FMT2AssetRecord EffectRecord;
			EffectRecord.ContentPath = Rule.LegacyEffectPath;
			const FString ObjectPath = MT2MotionScriptParser::BuildImportedEffectPath(
				Rule.LegacyEffectPath, EffectRecord, DestinationRoot);
			if (ObjectPath.IsEmpty())
			{
				continue;
			}

			FMT2SkillPersistentVisual& Visual =
				Definition->PersistentVisuals.AddDefaulted_GetRef();
			Visual.Effect = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(ObjectPath));
			Visual.Target = Rule.bAttachToWeaponMesh
				? EMT2SkillVisualTarget::WeaponMesh : EMT2SkillVisualTarget::CharacterMesh;
			Visual.AttachSocket = FName(Rule.AttachSocket);
			Visual.WeaponMode = Rule.WeaponMode;
			Visual.bOnlyWhileMoving = Rule.bOnlyWhileMoving;
		}
	}

	// Old server ESkillFlags names as they appear in skilltable's FLAG column.
	int32 ParseSkillFlags(const FString& FlagText, TArray<FString>& OutWarnings)
	{
		static const TMap<FString, int32> FlagBits = {
			{TEXT("ATTACK"), 0}, {TEXT("USE_MELEE_DAMAGE"), 1}, {TEXT("COMPUTE_ATTGRADE"), 2},
			{TEXT("SELFONLY"), 3}, {TEXT("USE_MAGIC_DAMAGE"), 4}, {TEXT("USE_HP_AS_COST"), 5},
			{TEXT("COMPUTE_MAGIC_DAMAGE"), 6}, {TEXT("SPLASH"), 7}, {TEXT("GIVE_PENALTY"), 8},
			{TEXT("USE_ARROW_DAMAGE"), 9}, {TEXT("PENETRATE"), 10}, {TEXT("IGNORE_TARGET_RATING"), 11},
			{TEXT("ATTACK_SLOW"), 12}, {TEXT("SLOW"), 12}, {TEXT("ATTACK_STUN"), 13}, {TEXT("STUN"), 13},
			{TEXT("HP_ABSORB"), 14}, {TEXT("SP_ABSORB"), 15}, {TEXT("ATTACK_FIRE_CONT"), 16},
			{TEXT("FIRE_CONT"), 16}, {TEXT("REMOVE_BAD_AFFECT"), 17}, {TEXT("REMOVE_GOOD_AFFECT"), 18},
			{TEXT("CRUSH"), 19}, {TEXT("ATTACK_POISON"), 20}, {TEXT("POISON"), 20}, {TEXT("TOGGLE"), 21},
			{TEXT("DISABLE_BY_POINT_UP"), 22}, {TEXT("CRUSH_LONG"), 23}, {TEXT("WIND"), 24},
			{TEXT("ELEC"), 25}, {TEXT("FIRE"), 26}};

		int32 Flags = 0;
		TArray<FString> Names;
		FlagText.ParseIntoArray(Names, TEXT(","));
		for (FString Name : Names)
		{
			Name.TrimStartAndEndInline();
			if (Name.IsEmpty() || Name == TEXT("NONE"))
			{
				continue;
			}
			if (const int32* Bit = FlagBits.Find(Name))
			{
				Flags |= 1 << *Bit;
			}
			else
			{
				OutWarnings.AddUnique(FString::Printf(TEXT("Unknown skill flag '%s'."), *Name));
			}
		}
		return Flags;
	}

	// skilltable POINT_ON names -> EApplyTypes ordinals of the status-effect pipeline. Names with
	// no APPLY equivalent (HP damage, MANASHIELD, ...) keep ApplyType 0; the raw name survives in
	// PointOnName for the skill executor.
	int32 MapPointOnToApplyType(const FString& PointOnName)
	{
		static const TMap<FString, int32> ApplyTypes = {
			{TEXT("NONE"), 0}, {TEXT("HP"), 0}, {TEXT("MAX_HP"), 1}, {TEXT("MAX_SP"), 2},
			{TEXT("ATT_SPEED"), 7}, {TEXT("MOV_SPEED"), 8}, {TEXT("CASTING_SPEED"), 9},
			{TEXT("CRITICAL"), 15}, {TEXT("DODGE"), 28}, {TEXT("REFLECT_MELEE"), 39},
			{TEXT("ATT_GRADE"), 53}, {TEXT("DEF_GRADE"), 54}};
		const int32* Found = ApplyTypes.Find(PointOnName);
		return Found ? *Found : 0;
	}

	// Minimal evaluator for the level-only skill polys ("40+100*k", "30+40*k", "floor(1+k*6)").
	// Anything referencing runtime stats (atk, str, ...) fails and stays formula-only.
	class FSkillPolyEvaluator
	{
	public:
		explicit FSkillPolyEvaluator(double InK) : K(InK) {}

		TOptional<double> Evaluate(const FString& Expression)
		{
			Text = Expression;
			Position = 0;
			bFailed = false;
			const double Value = ParseSum();
			SkipWhitespace();
			if (bFailed || Position != Text.Len())
			{
				return {};
			}
			return Value;
		}

	private:
		void SkipWhitespace()
		{
			while (Position < Text.Len() && FChar::IsWhitespace(Text[Position]))
			{
				++Position;
			}
		}

		bool Consume(TCHAR Character)
		{
			SkipWhitespace();
			if (Position < Text.Len() && Text[Position] == Character)
			{
				++Position;
				return true;
			}
			return false;
		}

		double ParseSum()
		{
			double Value = ParseProduct();
			while (!bFailed)
			{
				if (Consume(TEXT('+')))
				{
					Value += ParseProduct();
				}
				else if (Consume(TEXT('-')))
				{
					Value -= ParseProduct();
				}
				else
				{
					break;
				}
			}
			return Value;
		}

		double ParseProduct()
		{
			double Value = ParseUnary();
			while (!bFailed)
			{
				if (Consume(TEXT('*')))
				{
					Value *= ParseUnary();
				}
				else if (Consume(TEXT('/')))
				{
					const double Divisor = ParseUnary();
					if (FMath::IsNearlyZero(Divisor))
					{
						bFailed = true;
						return 0.0;
					}
					Value /= Divisor;
				}
				else
				{
					break;
				}
			}
			return Value;
		}

		double ParseUnary()
		{
			if (Consume(TEXT('-')))
			{
				return -ParseUnary();
			}
			return ParseAtom();
		}

		double ParseAtom()
		{
			SkipWhitespace();
			if (Consume(TEXT('(')))
			{
				const double Value = ParseSum();
				if (!Consume(TEXT(')')))
				{
					bFailed = true;
				}
				return Value;
			}
			if (Position < Text.Len() && (FChar::IsDigit(Text[Position]) || Text[Position] == TEXT('.')))
			{
				int32 Start = Position;
				while (Position < Text.Len() && (FChar::IsDigit(Text[Position]) || Text[Position] == TEXT('.')))
				{
					++Position;
				}
				return FCString::Atod(*Text.Mid(Start, Position - Start));
			}
			if (Position < Text.Len() && FChar::IsAlpha(Text[Position]))
			{
				int32 Start = Position;
				while (Position < Text.Len() && (FChar::IsAlnum(Text[Position]) || Text[Position] == TEXT('_')))
				{
					++Position;
				}
				const FString Identifier = Text.Mid(Start, Position - Start).ToLower();
				if (Identifier == TEXT("k"))
				{
					return K;
				}
				if (Identifier == TEXT("floor") && Consume(TEXT('(')))
				{
					const double Value = ParseSum();
					if (!Consume(TEXT(')')))
					{
						bFailed = true;
					}
					return FMath::FloorToDouble(Value);
				}
				// number(a,b): random range at runtime; bake the midpoint.
				if (Identifier == TEXT("number") && Consume(TEXT('(')))
				{
					const double A = ParseSum();
					if (!Consume(TEXT(',')))
					{
						bFailed = true;
						return 0.0;
					}
					const double B = ParseSum();
					if (!Consume(TEXT(')')))
					{
						bFailed = true;
					}
					return (A + B) * 0.5;
				}
				bFailed = true;
				return 0.0;
			}
			bFailed = true;
			return 0.0;
		}

		FString Text;
		int32 Position = 0;
		double K = 0.0;
		bool bFailed = false;
	};

	// Bakes a k-only poly into a level-indexed curve; leaves the curve empty when the formula
	// needs runtime stats. PowerScale is the proto bMaxLevel (the k scale).
	void BakeCurve(
		const FString& Formula, int32 PowerScale, int32 MaxLevel, FRuntimeFloatCurve& OutCurve)
	{
		FRichCurve* Curve = OutCurve.GetRichCurve();
		Curve->Reset();
		if (Formula.TrimStartAndEnd().IsEmpty())
		{
			return;
		}
		for (int32 Level = 1; Level <= MaxLevel; ++Level)
		{
			const double PowerPercent = SkillPowerByLevel[FMath::Clamp(Level, 0, 40)];
			FSkillPolyEvaluator Evaluator(PowerPercent * PowerScale / 100.0);
			const TOptional<double> Value = Evaluator.Evaluate(Formula);
			if (!Value.IsSet())
			{
				Curve->Reset();
				return;
			}
			Curve->AddKey(static_cast<float>(Level), static_cast<float>(Value.GetValue()));
		}
	}

	FString ReadSkillAnsiLineFile(const FString& Path)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			return FString();
		}
		Bytes.Add(0);
		return FString(ANSI_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Bytes.GetData())));
	}

	bool FindSkillSourceFile(const FString& SourceRoot, const TCHAR* BaseName, FString& OutPath)
	{
		// locale/en first: English names/descriptions AND the formulas matching this client
		// version. The 936* files are the Chinese-locale copies (last resort).
		const FString Candidates[] = {
			SourceRoot / TEXT("locale/en") / BaseName,
			SourceRoot / BaseName,
			SourceRoot / FString::Printf(TEXT("936%s"), BaseName)};
		for (const FString& Candidate : Candidates)
		{
			if (FPaths::FileExists(Candidate))
			{
				OutPath = Candidate;
				return true;
			}
		}
		return false;
	}

	struct FSkillTableRow
	{
		int32 Vnum = 0;
		int32 Type = 0;
		int32 LevelStep = 1;
		int32 MaxLevelScale = 1;
		int32 LevelLimit = 0;
		FString PointOn[3];
		FString PointPoly[3];
		FString SPCostPoly;
		FString DurationPoly[2];
		FString CooldownPoly;
		FString FlagText;
		int32 AffectFlag[2] = {0, 0};
		int32 PrereqVnum = 0;
		int32 PrereqLevel = 0;
		FString AttackType;
		int32 MaxHit = 0;
		float TargetRange = 0.0f;
		float SplashRange = 0.0f;
	};

	struct FSkillDescRow
	{
		FString Job;
		FString GradeNames[3];
		FString Description;
		FString AttributeNames;
		FString WeaponNames;
		FString IconName;
		int32 MotionIndex = 0;
		int32 MotionGradeCount = 1;
	};

	// Column order = the 40250 client's ESkillTableTokenType / ESkillDescTokenType enums.
	bool ParseSkillTable(const FString& Path, TMap<int32, FSkillTableRow>& OutRows, FString& OutError)
	{
		const FString Content = ReadSkillAnsiLineFile(Path);
		if (Content.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Could not read %s"), *Path);
			return false;
		}
		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TArray<FString> Columns;
			Line.ParseIntoArray(Columns, TEXT("\t"), /*bCullEmpty=*/false);
			if (Columns.Num() < 27 || Columns[0].TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			FSkillTableRow Row;
			Row.Vnum = FCString::Atoi(*Columns[0]);
			Row.Type = FCString::Atoi(*Columns[2]);
			Row.LevelStep = FCString::Atoi(*Columns[3]);
			Row.MaxLevelScale = FMath::Max(1, FCString::Atoi(*Columns[4]));
			Row.LevelLimit = FCString::Atoi(*Columns[5]);
			Row.PointOn[0] = Columns[6].TrimStartAndEnd();
			Row.PointPoly[0] = Columns[7].TrimStartAndEnd();
			Row.SPCostPoly = Columns[8].TrimStartAndEnd();
			Row.DurationPoly[0] = Columns[9].TrimStartAndEnd();
			Row.CooldownPoly = Columns[11].TrimStartAndEnd();
			Row.FlagText = Columns[14].TrimStartAndEnd();
			Row.AffectFlag[0] = FCString::Atoi(*Columns[15]);
			Row.PointOn[1] = Columns[16].TrimStartAndEnd();
			Row.PointPoly[1] = Columns[17].TrimStartAndEnd();
			Row.DurationPoly[1] = Columns[18].TrimStartAndEnd();
			Row.AffectFlag[1] = FCString::Atoi(*Columns[19]);
			Row.PrereqVnum = FCString::Atoi(*Columns[20]);
			Row.PrereqLevel = FCString::Atoi(*Columns[21]);
			Row.AttackType = Columns[22].TrimStartAndEnd();
			Row.MaxHit = FCString::Atoi(*Columns[23]);
			Row.TargetRange = FCString::Atof(*Columns[25]);
			Row.SplashRange = FCString::Atof(*Columns[26]);
			if (Row.Vnum > 0)
			{
				OutRows.Add(Row.Vnum, Row);
			}
		}
		return !OutRows.IsEmpty();
	}

	bool ParseSkillDesc(const FString& Path, TMap<int32, FSkillDescRow>& OutRows, FString& OutError)
	{
		const FString Content = ReadSkillAnsiLineFile(Path);
		if (Content.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Could not read %s"), *Path);
			return false;
		}
		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TArray<FString> Columns;
			Line.ParseIntoArray(Columns, TEXT("\t"), /*bCullEmpty=*/false);
			if (Columns.Num() < 14 || Columns[0].TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			const int32 Vnum = FCString::Atoi(*Columns[0]);
			if (Vnum <= 0)
			{
				continue;
			}
			FSkillDescRow Row;
			Row.Job = Columns[1].TrimStartAndEnd();
			Row.GradeNames[0] = Columns[2].TrimStartAndEnd();
			Row.GradeNames[1] = Columns[3].TrimStartAndEnd();
			Row.GradeNames[2] = Columns[4].TrimStartAndEnd();
			Row.Description = Columns[5].TrimStartAndEnd();
			Row.AttributeNames = Columns[10].TrimStartAndEnd();
			Row.WeaponNames = Columns[11].TrimStartAndEnd();
			Row.IconName = Columns[12].TrimStartAndEnd();
			Row.MotionIndex = FCString::Atoi(*Columns[13]);
			Row.MotionGradeCount = Columns.IsValidIndex(14)
				? FMath::Max(1, FCString::Atoi(*Columns[14])) : 1;
			OutRows.Add(Vnum, Row);
		}
		return !OutRows.IsEmpty();
	}

	// Parses an old .sub atlas fragment ("image X.dds" + left/top/right/bottom).
	bool ParseSubImage(const FString& SubPath, FString& OutImageName, FMT2AtlasRect& OutRect)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *SubPath))
		{
			return false;
		}
		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines);
		float Left = 0.0f, Top = 0.0f, Right = 0.0f, Bottom = 0.0f;
		for (const FString& Line : Lines)
		{
			FString Key, Value;
			if (!Line.TrimStartAndEnd().Split(TEXT(" "), &Key, &Value))
			{
				continue;
			}
			Value = Value.TrimStartAndEnd().TrimQuotes();
			if (Key == TEXT("image")) OutImageName = Value;
			else if (Key == TEXT("left")) Left = FCString::Atof(*Value);
			else if (Key == TEXT("top")) Top = FCString::Atof(*Value);
			else if (Key == TEXT("right")) Right = FCString::Atof(*Value);
			else if (Key == TEXT("bottom")) Bottom = FCString::Atof(*Value);
		}
		OutRect = FMT2AtlasRect(Left, Top, Right - Left, Bottom - Top);
		return !OutImageName.IsEmpty() && OutRect.IsValid();
	}

	// PythonSkill's m_PathNameMap: skilldesc JOB -> icon folder under ymir work/ui/skill/.
	FString JobToIconFolder(const FString& Job)
	{
		if (Job == TEXT("WARRIOR")) return TEXT("warrior");
		if (Job == TEXT("ASSASSIN")) return TEXT("assassin");
		if (Job == TEXT("SURA")) return TEXT("sura");
		if (Job == TEXT("SHAMAN")) return TEXT("shaman");
		if (Job == TEXT("PASSIVE")) return TEXT("passive");
		if (Job == TEXT("SUPPORT")) return TEXT("common/support");
		if (Job == TEXT("HORSE")) return TEXT("common/horse");
		if (Job == TEXT("GUILD")) return TEXT("common/guild");
		return FString();
	}

	// Active skills use per-grade icons ({name}_01.._03.sub, PythonSkill __RegisterGradeIconImage);
	// everything else one icon for all grades (__RegisterNormalIconImage).
	void ResolveSkillIcons(
		const FString& SourceRoot, const FString& DestinationRoot, const FSkillDescRow& Desc,
		UMT2SkillDefinition* Definition, TArray<FString>& OutWarnings)
	{
		Definition->IconAtlas = nullptr;
		Definition->IconRegionsByGrade.Reset();
		const FString Folder = JobToIconFolder(Desc.Job);
		if (Folder.IsEmpty() || Desc.IconName.IsEmpty())
		{
			return;
		}

		// Actives (and Riding, PythonSkill's special case) use per-grade _01.._03 icons; the rest a
		// single icon. Fall back to the other naming when the preferred one is absent.
		const FString IconRoot = SourceRoot / TEXT("ymir work/ui/skill") / Folder;
		const bool bPreferGradeIcons = Definition->SkillType == EMT2SkillType::Active ||
			Desc.IconName == TEXT("riding");
		TArray<FString> SubPaths;
		if (bPreferGradeIcons)
		{
			for (int32 Grade = 1; Grade <= 3; ++Grade)
			{
				SubPaths.Add(IconRoot / FString::Printf(TEXT("%s_%02d.sub"), *Desc.IconName, Grade));
			}
		}
		if (!bPreferGradeIcons || !FPaths::FileExists(SubPaths[0]))
		{
			SubPaths.Reset();
			SubPaths.Add(IconRoot / (Desc.IconName + TEXT(".sub")));
			if (!FPaths::FileExists(SubPaths[0]))
			{
				SubPaths.Reset();
				for (int32 Grade = 1; Grade <= 3; ++Grade)
				{
					SubPaths.Add(IconRoot / FString::Printf(TEXT("%s_%02d.sub"), *Desc.IconName, Grade));
				}
			}
		}

		for (const FString& SubPath : SubPaths)
		{
			FString ImageName;
			FMT2AtlasRect Rect;
			if (!FPaths::FileExists(SubPath) || !ParseSubImage(SubPath, ImageName, Rect))
			{
				continue;
			}
			if (Definition->IconAtlas.IsNull())
			{
				const FString AtlasBase = FPaths::GetBaseFilename(ImageName).ToLower();
				const FString AtlasPath = FString::Printf(
					TEXT("%s/ymir_work/ui/T_%s.T_%s"), *DestinationRoot, *AtlasBase, *AtlasBase);
				Definition->IconAtlas = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(AtlasPath));
			}
			Definition->IconRegionsByGrade.Add(Rect);
		}
		if (Definition->IconRegionsByGrade.IsEmpty())
		{
			OutWarnings.Add(FString::Printf(
				TEXT("Skill %d ('%s'): no icon .sub found under %s."),
				Definition->Vnum, *Desc.IconName, *IconRoot));
		}
	}

	FString JobToAnimationFolder(const FString& Job)
	{
		if (Job == TEXT("WARRIOR")) return TEXT("warrior");
		if (Job == TEXT("ASSASSIN")) return TEXT("assassin");
		if (Job == TEXT("SURA")) return TEXT("sura");
		if (Job == TEXT("SHAMAN")) return TEXT("shaman");
		return FString();
	}

	bool ReadMsaFloat(const FString& Script, const TCHAR* Key, float& OutValue)
	{
		TArray<FString> Lines;
		Script.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TArray<FString> Tokens;
			Line.TrimStartAndEnd().ParseIntoArrayWS(Tokens);
			if (Tokens.Num() >= 2 && Tokens[0] == Key)
			{
				OutValue = FCString::Atof(*Tokens[1]);
				return true;
			}
		}
		return false;
	}

	bool ReadMsaVector(const FString& Script, const TCHAR* Key, FVector& OutValue)
	{
		TArray<FString> Lines;
		Script.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TArray<FString> Tokens;
			Line.TrimStartAndEnd().ParseIntoArrayWS(Tokens);
			if (Tokens.Num() >= 4 && Tokens[0] == Key)
			{
				OutValue = FVector(
					FCString::Atof(*Tokens[1]), FCString::Atof(*Tokens[2]), FCString::Atof(*Tokens[3]));
				return true;
			}
		}
		return false;
	}

	// Each MotionEventData block with "MotionEventType 4" (SPECIAL_ATTACKING) is one skill hit; its
	// StartingTime is when that hit lands. Collect them in order = the skill's damage ticks (a skill
	// like Samyeon has three). See Docs/OldGameResearch/SkillSystem.md ("Skill damage timing").
	void ReadMsaAttackHitTimes(const FString& Script, TArray<float>& OutTimes)
	{
		OutTimes.Reset();
		TArray<FString> Lines;
		Script.ParseIntoArrayLines(Lines);
		bool bInAttackEvent = false;
		for (const FString& Line : Lines)
		{
			TArray<FString> Tokens;
			Line.TrimStartAndEnd().ParseIntoArrayWS(Tokens);
			if (Tokens.Num() < 2)
			{
				continue;
			}
			if (Tokens[0] == TEXT("MotionEventType"))
			{
				// 4 = SPECIAL_ATTACKING. Any other event type ends interest until the next one.
				bInAttackEvent = FCString::Atoi(*Tokens[1]) == 4;
			}
			else if (bInAttackEvent && Tokens[0] == TEXT("StartingTime"))
			{
				OutTimes.Add(FCString::Atof(*Tokens[1]));
				bInAttackEvent = false;
			}
		}
		OutTimes.Sort();
	}

	// The bulk asset importer that brought the skill motions in did not attach the .msa metadata,
	// so cast animations had no Accumulation and skills never moved the pawn. Attach/refresh it
	// here from the source .msa, mirroring MT2AnimationImporter::ApplyMotionMetadata.
	void ApplySkillMotionData(
		const FString& SourceRoot, const FString& DestinationRoot,
		const FString& MsaPath, const FSoftObjectPath& AnimObjectPath,
		TArray<UPackage*>& PackagesToSave)
	{
		FString Script;
		if (!FPaths::FileExists(MsaPath) || !FFileHelper::LoadFileToString(Script, *MsaPath))
		{
			return;
		}
		UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimObjectPath.TryLoad());
		if (!AnimSequence)
		{
			return;
		}
		UMT2AnimationMotionData* MotionData = AnimSequence->GetAssetUserData<UMT2AnimationMotionData>();
		if (!MotionData)
		{
			MotionData = NewObject<UMT2AnimationMotionData>(AnimSequence);
			AnimSequence->AddAssetUserData(MotionData);
		}
		AnimSequence->Modify();
		ReadMsaFloat(Script, TEXT("MotionDuration"), MotionData->MotionDuration);
		MotionData->bHasAccumulation =
			ReadMsaVector(Script, TEXT("Accumulation"), MotionData->Accumulation) &&
			!MotionData->Accumulation.IsNearlyZero();
		// .msa AttackingData: knockback strength + hit kind (1 = blow, 2 = normal).
		ReadMsaFloat(Script, TEXT("ExternalForce"), MotionData->ExternalForce);
		float HittingTypeValue = 0.0f;
		if (ReadMsaFloat(Script, TEXT("HittingType"), HittingTypeValue))
		{
			MotionData->HittingType = FMath::RoundToInt(HittingTypeValue);
		}
		// When each hit lands, so the skill deals damage at the animation's hit frame(s) rather than
		// instantly on cast.
		ReadMsaAttackHitTimes(Script, MotionData->AttackHitTimes);
		FMT2AssetRecord MotionRecord;
		MotionRecord.AbsolutePath = MsaPath;
		MotionRecord.ContentPath = MsaPath;
		FPaths::MakePathRelativeTo(MotionRecord.ContentPath, *(SourceRoot / TEXT("")));
		MotionRecord.ContentPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		MT2MotionScriptParser::PopulateEffectEvents(
			Script, MotionRecord, DestinationRoot, MotionData);
		// Skill displacement is applied from .msa accumulation by gameplay code. Preserve the authored
		// root transform as pose data, matching locomotion and attack playback.
		AnimSequence->bEnableRootMotion = false;
		AnimSequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
		AnimSequence->bForceRootLock = false;
		AnimSequence->MarkPackageDirty();
		PackagesToSave.AddUnique(AnimSequence->GetPackage());
	}

	// Which legacy folder holds a job's given sex. The old race enum defaults Warrior/Sura to male
	// but Assassin/Shaman to FEMALE (RACE_ASSASSIN_W = 1, RACE_SHAMAN_W = 3), so for those two jobs
	// "pc" is the female rig and "pc2" the male one - verified against the .msm model files.
	// Mirrors UsesPrimaryLegacyFolder in MT2CharacterAppearanceSettings.
	const TCHAR* GetLegacyPcFolder(const FString& Job, bool bFemale)
	{
		const bool bMalePrimary = Job == TEXT("WARRIOR") || Job == TEXT("SURA");
		const bool bPrimary = bMalePrimary ? !bFemale : bFemale;
		return bPrimary ? TEXT("pc") : TEXT("pc2");
	}

	// Imported skill motions live at /Game/ymir_work/pc[2]/{job}/skill/A_{icon}[_2.._4];
	// suffix = mastery grade motion (skilldesc grade count).
	void ResolveCastAnimations(
		const FString& SourceRoot, const FString& DestinationRoot, const FSkillDescRow& Desc,
		UMT2SkillDefinition* Definition, int32& InOutAnimationsResolved, TArray<UPackage*>& PackagesToSave)
	{
		Definition->CastAnimationsByGrade.Reset();
		const FString JobFolder = JobToAnimationFolder(Desc.Job);
		if (JobFolder.IsEmpty() || Desc.IconName.IsEmpty())
		{
			return;
		}

		const int32 GradeCount = FMath::Clamp(Desc.MotionGradeCount, 1, 4);
		for (int32 Grade = 0; Grade < GradeCount; ++Grade)
		{
			const FString Suffix = Grade == 0 ? FString() : FString::Printf(TEXT("_%d"), Grade + 1);
			FMT2SkillGradeAnimation GradeAnimation;
			for (int32 SexIndex = 0; SexIndex < 2; ++SexIndex)
			{
				const bool bFemale = SexIndex != 0;
				const TCHAR* PcFolder = GetLegacyPcFolder(Desc.Job, bFemale);
				const FString PackagePath = FString::Printf(TEXT("%s/ymir_work/%s/%s/skill/A_%s%s"),
					*DestinationRoot, PcFolder, *JobFolder, *Desc.IconName, *Suffix);
				if (FPackageName::DoesPackageExist(PackagePath))
				{
					const FSoftObjectPath AnimPath(FString::Printf(
						TEXT("%s.A_%s%s"), *PackagePath, *Desc.IconName, *Suffix));
					if (SexIndex == 0)
					{
						GradeAnimation.MaleAnimation = TSoftObjectPtr<UAnimSequence>(AnimPath);
					}
					else
					{
						GradeAnimation.FemaleAnimation = TSoftObjectPtr<UAnimSequence>(AnimPath);
					}
					++InOutAnimationsResolved;

					const FString MsaPath = FString::Printf(TEXT("%s/ymir work/%s/%s/skill/%s%s.msa"),
						*SourceRoot, PcFolder, *JobFolder, *Desc.IconName, *Suffix);
					ApplySkillMotionData(
						SourceRoot, DestinationRoot, MsaPath, AnimPath, PackagesToSave);
				}
			}
			Definition->CastAnimationsByGrade.Add(GradeAnimation);
		}
	}

	template <typename AssetType>
	AssetType* FindOrCreateAsset(const FString& PackageName, const FString& AssetName, bool& bOutCreated)
	{
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		if (AssetType* Existing = LoadObject<AssetType>(nullptr, *ObjectPath))
		{
			bOutCreated = false;
			return Existing;
		}
		UPackage* Package = CreatePackage(*PackageName);
		AssetType* Asset = NewObject<AssetType>(Package, *AssetName, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Asset);
		bOutCreated = true;
		return Asset;
	}
}

FString FMT2SkillImportResult::BuildSummary() const
{
	return FString::Printf(
		TEXT("Skills: %d created, %d refreshed | Sets: %d created, %d refreshed | Cast anims resolved: %d | Warnings: %d | Errors: %d"),
		DefinitionsCreated, DefinitionsRefreshed, SkillSetsCreated, SkillSetsRefreshed,
		AnimationsResolved, Warnings.Num(), Errors.Num());
}

bool FMT2SkillImporter::Import(
	const FString& SourceRoot, const FString& DestinationRoot, FMT2SkillImportResult& OutResult,
	bool bLegacyMetadataOnly)
{
	FString TablePath, DescPath, Error;
	if (!FindSkillSourceFile(SourceRoot, TEXT("skilltable.txt"), TablePath))
	{
		OutResult.Errors.Add(FString::Printf(TEXT("skilltable.txt not found under %s"), *SourceRoot));
		return false;
	}
	if (!FindSkillSourceFile(SourceRoot, TEXT("skilldesc.txt"), DescPath))
	{
		OutResult.Errors.Add(FString::Printf(TEXT("skilldesc.txt not found under %s"), *SourceRoot));
		return false;
	}

	TMap<int32, FSkillTableRow> TableRows;
	TMap<int32, FSkillDescRow> DescRows;
	if (!ParseSkillTable(TablePath, TableRows, Error))
	{
		OutResult.Errors.Add(Error);
		return false;
	}
	if (!ParseSkillDesc(DescPath, DescRows, Error))
	{
		OutResult.Errors.Add(Error);
		return false;
	}

	TArray<UPackage*> PackagesToSave;
	TMap<int32, UMT2SkillDefinition*> DefinitionsByVnum;

	for (const TPair<int32, FSkillTableRow>& Pair : TableRows)
	{
		const FSkillTableRow& Row = Pair.Value;
		const FSkillDescRow* Desc = DescRows.Find(Row.Vnum);

		const FString AssetName = FString::Printf(TEXT("DA_Skill_%05d"), Row.Vnum);
		const FString PackageName = DestinationRoot / TEXT("Skills/Definitions") / AssetName;
		if (bLegacyMetadataOnly)
		{
			UMT2SkillDefinition* Existing = LoadObject<UMT2SkillDefinition>(nullptr, *(PackageName + TEXT(".") + AssetName));
			if (!Existing)
			{
				OutResult.Errors.Add(FString::Printf(TEXT("Metadata refresh requires existing %s"), *PackageName));
				continue;
			}
			Existing->Modify();
			Existing->LegacySkillType = Row.Type;
			Existing->MarkPackageDirty();
			PackagesToSave.AddUnique(Existing->GetPackage());
			++OutResult.DefinitionsRefreshed;
			continue;
		}
		bool bCreated = false;
		UMT2SkillDefinition* Definition =
			FindOrCreateAsset<UMT2SkillDefinition>(PackageName, AssetName, bCreated);
		if (!Definition)
		{
			OutResult.Errors.Add(FString::Printf(TEXT("Could not create %s"), *PackageName));
			continue;
		}
		Definition->Modify();

		Definition->Vnum = Row.Vnum;
		Definition->LegacySkillType = Row.Type;
		Definition->PowerScale = Row.MaxLevelScale;
		Definition->LevelLimit = Row.LevelLimit;
		Definition->PrerequisiteSkillVnum = Row.PrereqVnum;
		Definition->PrerequisiteSkillLevel = Row.PrereqLevel;
		Definition->Flags = ParseSkillFlags(Row.FlagText, OutResult.Warnings);
		Definition->TargetRange = Row.TargetRange;
		Definition->SplashRange = Row.SplashRange;
		Definition->MaxHits = Row.MaxHit;
		Definition->SPCostFormula = Row.SPCostPoly;
		Definition->CooldownFormula = Row.CooldownPoly;

		// Skill categories: table type 0 = job-independent support, 1-4 = job actives, 5 = horse;
		// guild skills sit in the 151+ vnum band.
		if (Row.Vnum >= 151 && Row.Vnum <= 162)
		{
			Definition->SkillType = EMT2SkillType::Guild;
		}
		else if (Row.Type == 5)
		{
			Definition->SkillType = EMT2SkillType::Horse;
		}
		else if (Row.Type == 0)
		{
			Definition->SkillType = EMT2SkillType::Support;
		}
		else
		{
			Definition->SkillType = EMT2SkillType::Active;
		}

		// Job skills cap at the global 40; support skills at the proto scale (Combo 2, Riding 20).
		Definition->MaxLevel = Definition->SkillType == EMT2SkillType::Active
			? MT2SkillLimits::MaxSkillLevel
			: FMath::Clamp(Row.MaxLevelScale, 1, MT2SkillLimits::MaxSkillLevel);

		if (Row.AttackType == TEXT("MELEE")) Definition->AttackType = EMT2SkillAttackType::Melee;
		else if (Row.AttackType == TEXT("RANGE")) Definition->AttackType = EMT2SkillAttackType::Range;
		else if (Row.AttackType == TEXT("MAGIC")) Definition->AttackType = EMT2SkillAttackType::Magic;
		else Definition->AttackType = EMT2SkillAttackType::Normal;

		FRichCurve* PowerCurve = Definition->SkillPowerPercentByLevel.GetRichCurve();
		PowerCurve->Reset();
		for (int32 Level = 1; Level <= MT2SkillLimits::MaxSkillLevel; ++Level)
		{
			PowerCurve->AddKey(static_cast<float>(Level), static_cast<float>(SkillPowerByLevel[Level]));
		}
		BakeCurve(Row.SPCostPoly, Row.MaxLevelScale, MT2SkillLimits::MaxSkillLevel,
			Definition->SPCostByLevel);
		BakeCurve(Row.CooldownPoly, Row.MaxLevelScale, MT2SkillLimits::MaxSkillLevel,
			Definition->CooldownByLevel);

		Definition->Applies.Reset();
		for (int32 ApplyIndex = 0; ApplyIndex < 3; ++ApplyIndex)
		{
			const FString& PointOn = ApplyIndex < 2 ? Row.PointOn[ApplyIndex] : FString();
			if (PointOn.IsEmpty())
			{
				continue;
			}
			// Some state-only skills (Stealth is the important one) intentionally use POINT_NONE:
			// their duration + AFF_* flag is the effect. Discard only a truly empty NONE slot.
			if (PointOn == TEXT("NONE") &&
				Row.DurationPoly[ApplyIndex].IsEmpty() &&
				Row.AffectFlag[ApplyIndex] == 0)
			{
				continue;
			}
			FMT2SkillApplyDefinition& Apply = Definition->Applies.AddDefaulted_GetRef();
			Apply.PointOnName = PointOn;
			Apply.ApplyType = MapPointOnToApplyType(PointOn);
			Apply.ValueFormula = Row.PointPoly[ApplyIndex];
			Apply.DurationFormula = Row.DurationPoly[ApplyIndex];
			Apply.AffectFlags = Row.AffectFlag[ApplyIndex];
			BakeCurve(Apply.ValueFormula, Row.MaxLevelScale, MT2SkillLimits::MaxSkillLevel,
				Apply.ValueByLevel);
			BakeCurve(Apply.DurationFormula, Row.MaxLevelScale, MT2SkillLimits::MaxSkillLevel,
				Apply.DurationByLevel);
		}
		ResolvePersistentVisuals(DestinationRoot, Definition);

		if (Desc)
		{
			Definition->InternalName = Desc->IconName;
			Definition->GradeNames = {
				FText::FromString(Desc->GradeNames[0]),
				FText::FromString(Desc->GradeNames[1]),
				FText::FromString(Desc->GradeNames[2])};
			Definition->Description = FText::FromString(Desc->Description);
			Definition->ClientAttributeNames = Desc->AttributeNames;
			Definition->WeaponLimitationNames = Desc->WeaponNames;
			Definition->MotionIndex = Desc->MotionIndex;
			Definition->MotionGradeCount = Desc->MotionGradeCount;
			ResolveCastAnimations(SourceRoot, DestinationRoot, *Desc, Definition,
				OutResult.AnimationsResolved, PackagesToSave);
			ResolveSkillIcons(SourceRoot, DestinationRoot, *Desc, Definition, OutResult.Warnings);
		}
		else
		{
			OutResult.Warnings.Add(FString::Printf(
				TEXT("Skill %d has no skilldesc row (no name/animations)."), Row.Vnum));
		}

		Definition->MarkPackageDirty();
		PackagesToSave.AddUnique(Definition->GetPackage());
		DefinitionsByVnum.Add(Row.Vnum, Definition);
		++(bCreated ? OutResult.DefinitionsCreated : OutResult.DefinitionsRefreshed);
	}

	// The 8 race/group sets + the shared support skills.
	if (bLegacyMetadataOnly)
	{
		if (!PackagesToSave.IsEmpty() && !UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true))
		{
			OutResult.Errors.Add(TEXT("Failed to save legacy skill metadata."));
		}
		return OutResult.Errors.IsEmpty();
	}

	for (const FSkillSetLayout& Layout : SkillSetLayouts)
	{
		const UEnum* RaceEnum = StaticEnum<EMT2CharacterRace>();
		const FString RaceName = RaceEnum->GetNameStringByValue(static_cast<int64>(Layout.Race));
		const FString AssetName = FString::Printf(TEXT("DA_SkillSet_%s_G%d"), *RaceName, Layout.Group);
		const FString PackageName = DestinationRoot / TEXT("Skills/Sets") / AssetName;
		bool bCreated = false;
		UMT2SkillSet* SkillSet = FindOrCreateAsset<UMT2SkillSet>(PackageName, AssetName, bCreated);
		if (!SkillSet)
		{
			OutResult.Errors.Add(FString::Printf(TEXT("Could not create %s"), *PackageName));
			continue;
		}
		SkillSet->Modify();
		SkillSet->Race = Layout.Race;
		SkillSet->GroupIndex = Layout.Group;
		SkillSet->GroupDisplayName = FText::FromString(Layout.GroupName);
		SkillSet->ActiveSkills.Reset();
		SkillSet->SupportSkills.Reset();
		for (int32 Vnum : Layout.ActiveVnums)
		{
			if (UMT2SkillDefinition* const* Definition = DefinitionsByVnum.Find(Vnum))
			{
				SkillSet->ActiveSkills.Add(*Definition);
			}
			else
			{
				OutResult.Warnings.Add(FString::Printf(
					TEXT("%s: active skill %d missing from skilltable."), *AssetName, Vnum));
			}
		}
		for (int32 Vnum : SupportSkillVnums)
		{
			if (UMT2SkillDefinition* const* Definition = DefinitionsByVnum.Find(Vnum))
			{
				SkillSet->SupportSkills.Add(*Definition);
			}
		}
		SkillSet->MarkPackageDirty();
		PackagesToSave.AddUnique(SkillSet->GetPackage());
		++(bCreated ? OutResult.SkillSetsCreated : OutResult.SkillSetsRefreshed);
	}

	if (!PackagesToSave.IsEmpty())
	{
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	}
	return OutResult.Errors.IsEmpty();
}

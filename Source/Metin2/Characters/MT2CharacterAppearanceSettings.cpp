/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Characters/MT2CharacterAppearanceSettings.h"
#include "Config/MT2PathSettings.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"

namespace
{
	const TCHAR* GetRaceName(EMT2CharacterRace Race)
	{
		switch (Race)
		{
		case EMT2CharacterRace::Warrior: return TEXT("warrior");
		case EMT2CharacterRace::Assassin: return TEXT("assassin");
		case EMT2CharacterRace::Sura: return TEXT("sura");
		case EMT2CharacterRace::Shaman: return TEXT("shaman");
		default: return TEXT("warrior");
		}
	}

	bool UsesPrimaryLegacyFolder(EMT2CharacterRace Race, EMT2CharacterSex Sex)
	{
		const bool bMalePrimary = Race == EMT2CharacterRace::Warrior || Race == EMT2CharacterRace::Sura;
		return bMalePrimary ? Sex == EMT2CharacterSex::Male : Sex == EMT2CharacterSex::Female;
	}

	const TCHAR* GetStyleTextureName(EMT2CharacterRace Race, EMT2CharacterStyle Style)
	{
		if (Style == EMT2CharacterStyle::Red)
		{
			return TEXT("red");
		}

		// The original second Assassin and Shaman skins are named green in the client data.
		return Race == EMT2CharacterRace::Assassin || Race == EMT2CharacterRace::Shaman
			? TEXT("green")
			: TEXT("blue");
	}

	const TCHAR* GetSexName(EMT2CharacterSex Sex)
	{
		return Sex == EMT2CharacterSex::Male ? TEXT("Male") : TEXT("Female");
	}

	const TCHAR* GetRaceDisplayName(EMT2CharacterRace Race)
	{
		switch (Race)
		{
		case EMT2CharacterRace::Warrior: return TEXT("Warrior");
		case EMT2CharacterRace::Assassin: return TEXT("Assassin");
		case EMT2CharacterRace::Sura: return TEXT("Sura");
		case EMT2CharacterRace::Shaman: return TEXT("Shaman");
		default: return TEXT("Warrior");
		}
	}

	FString GetFaceTexturePath(EMT2CharacterRace Race, EMT2CharacterSex Sex)
	{
		const FString TextureName = FString::Printf(
			TEXT("T_%s_%s"), GetRaceName(Race),
			Sex == EMT2CharacterSex::Female ? TEXT("w") : TEXT("m"));
		return UMT2PathSettings::Format(TEXT("ymir_work_icon_face_Name"), TEXT("%s%s"), *TextureName, *TextureName);
	}
}

UMT2CharacterAppearanceSettings::UMT2CharacterAppearanceSettings()
{
	const bool bBuildDefaultAppearances = Appearances.IsEmpty();
	const bool bBuildDefaultAnimationProfiles = AnimationProfiles.IsEmpty();

	for (uint8 RaceIndex = 0; RaceIndex < 4; ++RaceIndex)
	{
		const EMT2CharacterRace Race = static_cast<EMT2CharacterRace>(RaceIndex);
		const FString RaceName = GetRaceName(Race);

		for (uint8 SexIndex = 0; SexIndex < 2; ++SexIndex)
		{
			const EMT2CharacterSex Sex = static_cast<EMT2CharacterSex>(SexIndex);
			const FString RootFolder = UsesPrimaryLegacyFolder(Race, Sex) ? TEXT("pc") : TEXT("pc2");
			if (bBuildDefaultAnimationProfiles)
			{
				FMT2CharacterAnimationProfile& AnimationProfile = AnimationProfiles.AddDefaulted_GetRef();
				AnimationProfile.Race = Race;
				AnimationProfile.Sex = Sex;
				const FString AnimBlueprintName = FString::Printf(
					TEXT("ABP_%s_%s"), GetRaceDisplayName(Race), GetSexName(Sex));
				AnimationProfile.AnimInstanceClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(UMT2PathSettings::Format(TEXT("Characters_Animations_Name_Prefix"), TEXT("%s%s"), *AnimBlueprintName, *AnimBlueprintName)));
			}

			for (uint8 StyleIndex = 0; bBuildDefaultAppearances && StyleIndex < 2; ++StyleIndex)
			{
				const EMT2CharacterStyle Style = static_cast<EMT2CharacterStyle>(StyleIndex);
				FMT2CharacterAppearanceAsset& Asset = Appearances.AddDefaulted_GetRef();
				Asset.Appearance.Race = Race;
				Asset.Appearance.Sex = Sex;
				Asset.Appearance.Style = Style;

				const FString MeshName = FString::Printf(TEXT("SK_%s_novice"), *RaceName);
				const FString MeshPath = UMT2PathSettings::Format(TEXT("ymir_work_Name_Name_Name_novice_SkeletalMeshes_Name"), TEXT("%s%s%s%s%s"),
					*RootFolder, *RaceName, *RaceName, *MeshName, *MeshName);
				Asset.Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(MeshPath));

				const FString TextureName = FString::Printf(
					TEXT("T_%s_novice_%s"), *RaceName, GetStyleTextureName(Race, Style));
				const FString TexturePath = UMT2PathSettings::Format(TEXT("ymir_work_Name_Name_Name"), TEXT("%s%s%s%s"),
					*RootFolder, *RaceName, *TextureName, *TextureName);
				Asset.DiffuseTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TexturePath));
				Asset.FaceTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(
					GetFaceTexturePath(Race, Sex)));

				const FString HairMeshName = TEXT("SK_hair_1_1");
				const FString HairMeshPath = UMT2PathSettings::Format(TEXT("ymir_work_Name_Name_hair_hair_1_1_SkeletalMeshes_Name"), TEXT("%s%s%s%s"),
					*RootFolder, *RaceName, *HairMeshName, *HairMeshName);
				Asset.DefaultHairMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(HairMeshPath));

				const FString HairTextureName = FString::Printf(TEXT("T_%s_hair_01"), *RaceName);
				const FString HairTexturePath = UMT2PathSettings::Format(TEXT("ymir_work_Name_Name_Name"), TEXT("%s%s%s%s"),
					*RootFolder, *RaceName, *HairTextureName, *HairTextureName);
				Asset.DefaultHairTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(HairTexturePath));
			}
		}
	}
}

const FMT2CharacterAnimationProfile* UMT2CharacterAppearanceSettings::FindAnimationProfile(
	EMT2CharacterRace Race, EMT2CharacterSex Sex) const
{
	return AnimationProfiles.FindByPredicate(
		[Race, Sex](const FMT2CharacterAnimationProfile& Profile)
		{
			return Profile.Race == Race && Profile.Sex == Sex;
		});
}

const FMT2CharacterAppearanceAsset* UMT2CharacterAppearanceSettings::FindAppearance(
	const FMT2CharacterAppearance& Appearance) const
{
	return Appearances.FindByPredicate(
		[&Appearance](const FMT2CharacterAppearanceAsset& Asset) { return Asset.Appearance == Appearance; });
}

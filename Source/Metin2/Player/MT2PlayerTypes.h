/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2PlayerTypes.generated.h"

namespace MT2PlayerLimits
{
	inline constexpr int32 MaxCharacterNameLength = 24;
}

namespace MT2KarmaLimits
{
	inline constexpr int32 Minimum = -30000;
	inline constexpr int32 Maximum = 30000;
	inline constexpr int32 LegacyRawMinimum = -200000;
	inline constexpr int32 LegacyRawMaximum = 200000;
}

USTRUCT()
struct METIN2_API FMT2AlignmentState
{
	GENERATED_BODY()
	// Public displayed alignment in tenths; zero while masked. Real points replicate owner-only.
	UPROPERTY() int32 RawPoints = 0;
	UPROPERTY() bool bHidden = false;
};

UENUM(BlueprintType)
enum class EMT2CharacterRace : uint8
{
	Warrior,
	Assassin,
	Sura,
	Shaman
};

UENUM(BlueprintType)
enum class EMT2CharacterSex : uint8
{
	Male,
	Female
};

UENUM(BlueprintType)
enum class EMT2CharacterStyle : uint8
{
	Red,
	Blue
};

UENUM(BlueprintType)
enum class EMT2Empire : uint8
{
	None = 0,
	Shinsoo = 1 UMETA(DisplayName = "Shinsoo (Red)"),
	Chunjo = 2 UMETA(DisplayName = "Chunjo (Yellow)"),
	Jinno = 3 UMETA(DisplayName = "Jinno (Blue)")
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2CharacterAppearance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	EMT2CharacterRace Race = EMT2CharacterRace::Warrior;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	EMT2CharacterSex Sex = EMT2CharacterSex::Male;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	EMT2CharacterStyle Style = EMT2CharacterStyle::Red;

	bool operator==(const FMT2CharacterAppearance& Other) const
	{
		return Race == Other.Race && Sex == Other.Sex && Style == Other.Style;
	}

	bool operator!=(const FMT2CharacterAppearance& Other) const { return !(*this == Other); }
};

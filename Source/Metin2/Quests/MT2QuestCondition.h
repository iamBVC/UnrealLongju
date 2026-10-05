/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Quests/MT2QuestTypes.h"
#include "UObject/Object.h"
#include "MT2QuestCondition.generated.h"

// One test a quest node can gate on. Subclasses are EditInlineNew + DefaultToInstanced, so a designer
// picks a condition from a dropdown inside the quest Blueprint's details panel and fills its fields
// there - no graph wiring needed. Blueprintable, so new conditions can also be made without C++.
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, Blueprintable, CollapseCategories)
class METIN2_API UMT2QuestCondition : public UObject
{
	GENERATED_BODY()

public:
	// Native evaluation; the Blueprint hook below covers designer-made conditions.
	virtual bool Evaluate(const FMT2QuestContext& Context) const;

	// Override in a condition Blueprint for custom logic.
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Quest|Condition")
	bool Check(const FMT2QuestContext& Context) const;
	virtual bool Check_Implementation(const FMT2QuestContext& Context) const { return true; }

	// Inverts the result, so "player does NOT have item" needs no separate class.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	bool bInvert = false;
};

// How to compare a runtime value against Value.
UENUM(BlueprintType)
enum class EMT2QuestCompare : uint8
{
	Equal,
	NotEqual,
	Greater,
	GreaterOrEqual,
	Less,
	LessOrEqual
};

namespace MT2QuestCompare
{
	METIN2_API bool Apply(EMT2QuestCompare Comparison, int64 Left, int64 Right);
}

// pc.get_level() comparisons.
UCLASS(DisplayName = "Player Level")
class METIN2_API UMT2QuestCondition_Level : public UMT2QuestCondition
{
	GENERATED_BODY()

public:
	virtual bool Evaluate(const FMT2QuestContext& Context) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EMT2QuestCompare Comparison = EMT2QuestCompare::GreaterOrEqual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	int32 Level = 1;
};

// pc.count_item(vnum) comparisons.
UCLASS(DisplayName = "Has Item")
class METIN2_API UMT2QuestCondition_HasItem : public UMT2QuestCondition
{
	GENERATED_BODY()

public:
	virtual bool Evaluate(const FMT2QuestContext& Context) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	int32 ItemVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (ClampMin = "1"))
	int32 Count = 1;
};

// pc.getqf(name) comparisons - the single most used quest primitive in the old scripts.
UCLASS(DisplayName = "Quest Flag")
class METIN2_API UMT2QuestCondition_Flag : public UMT2QuestCondition
{
	GENERATED_BODY()

public:
	virtual bool Evaluate(const FMT2QuestContext& Context) const override;

	// Unqualified names resolve inside the running quest (old pc.getqf); use "quest.flag" to read
	// another quest's flag.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	FName FlagName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EMT2QuestCompare Comparison = EMT2QuestCompare::Equal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	int32 Value = 0;
};

// pc.get_empire().
UCLASS(DisplayName = "Player Empire")
class METIN2_API UMT2QuestCondition_Empire : public UMT2QuestCondition
{
	GENERATED_BODY()

public:
	virtual bool Evaluate(const FMT2QuestContext& Context) const override;

	// 1 Shinsoo, 2 Chunjo, 3 Jinno (old empire ordinals).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (ClampMin = "0", ClampMax = "3"))
	int32 Empire = 1;
};

// A raw script expression ("pc.get_level() >= 30 and pc.getqf(\"step\") == 2"), evaluated at runtime.
// This is what lets the importer convert the conditions that don't reduce to one of the simple tests
// above, instead of leaving them as TODOs.
UCLASS(DisplayName = "Expression")
class METIN2_API UMT2QuestCondition_Expression : public UMT2QuestCondition
{
	GENERATED_BODY()

public:
	virtual bool Evaluate(const FMT2QuestContext& Context) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	FString Expression;
};

// pc.get_money() comparisons.
UCLASS(DisplayName = "Player Gold")
class METIN2_API UMT2QuestCondition_Gold : public UMT2QuestCondition
{
	GENERATED_BODY()

public:
	virtual bool Evaluate(const FMT2QuestContext& Context) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EMT2QuestCompare Comparison = EMT2QuestCompare::GreaterOrEqual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	int64 Gold = 0;
};

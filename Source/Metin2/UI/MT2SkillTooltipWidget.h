/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "MT2SkillTooltipWidget.generated.h"

class UBorder;
class UMT2SkillDefinition;
class UVerticalBox;

// Hover popup for skill icons (skill page + quickbar), laid out like the old uitooltip.py
// SetSkillNew: grade name, required level, description, then the current level's affects (attack
// power / heal, buffs, duration, cooldown, SP or HP cost) followed by a greyed preview of the next
// level, and the skill requirements. Built natively; rebuild with SetSkill whenever level changes.
UCLASS()
class METIN2_API UMT2SkillTooltipWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSkill(const UMT2SkillDefinition* Definition, int32 SkillLevel);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void AddLine(const FString& Text, const FLinearColor& Color, int32 FontSize = 9);
	void AppendLevelLimit(const UMT2SkillDefinition* Definition);
	// One level's worth of lines (affects, duration, cooldown, cost), lit for the current level and
	// greyed for the next-level preview.
	void AppendLevelBlock(const UMT2SkillDefinition* Definition, int32 Level, const FLinearColor& Color);
	void AppendRequirements(const UMT2SkillDefinition* Definition);

	UPROPERTY(Transient) TObjectPtr<UBorder> RootBorder;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> LinesBox;
};

/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Quests/MT2QuestCondition.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Items/MT2InventoryComponent.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2QuestExpression.h"
#include "Quests/MT2QuestManagerComponent.h"

bool MT2QuestCompare::Apply(EMT2QuestCompare Comparison, int64 Left, int64 Right)
{
	switch (Comparison)
	{
	case EMT2QuestCompare::Equal:          return Left == Right;
	case EMT2QuestCompare::NotEqual:       return Left != Right;
	case EMT2QuestCompare::Greater:        return Left > Right;
	case EMT2QuestCompare::GreaterOrEqual: return Left >= Right;
	case EMT2QuestCompare::Less:           return Left < Right;
	case EMT2QuestCompare::LessOrEqual:    return Left <= Right;
	default:                               return false;
	}
}

bool UMT2QuestCondition::Evaluate(const FMT2QuestContext& Context) const
{
	// Base class defers to the Blueprint hook, so a condition Blueprint only implements Check.
	return Check(Context) != bInvert;
}

bool UMT2QuestCondition_Level::Evaluate(const FMT2QuestContext& Context) const
{
	const int32 PlayerLevel = Context.PlayerState ? Context.PlayerState->GetCharacterLevel() : 0;
	return MT2QuestCompare::Apply(Comparison, PlayerLevel, Level) != bInvert;
}

bool UMT2QuestCondition_HasItem::Evaluate(const FMT2QuestContext& Context) const
{
	const UMT2InventoryComponent* Inventory =
		Context.Player ? Context.Player->GetInventoryComponent() : nullptr;
	const int32 Owned = Inventory ? Inventory->CountItemByVnum(ItemVnum) : 0;
	return (Owned >= Count) != bInvert;
}

bool UMT2QuestCondition_Flag::Evaluate(const FMT2QuestContext& Context) const
{
	const int32 Current = Context.Manager ? Context.Manager->GetQuestFlag(FlagName, Context.Quest) : 0;
	return MT2QuestCompare::Apply(Comparison, Current, Value) != bInvert;
}

bool UMT2QuestCondition_Empire::Evaluate(const FMT2QuestContext& Context) const
{
	const int32 Current = Context.PlayerState
		? static_cast<int32>(Context.PlayerState->GetEmpire()) : 0;
	return (Current == Empire) != bInvert;
}

bool UMT2QuestCondition_Expression::Evaluate(const FMT2QuestContext& Context) const
{
	return FMT2QuestExpression::EvaluateBool(Expression, Context) != bInvert;
}

bool UMT2QuestCondition_Gold::Evaluate(const FMT2QuestContext& Context) const
{
	const int64 Current = Context.PlayerState ? Context.PlayerState->GetYang() : 0;
	return MT2QuestCompare::Apply(Comparison, Current, Gold) != bInvert;
}

/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Quests/MT2Quest.h"

FName UMT2Quest::GetQuestId() const
{
	if (!QuestName.IsNone())
	{
		return QuestName;
	}
	// Fall back to the asset name so a designer can drop in a quest Blueprint without filling anything
	// in. Blueprint classes carry the "_C" suffix; strip it so the id matches the asset.
	FString ClassName = GetClass()->GetName();
	ClassName.RemoveFromEnd(TEXT("_C"));
	ClassName.RemoveFromStart(TEXT("BP_"));
	return FName(*ClassName);
}

const FMT2QuestState* UMT2Quest::FindState(FName StateName) const
{
	return States.FindByPredicate(
		[StateName](const FMT2QuestState& State) { return State.StateName == StateName; });
}

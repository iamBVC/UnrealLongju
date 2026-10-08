#include "Fishing/MT2FishingSettings.h"

bool UMT2FishingSettings::Validate(FString& Error) const
{
	if (MinimumWaitSeconds < 1 || MaximumWaitSeconds < MinimumWaitSeconds || MaximumWaitSeconds > 3600 ||
		!FMath::IsFinite(CastDistance) || CastDistance < 1 || CastDistance > 10000 ||
		!FMath::IsFinite(MovementTolerance) || MovementTolerance < 0 || MovementTolerance > 100 ||
		!FMath::IsFinite(DroppedRewardOwnershipSeconds) || DroppedRewardOwnershipSeconds < 0 || DroppedRewardOwnershipSeconds > 3600 || CatchTable.IsEmpty() || TimingProfiles.Num() != 5)
	{ Error = TEXT("Invalid fishing rules/table dimensions."); return false; }
	for (const auto& Profile : TimingProfiles)
		if (Profile.Chances.Num() != 31 || Profile.Chances.ContainsByPredicate([](int32 Value) { return Value < 0 || Value > 100; }))
		{ Error = TEXT("Each timing profile needs 31 probability values in [0,100]."); return false; }
	for (const auto& Entry : CatchTable)
		if (Entry.Weights.Num() != 4 || Entry.Difficulty < 1 || Entry.TimeProfile < 0 || Entry.TimeProfile >= 5 ||
			Entry.LengthRange.Num() != 3 || Entry.LengthRange[0] < 0 || Entry.LengthRange[0] > Entry.LengthRange[1] || Entry.LengthRange[1] > Entry.LengthRange[2] ||
			Entry.Weights.ContainsByPredicate([](int32 Weight) { return Weight < 0; }))
		{ Error = TEXT("Invalid fishing catch row."); return false; }
	for (int32 Table = 0; Table < 4; ++Table)
	{
		int64 Total = 0; for (const auto& Entry : CatchTable) { Total += Entry.Weights[Table]; }
		if (Total <= 0 || Total > MAX_int32) { Error = TEXT("Fishing weight sum must be in [1,MAX_int32]."); return false; }
	}
	TSet<int32> Maps;
	for (const auto& Map : MapTables)
	{
		if (Map.MapIndex <= 0 || Map.TableIndex < 0 || Map.TableIndex >= 4 || Maps.Contains(Map.MapIndex)) { Error = TEXT("Invalid or duplicate fishing map rule."); return false; }
		Maps.Add(Map.MapIndex);
	}
	Error.Reset(); return true;
}
int32 UMT2FishingSettings::FindTable(int32 MapIndex) const
{
	for (const auto& Map : MapTables) { if (Map.MapIndex == MapIndex) { return Map.TableIndex; } }
	return INDEX_NONE;
}
int32 UMT2FishingSettings::PickCatch(int32 TableIndex, int32 Roll) const
{
	if (TableIndex < 0 || TableIndex >= 4 || Roll < 1) { return INDEX_NONE; }
	int64 Sum = 0;
	for (int32 Index = 0; Index < CatchTable.Num(); ++Index)
	{
		if (!CatchTable[Index].Weights.IsValidIndex(TableIndex)) { return INDEX_NONE; }
		Sum += CatchTable[Index].Weights[TableIndex]; if (Roll <= Sum) { return Index; }
	}
	return INDEX_NONE;
}
int32 UMT2FishingSettings::TimingChance(int32 Profile, int32 ElapsedMilliseconds) const
{
	if (ElapsedMilliseconds < 0 || ElapsedMilliseconds > 6000 || !TimingProfiles.IsValidIndex(Profile)) { return 0; }
	const int32 Step = FMath::Clamp((ElapsedMilliseconds + 99) / 200, 0, 30);
	return TimingProfiles[Profile].Chances.IsValidIndex(Step) ? TimingProfiles[Profile].Chances[Step] : 0;
}
bool UMT2FishingSettings::ResolveCatch(const FMT2FishingCatch& Catch, int32 Milliseconds, int32 Power, int32 TimingRoll, int32 DifficultyRoll) const
{
	return !Catch.ItemTemplate.IsNull() && TimingRoll >= 1 && TimingRoll <= TimingChance(Catch.TimeProfile, Milliseconds) &&
		DifficultyRoll >= 1 && DifficultyRoll <= Catch.Difficulty && DifficultyRoll <= Power;
}

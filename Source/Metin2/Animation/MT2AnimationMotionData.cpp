#include "Animation/MT2AnimationMotionData.h"
#include "String/LexFromString.h"

void UMT2AnimationMotionData::ReadAttackEvents(const FString& Script, TArray<FMT2MotionAttackEvent>& OutEvents)
{
	struct FScope
	{
		FString Name;
		FMT2MotionAttackEvent Event;
		int32 EventType = -1;
		float Start = -1.f, End = -1.f, During = 0.f;
		bool bNormal = false;
	};
	OutEvents.Reset();
	TArray<FScope> Scopes;
	Scopes.AddDefaulted();
	FString PendingName;
	TArray<FString> Lines; Script.ParseIntoArrayLines(Lines);
	for (FString Line : Lines)
	{
		Line.TrimStartAndEndInline();
		TArray<FString> Tokens; Line.ParseIntoArrayWS(Tokens);
		if (Tokens.IsEmpty()) { continue; }
		if (Tokens[0] == TEXT("Group") || Tokens[0] == TEXT("List"))
		{
			PendingName = Tokens.Num() > 1 ? Tokens[1] : FString();
			continue;
		}
		if (Tokens[0] == TEXT("{"))
		{
			FScope Scope; Scope.Name = PendingName; PendingName.Reset();
			Scope.bNormal = Scope.Name == TEXT("AttackingData") || Scope.Name.StartsWith(TEXT("HitData"));
			if (Scope.bNormal) { Scope.Event = Scopes.Last().Event; }
			Scopes.Add(MoveTemp(Scope));
			continue;
		}
		if (Tokens[0] == TEXT("}"))
		{
			if (Scopes.Num() <= 1) { continue; }
			const FScope Scope = Scopes.Pop();
			if (Scope.Event.HittingType != 0 && Scope.Start >= 0.f &&
				(Scope.EventType == 4 || (Scope.bNormal && Scope.End > Scope.Start)))
			{
				FMT2MotionAttackEvent Event = Scope.Event;
				Event.TimeSeconds = Scope.Start;
				Event.EndTimeSeconds = Scope.EventType == 4 ? Scope.Start + Scope.During : Scope.End;
				OutEvents.Add(Event);
			}
			continue;
		}
		if (Tokens.Num() < 2) { continue; }
		FScope& Scope = Scopes.Last();
		float Value = 0.f;
		if (!LexTryParseString(Value, *Tokens[1]) || !FMath::IsFinite(Value)) { continue; }
		const FString& Key = Tokens[0];
		if (Key == TEXT("HittingType")) { LexTryParseString(Scope.Event.HittingType, *Tokens[1]); }
		else if (Key == TEXT("ExternalForce")) { Scope.Event.ExternalForce = FMath::Max(Value, 0.f); }
		else if (Key == TEXT("MotionEventType")) { LexTryParseString(Scope.EventType, *Tokens[1]); }
		else if (Key == TEXT("StartingTime") || Key == TEXT("AttackingStartTime")) { Scope.Start = Value; }
		else if (Key == TEXT("AttackingEndTime")) { Scope.End = Value; }
		else if (Key == TEXT("DuringTime")) { Scope.During = FMath::Max(Value, 0.f); }
	}
	OutEvents.Sort([](const FMT2MotionAttackEvent& A, const FMT2MotionAttackEvent& B) { return A.TimeSeconds < B.TimeSeconds; });
}

void UMT2AnimationMotionData::ReadKnockbackMetadata(const FString& Script, float& OutForce, int32& OutHittingType)
{
	OutForce = 0; OutHittingType = 0;
	int32 CurrentHitType = 0;
	TArray<int32> HitTypeStack;
	TArray<FString> Lines; Script.ParseIntoArrayLines(Lines);
	for (FString Line : Lines)
	{
		Line.TrimStartAndEndInline();
		if (Line == TEXT("{")) { HitTypeStack.Add(CurrentHitType); continue; }
		if (Line == TEXT("}")) { CurrentHitType = HitTypeStack.IsEmpty() ? 0 : HitTypeStack.Pop(); continue; }
		TArray<FString> Tokens; Line.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() < 2) { continue; }
		if (Tokens[0] == TEXT("HittingType")) { LexTryParseString(CurrentHitType, *Tokens[1]); }
		else if (Tokens[0] == TEXT("ExternalForce"))
		{
			float Force;
			if (LexTryParseString(Force, *Tokens[1]) && FMath::IsFinite(Force) && Force >= 0 && CurrentHitType != 0 &&
				(Force > OutForce || OutHittingType == 0))
			{ OutForce = Force; OutHittingType = CurrentHitType; }
		}
	}
}

#include "Config/MT2PathSettings.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Paths, Log, All);

FString UMT2PathSettings::FormatValues(FName Key, const TCHAR* Signature, const TArray<FString>& Arguments)
{
	const FString Pattern = Path(Key, Signature);
	FString Result;
	int32 Argument = 0;
	for (int32 I = 0; I < Pattern.Len(); ++I)
	{
		if (Pattern[I] != TEXT('%')) { Result.AppendChar(Pattern[I]); continue; }
		if (I + 1 < Pattern.Len() && Pattern[I + 1] == TEXT('%')) { Result.AppendChar(TEXT('%')); ++I; continue; }
		const int32 Start = ++I;
		while (I < Pattern.Len() && FChar::IsDigit(Pattern[I])) { ++I; }
		if (!Arguments.IsValidIndex(Argument))
		{
			UE_LOG(LogMT2Paths, Fatal, TEXT("Not enough arguments for path '%s'."), *Key.ToString());
		}
		FString Value = Arguments[Argument++];
		if (I > Start)
		{
			const int32 Width = FCString::Atoi(*Pattern.Mid(Start, I - Start));
			if (Width > 64) { UE_LOG(LogMT2Paths, Fatal, TEXT("Excessive field width in path '%s'."), *Key.ToString()); }
			while (Value.Len() < Width) { Value = (Pattern[Start] == TEXT('0') ? TEXT("0") : TEXT(" ")) + Value; }
		}
		Result += Value;
	}
	if (Argument != Arguments.Num())
	{
		UE_LOG(LogMT2Paths, Fatal, TEXT("Too many arguments for path '%s'."), *Key.ToString());
	}
	return Result;
}

bool UMT2PathSettings::HasFormat(const FString& Value, const FString& Expected)
{
	FString Tokens;
	for (int32 I = 0; I < Value.Len(); ++I)
	{
		if (Value[I] != TEXT('%')) { continue; }
		if (I + 1 < Value.Len() && Value[I + 1] == TEXT('%')) { ++I; continue; }
		const int32 Start = I++;
		while (I < Value.Len() && FChar::IsDigit(Value[I])) { ++I; }
		if (I >= Value.Len() || (Value[I] != TEXT('s') && Value[I] != TEXT('d'))) { return false; }
		Tokens += Value.Mid(Start, I - Start + 1);
	}
	return Tokens == Expected;
}

const TCHAR* UMT2PathSettings::Path(FName Key, const TCHAR* Format)
{
	// Immutable after initialization: edits cannot invalidate pointers retained by callers.
	static const TMap<FName, FString> Catalog = []()
	{
		const UMT2PathSettings* Settings = GetDefault<UMT2PathSettings>();
		TMap<FName, FString> Values;
		for (const auto& Entry : Settings->Locations)
		{
			if (Entry.Key.IsNone() || Values.Contains(Entry.Key))
			{
				UE_LOG(LogMT2Paths, Fatal, TEXT("Empty/duplicate location key '%s' in Asset and File Paths settings."), *Entry.Key.ToString());
			}
			Values.Add(Entry.Key, Entry.Value);
		}
		for (const auto& Entry : Settings->Assets)
		{
			if (Entry.Key.IsNone() || Values.Contains(Entry.Key))
			{
				UE_LOG(LogMT2Paths, Fatal, TEXT("Duplicate path key '%s' in Asset and File Paths settings."), *Entry.Key.ToString());
			}
			Values.Add(Entry.Key, Entry.Value.ToString());
		}
		Values.Add(TEXT("ProjectDir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
		Values.Add(TEXT("ContentDir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
		Values.Add(TEXT("SavedDir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()));
		for (auto& Entry : Values)
		{
			for (int32 Depth = 0; Entry.Value.Contains(TEXT("{")); ++Depth)
			{
				const int32 Begin = Entry.Value.Find(TEXT("{"));
				const int32 End = Entry.Value.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Begin + 1);
				if (End == INDEX_NONE || Depth >= 64)
				{
					UE_LOG(LogMT2Paths, Fatal, TEXT("Invalid/cyclic path substitution in '%s'."), *Entry.Key.ToString());
				}
				const FName Reference(*Entry.Value.Mid(Begin + 1, End - Begin - 1));
				const FString* Replacement = Values.Find(Reference);
				if (!Replacement || Reference == Entry.Key)
				{
					UE_LOG(LogMT2Paths, Fatal, TEXT("Unresolved path reference '%s' in '%s'."), *Reference.ToString(), *Entry.Key.ToString());
				}
				const FString Expanded = *Replacement;
				Entry.Value = Entry.Value.Left(Begin) + Expanded + Entry.Value.Mid(End + 1);
			}
		}
		return Values;
	}();
	const FString* Value = Catalog.Find(Key);
	if (!Value || Value->IsEmpty() || !HasFormat(*Value, Format))
	{
		UE_LOG(LogMT2Paths, Fatal, TEXT("Missing/invalid required path '%s'. Check [/Script/Metin2.MT2PathSettings] in DefaultGame.ini (printf tokens must be '%s')."), *Key.ToString(), Format);
	}
	return **Value;
}

/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2EffectImporter.h"

#include "Algo/Reverse.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Importers/MT2TextureImporter.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionParticleSubUV.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Modules/ModuleManager.h"
#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionFloatConstantCurve.h"
#include "Distributions/DistributionFloatUniform.h"
#include "Distributions/DistributionVectorConstant.h"
#include "Distributions/DistributionVectorConstantCurve.h"
#include "Distributions/DistributionVectorUniform.h"
#include "Particles/Acceleration/ParticleModuleAccelerationDrag.h"
#include "Particles/Acceleration/ParticleModuleAccelerationOverLifetime.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/Location/ParticleModuleLocation.h"
#include "Particles/Location/ParticleModuleLocationPrimitiveCylinder.h"
#include "Particles/Location/ParticleModuleLocationPrimitiveSphere.h"
#include "Particles/Orientation/ParticleModuleOrientationAxisLock.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSpriteEmitter.h"
#include "Particles/Rotation/ParticleModuleRotation.h"
#include "Particles/RotationRate/ParticleModuleRotationRate.h"
#include "Particles/Size/ParticleModuleSize.h"
#include "Particles/Size/ParticleModuleSizeScale.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/SubUV/ParticleModuleSubUVMovie.h"
#include "Particles/TypeData/ParticleModuleTypeDataBase.h"
#include "Particles/Velocity/ParticleModuleVelocity.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace
{
	struct FMT2EffectPositionKey
	{
		float Time = 0.0f;
		FVector Position = FVector::ZeroVector;
		FVector ControlPoint = FVector::ZeroVector;
		bool bBezier = false;
	};

	struct FMT2LegacyParticleSettings
	{
		float StartTime = 0.0f;
		int32 MaxEmissionCount = 0;
		float CycleLength = 1.0f;
		bool bCycleLoop = false;
		int32 LoopCount = 0;
		int32 EmitterShape = 0;
		int32 EmitterAdvancedType = 0;
		bool bEmitFromEdge = false;
		FVector EmittingSize = FVector::ZeroVector;
		float EmittingRadius = 0.0f;
		FVector EmittingDirection = FVector::ZeroVector;

		int32 SrcBlendType = 5;
		int32 DestBlendType = 2;
		int32 BillboardType = 1;
		int32 RotationType = 0;
		float RotationSpeed = 0.0f;
		float RotationStartMin = 0.0f;
		float RotationStartMax = 0.0f;
		bool bAttachParticles = false;
		bool bStretch = false;
		int32 TextureAnimationType = 0;
		float TextureAnimationDelay = 0.03f;
		bool bRandomTextureStartFrame = false;

		TArray<FMT2EffectPositionKey> PositionKeys;
		TArray<TPair<float, float>> EmittingSizeKeys;
		TArray<TPair<float, float>> DirectionXKeys;
		TArray<TPair<float, float>> DirectionYKeys;
		TArray<TPair<float, float>> DirectionZKeys;
		TArray<TPair<float, float>> VelocityKeys;
		TArray<TPair<float, float>> SpawnRateKeys;
		TArray<TPair<float, float>> LifetimeKeys;
		TArray<TPair<float, float>> SizeXKeys;
		TArray<TPair<float, float>> SizeYKeys;
		TArray<TPair<float, float>> GravityKeys;
		TArray<TPair<float, float>> AirResistanceKeys;
		TArray<TPair<float, float>> ScaleXKeys;
		TArray<TPair<float, float>> ScaleYKeys;
		TArray<TPair<float, float>> ColorRKeys;
		TArray<TPair<float, float>> ColorGKeys;
		TArray<TPair<float, float>> ColorBKeys;
		TArray<TPair<float, float>> AlphaKeys;
		TArray<TPair<float, float>> RotationKeys;
		TArray<FString> TextureReferences;
	};

	struct FMT2LegacyEffectSettings
	{
		float BoundingSphereRadius = 0.0f;
		FVector BoundingSpherePosition = FVector::ZeroVector;
		TArray<FMT2LegacyParticleSettings> Particles;
		int32 MeshElementCount = 0;
		int32 LightElementCount = 0;
	};

	bool ExtractFirstFloatAfter(const FString& Text, const FString& Key, float& OutValue);
	bool ExtractTimeValuePairs(
		const FString& Text, const FString& ListName, TArray<TPair<float, float>>& OutPairs);

	FString MakeParticleAssetName(const FMT2AssetRecord& Record)
	{
		const FString BaseName = FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Record.ContentPath));
		return BaseName.StartsWith(TEXT("PS_"), ESearchCase::IgnoreCase) ? BaseName : FString(TEXT("PS_")) + BaseName;
	}

	bool IsTextureReferenceToken(const FString& Token)
	{
		const FString Extension = FPaths::GetExtension(Token).ToLower();
		return Extension == TEXT("dds") || Extension == TEXT("tga") || Extension == TEXT("png") ||
			Extension == TEXT("jpg") || Extension == TEXT("jpeg") || Extension == TEXT("bmp");
	}

	FString CleanReferenceToken(FString Token)
	{
		Token.TrimStartAndEndInline();
		Token = Token.TrimQuotes();
		Token.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Token.StartsWith(TEXT("./")))
		{
			Token.RightChopInline(2);
		}
		return Token;
	}

	FVector ConvertLegacyEffectVector(const FVector& Value)
	{
		// The source client uses -Y as actor forward. This is the same basis conversion used by
		// attack accumulation and keeps authored effect offsets aligned with imported characters.
		return FVector(-Value.Y, Value.X, Value.Z);
	}

	bool ExtractNamedGroupBlocks(const FString& Text, const FString& GroupName, TArray<FString>& OutBlocks)
	{
		OutBlocks.Reset();
		int32 SearchFrom = 0;
		while (SearchFrom < Text.Len())
		{
			const int32 GroupIndex = Text.Find(TEXT("Group"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
			if (GroupIndex == INDEX_NONE)
			{
				break;
			}

			int32 NameStart = GroupIndex + 5;
			while (NameStart < Text.Len() && FChar::IsWhitespace(Text[NameStart]))
			{
				++NameStart;
			}
			int32 NameEnd = NameStart;
			while (NameEnd < Text.Len() && !FChar::IsWhitespace(Text[NameEnd]) && Text[NameEnd] != TCHAR('{'))
			{
				++NameEnd;
			}
			if (!Text.Mid(NameStart, NameEnd - NameStart).Equals(GroupName, ESearchCase::IgnoreCase))
			{
				SearchFrom = NameEnd;
				continue;
			}

			const int32 OpenBrace = Text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameEnd);
			if (OpenBrace == INDEX_NONE)
			{
				break;
			}
			int32 Depth = 0;
			bool bInQuotes = false;
			int32 CloseBrace = INDEX_NONE;
			for (int32 Index = OpenBrace; Index < Text.Len(); ++Index)
			{
				if (Text[Index] == TCHAR('"') && (Index == 0 || Text[Index - 1] != TCHAR('\\')))
				{
					bInQuotes = !bInQuotes;
				}
				if (bInQuotes)
				{
					continue;
				}
				if (Text[Index] == TCHAR('{'))
				{
					++Depth;
				}
				else if (Text[Index] == TCHAR('}') && --Depth == 0)
				{
					CloseBrace = Index;
					break;
				}
			}
			if (CloseBrace == INDEX_NONE)
			{
				break;
			}
			OutBlocks.Add(Text.Mid(OpenBrace + 1, CloseBrace - OpenBrace - 1));
			SearchFrom = CloseBrace + 1;
		}
		return !OutBlocks.IsEmpty();
	}

	bool ExtractVectorAfter(const FString& Text, const FString& Key, FVector& OutValue)
	{
		const int32 KeyIndex = Text.Find(Key, ESearchCase::IgnoreCase);
		if (KeyIndex == INDEX_NONE)
		{
			return false;
		}
		TArray<FString> Tokens;
		Text.RightChop(KeyIndex + Key.Len()).ParseIntoArrayWS(Tokens);
		if (Tokens.Num() < 3)
		{
			return false;
		}
		OutValue = FVector(
			FCString::Atof(*Tokens[0]), FCString::Atof(*Tokens[1]), FCString::Atof(*Tokens[2]));
		return true;
	}

	bool ExtractBoolAfter(const FString& Text, const FString& Key, bool& OutValue)
	{
		float Value = 0.0f;
		if (!ExtractFirstFloatAfter(Text, Key, Value))
		{
			return false;
		}
		OutValue = !FMath::IsNearlyZero(Value);
		return true;
	}

	bool ExtractIntAfter(const FString& Text, const FString& Key, int32& OutValue)
	{
		float Value = 0.0f;
		if (!ExtractFirstFloatAfter(Text, Key, Value))
		{
			return false;
		}
		OutValue = FMath::RoundToInt(Value);
		return true;
	}

	bool ExtractListBody(const FString& Text, const FString& ListName, FString& OutBody)
	{
		const FString Header = FString::Printf(TEXT("List %s"), *ListName);
		const int32 ListIndex = Text.Find(Header, ESearchCase::IgnoreCase);
		if (ListIndex == INDEX_NONE)
		{
			return false;
		}
		const int32 OpenBrace = Text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ListIndex);
		const int32 CloseBrace = OpenBrace == INDEX_NONE
			? INDEX_NONE
			: Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenBrace + 1);
		if (OpenBrace == INDEX_NONE || CloseBrace == INDEX_NONE)
		{
			return false;
		}
		OutBody = Text.Mid(OpenBrace + 1, CloseBrace - OpenBrace - 1);
		return true;
	}

	void ExtractTextureList(const FString& Text, TArray<FString>& OutReferences)
	{
		OutReferences.Reset();
		FString Body;
		if (!ExtractListBody(Text, TEXT("TextureFiles"), Body))
		{
			return;
		}
		TArray<FString> Lines;
		Body.ParseIntoArrayLines(Lines, true);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			Line = CleanReferenceToken(Line);
			if (!Line.IsEmpty())
			{
				OutReferences.Add(Line);
			}
		}
	}

	void ExtractPositionKeys(const FString& Text, TArray<FMT2EffectPositionKey>& OutKeys)
	{
		OutKeys.Reset();
		FString Body;
		if (!ExtractListBody(Text, TEXT("TimeEventPosition"), Body))
		{
			return;
		}
		TArray<FString> Lines;
		Body.ParseIntoArrayLines(Lines, true);
		for (FString Line : Lines)
		{
			Line.ReplaceInline(TEXT("\""), TEXT(""));
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() < 5)
			{
				continue;
			}
			FMT2EffectPositionKey& Key = OutKeys.AddDefaulted_GetRef();
			Key.Time = FCString::Atof(*Tokens[0]);
			Key.bBezier = Tokens[1].Equals(TEXT("MOVING_TYPE_BEZIER_CURVE"), ESearchCase::IgnoreCase);
			Key.Position = FVector(
				FCString::Atof(*Tokens[2]), FCString::Atof(*Tokens[3]), FCString::Atof(*Tokens[4]));
			if (Key.bBezier && Tokens.Num() >= 8)
			{
				Key.ControlPoint = FVector(
					FCString::Atof(*Tokens[5]), FCString::Atof(*Tokens[6]), FCString::Atof(*Tokens[7]));
			}
		}
	}

	bool ParseLegacyEffect(const FMT2AssetRecord& Record, FMT2LegacyEffectSettings& OutSettings)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Record.AbsolutePath))
		{
			return false;
		}
		ExtractFirstFloatAfter(Text, TEXT("BoundingSphereRadius"), OutSettings.BoundingSphereRadius);
		ExtractVectorAfter(Text, TEXT("BoundingSpherePosition"), OutSettings.BoundingSpherePosition);

		TArray<FString> ParticleBlocks;
		ExtractNamedGroupBlocks(Text, TEXT("Particle"), ParticleBlocks);
		for (const FString& ParticleBlock : ParticleBlocks)
		{
			FMT2LegacyParticleSettings& Particle = OutSettings.Particles.AddDefaulted_GetRef();
			ExtractFirstFloatAfter(ParticleBlock, TEXT("StartTime"), Particle.StartTime);
			ExtractPositionKeys(ParticleBlock, Particle.PositionKeys);

			TArray<FString> EmitterGroups;
			TArray<FString> PropertyGroups;
			ExtractNamedGroupBlocks(ParticleBlock, TEXT("EmitterProperty"), EmitterGroups);
			ExtractNamedGroupBlocks(ParticleBlock, TEXT("ParticleProperty"), PropertyGroups);
			const FString& Emitter = EmitterGroups.IsEmpty() ? ParticleBlock : EmitterGroups[0];
			const FString& Property = PropertyGroups.IsEmpty() ? ParticleBlock : PropertyGroups[0];

			ExtractIntAfter(Emitter, TEXT("MaxEmissionCount"), Particle.MaxEmissionCount);
			ExtractFirstFloatAfter(Emitter, TEXT("CycleLength"), Particle.CycleLength);
			ExtractBoolAfter(Emitter, TEXT("CycleLoopEnable"), Particle.bCycleLoop);
			ExtractIntAfter(Emitter, TEXT("LoopCount"), Particle.LoopCount);
			ExtractIntAfter(Emitter, TEXT("EmitterShape"), Particle.EmitterShape);
			ExtractIntAfter(Emitter, TEXT("EmitterAdvancedType"), Particle.EmitterAdvancedType);
			ExtractBoolAfter(Emitter, TEXT("EmitterEmitFromEdgeFlag"), Particle.bEmitFromEdge);
			ExtractVectorAfter(Emitter, TEXT("EmittingSize"), Particle.EmittingSize);
			ExtractFirstFloatAfter(Emitter, TEXT("EmittingRadius"), Particle.EmittingRadius);
			ExtractVectorAfter(Emitter, TEXT("EmittingDirection"), Particle.EmittingDirection);
			ExtractTimeValuePairs(Emitter, TEXT("TimeEventEmittingSize"), Particle.EmittingSizeKeys);
			ExtractTimeValuePairs(Emitter, TEXT("TimeEventEmittingDirectionX"), Particle.DirectionXKeys);
			ExtractTimeValuePairs(Emitter, TEXT("TimeEventEmittingDirectionY"), Particle.DirectionYKeys);
			ExtractTimeValuePairs(Emitter, TEXT("TimeEventEmittingDirectionZ"), Particle.DirectionZKeys);
			ExtractTimeValuePairs(Emitter, TEXT("TimeEventEmittingVelocity"), Particle.VelocityKeys);
			ExtractTimeValuePairs(Emitter, TEXT("TimeEventEmissionCountPerSecond"), Particle.SpawnRateKeys);
			ExtractTimeValuePairs(Emitter, TEXT("TimeEventLifeTime"), Particle.LifetimeKeys);
			ExtractTimeValuePairs(Emitter, TEXT("TimeEventSizeX"), Particle.SizeXKeys);
			ExtractTimeValuePairs(Emitter, TEXT("TimeEventSizeY"), Particle.SizeYKeys);

			ExtractIntAfter(Property, TEXT("SrcBlendType"), Particle.SrcBlendType);
			ExtractIntAfter(Property, TEXT("DestBlendType"), Particle.DestBlendType);
			ExtractIntAfter(Property, TEXT("BillboardType"), Particle.BillboardType);
			ExtractIntAfter(Property, TEXT("RotationType"), Particle.RotationType);
			ExtractFirstFloatAfter(Property, TEXT("RotationSpeed"), Particle.RotationSpeed);
			ExtractFirstFloatAfter(Property, TEXT("RotationRandomStartingBegin"), Particle.RotationStartMin);
			ExtractFirstFloatAfter(Property, TEXT("RotationRandomStartingEnd"), Particle.RotationStartMax);
			ExtractBoolAfter(Property, TEXT("AttachEnable"), Particle.bAttachParticles);
			ExtractBoolAfter(Property, TEXT("StretchEnable"), Particle.bStretch);
			ExtractIntAfter(Property, TEXT("TexAniType"), Particle.TextureAnimationType);
			ExtractFirstFloatAfter(Property, TEXT("TexAniDelay"), Particle.TextureAnimationDelay);
			ExtractBoolAfter(Property, TEXT("TexAniRandomStartFrameEnable"), Particle.bRandomTextureStartFrame);
			ExtractTimeValuePairs(Property, TEXT("TimeEventGravity"), Particle.GravityKeys);
			ExtractTimeValuePairs(Property, TEXT("TimeEventAirResistance"), Particle.AirResistanceKeys);
			ExtractTimeValuePairs(Property, TEXT("TimeEventScaleX"), Particle.ScaleXKeys);
			ExtractTimeValuePairs(Property, TEXT("TimeEventScaleY"), Particle.ScaleYKeys);
			ExtractTimeValuePairs(Property, TEXT("TimeEventColorRed"), Particle.ColorRKeys);
			ExtractTimeValuePairs(Property, TEXT("TimeEventColorGreen"), Particle.ColorGKeys);
			ExtractTimeValuePairs(Property, TEXT("TimeEventColorBlue"), Particle.ColorBKeys);
			ExtractTimeValuePairs(Property, TEXT("TimeEventAlpha"), Particle.AlphaKeys);
			ExtractTimeValuePairs(Property, TEXT("TimeEventRotation"), Particle.RotationKeys);
			ExtractTextureList(Property, Particle.TextureReferences);
		}

		TArray<FString> UnsupportedGroups;
		ExtractNamedGroupBlocks(Text, TEXT("Mesh"), UnsupportedGroups);
		OutSettings.MeshElementCount = UnsupportedGroups.Num();
		ExtractNamedGroupBlocks(Text, TEXT("Light"), UnsupportedGroups);
		OutSettings.LightElementCount = UnsupportedGroups.Num();
		return !OutSettings.Particles.IsEmpty() || OutSettings.MeshElementCount > 0 || OutSettings.LightElementCount > 0;
	}


	void ExtractTextureReferences(const FMT2AssetRecord& Record, TArray<FString>& OutReferences)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Record.AbsolutePath))
		{
			return;
		}

		TArray<FString> Tokens;
		Text.ParseIntoArrayWS(Tokens);
		for (FString Token : Tokens)
		{
			Token = CleanReferenceToken(Token);
			Token.RemoveFromEnd(TEXT(","));
			Token.RemoveFromEnd(TEXT(";"));
			Token.RemoveFromEnd(TEXT("}"));
			Token.RemoveFromStart(TEXT("{"));

			if (IsTextureReferenceToken(Token))
			{
				OutReferences.AddUnique(Token);
			}
		}
	}

	bool ExtractFirstFloatAfter(const FString& Text, const FString& Key, float& OutValue)
	{
		const int32 KeyIndex = Text.Find(Key, ESearchCase::IgnoreCase);
		if (KeyIndex == INDEX_NONE)
		{
			return false;
		}

		FString Tail = Text.RightChop(KeyIndex + Key.Len());
		TArray<FString> Tokens;
		Tail.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() == 0)
		{
			return false;
		}

		OutValue = FCString::Atof(*Tokens[0]);
		return true;
	}

	bool ExtractTimeValuePairs(const FString& Text, const FString& ListName, TArray<TPair<float, float>>& OutPairs)
	{
		OutPairs.Reset();
		const FString Header = FString::Printf(TEXT("List %s"), *ListName);
		const int32 ListIndex = Text.Find(Header, ESearchCase::IgnoreCase);
		if (ListIndex == INDEX_NONE)
		{
			return false;
		}

		const int32 OpenBrace = Text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ListIndex);
		const int32 CloseBrace = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenBrace + 1);
		if (OpenBrace == INDEX_NONE || CloseBrace == INDEX_NONE || CloseBrace <= OpenBrace)
		{
			return false;
		}

		FString Body = Text.Mid(OpenBrace + 1, CloseBrace - OpenBrace - 1);
		TArray<FString> Lines;
		Body.ParseIntoArrayLines(Lines, true);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line.IsEmpty())
			{
				continue;
			}

			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() >= 2)
			{
				OutPairs.Add(TPair<float, float>(FCString::Atof(*Tokens[0]), FCString::Atof(*Tokens[1])));
			}
		}
		return OutPairs.Num() > 0;
	}

	void SetFloatConstant(FRawDistributionFloat& Distribution, UObject* Outer, float Value)
	{
		UDistributionFloatConstant* Constant = NewObject<UDistributionFloatConstant>(Outer);
		Constant->Constant = Value;
		Constant->bIsDirty = true;
		Distribution.Distribution = Constant;
	}

	void SetVectorConstant(FRawDistributionVector& Distribution, UObject* Outer, const FVector& Value)
	{
		UDistributionVectorConstant* Constant = NewObject<UDistributionVectorConstant>(Outer);
		Constant->Constant = Value;
		Constant->bIsDirty = true;
		Distribution.Distribution = Constant;
	}

	float SampleFloatKeys(const TArray<TPair<float, float>>& Keys, float Time, float Fallback)
	{
		if (Keys.IsEmpty())
		{
			return Fallback;
		}
		if (Keys.Num() == 1 || Time <= Keys[0].Key)
		{
			return Keys[0].Value;
		}
		for (int32 Index = 1; Index < Keys.Num(); ++Index)
		{
			if (Time <= Keys[Index].Key)
			{
				const float Span = Keys[Index].Key - Keys[Index - 1].Key;
				const float Alpha = Span > UE_SMALL_NUMBER ? (Time - Keys[Index - 1].Key) / Span : 1.0f;
				return FMath::Lerp(Keys[Index - 1].Value, Keys[Index].Value, Alpha);
			}
		}
		return Keys.Last().Value;
	}

	void AddKeyTimes(const TArray<TPair<float, float>>& Keys, TArray<float>& Times)
	{
		for (const TPair<float, float>& Key : Keys)
		{
			Times.AddUnique(Key.Key);
		}
	}

	void SetFloatCurve(
		FRawDistributionFloat& Distribution, UObject* Outer,
		const TArray<TPair<float, float>>& Keys, float Fallback, float ValueScale = 1.0f)
	{
		if (Keys.IsEmpty())
		{
			SetFloatConstant(Distribution, Outer, Fallback * ValueScale);
			return;
		}
		UDistributionFloatConstantCurve* Curve = NewObject<UDistributionFloatConstantCurve>(Outer);
		for (const TPair<float, float>& Key : Keys)
		{
			const int32 Index = Curve->CreateNewKey(Key.Key);
			Curve->SetKeyOut(0, Index, Key.Value * ValueScale);
		}
		Curve->bIsDirty = true;
		Distribution.Distribution = Curve;
	}

	void SetCombinedVectorCurve(
		FRawDistributionVector& Distribution, UObject* Outer,
		const TArray<TPair<float, float>>& XKeys,
		const TArray<TPair<float, float>>& YKeys,
		const TArray<TPair<float, float>>& ZKeys,
		const FVector& Fallback, const FVector& ValueScale = FVector::OneVector)
	{
		TArray<float> Times;
		AddKeyTimes(XKeys, Times);
		AddKeyTimes(YKeys, Times);
		AddKeyTimes(ZKeys, Times);
		if (Times.IsEmpty())
		{
			SetVectorConstant(Distribution, Outer, Fallback * ValueScale);
			return;
		}
		Times.Sort();
		UDistributionVectorConstantCurve* Curve = NewObject<UDistributionVectorConstantCurve>(Outer);
		for (const float Time : Times)
		{
			const FVector Value(
				SampleFloatKeys(XKeys, Time, Fallback.X),
				SampleFloatKeys(YKeys, Time, Fallback.Y),
				SampleFloatKeys(ZKeys, Time, Fallback.Z));
			const int32 Index = Curve->CreateNewKey(Time);
			Curve->SetKeyOut(0, Index, Value.X * ValueScale.X);
			Curve->SetKeyOut(1, Index, Value.Y * ValueScale.Y);
			Curve->SetKeyOut(2, Index, Value.Z * ValueScale.Z);
		}
		Curve->bIsDirty = true;
		Distribution.Distribution = Curve;
	}

	void SetVelocityCurve(FRawDistributionVector& Distribution, UObject* Outer, const FMT2LegacyParticleSettings& Settings)
	{
		TArray<float> Times;
		AddKeyTimes(Settings.DirectionXKeys, Times);
		AddKeyTimes(Settings.DirectionYKeys, Times);
		AddKeyTimes(Settings.DirectionZKeys, Times);
		AddKeyTimes(Settings.VelocityKeys, Times);
		if (Times.IsEmpty())
		{
			SetVectorConstant(Distribution, Outer, FVector::ZeroVector);
			return;
		}
		Times.Sort();
		UDistributionVectorConstantCurve* Curve = NewObject<UDistributionVectorConstantCurve>(Outer);
		for (const float Time : Times)
		{
			const float Speed = SampleFloatKeys(Settings.VelocityKeys, Time, 0.0f);
			const FVector LegacyDirection(
				SampleFloatKeys(Settings.DirectionXKeys, Time, 0.0f),
				SampleFloatKeys(Settings.DirectionYKeys, Time, 0.0f),
				SampleFloatKeys(Settings.DirectionZKeys, Time, 0.0f));
			const FVector Value = ConvertLegacyEffectVector(LegacyDirection) * Speed;
			const int32 Index = Curve->CreateNewKey(Time);
			Curve->SetKeyOut(0, Index, Value.X);
			Curve->SetKeyOut(1, Index, Value.Y);
			Curve->SetKeyOut(2, Index, Value.Z);
		}
		Curve->bIsDirty = true;
		Distribution.Distribution = Curve;
	}

	FVector SamplePosition(const TArray<FMT2EffectPositionKey>& Keys, float Time)
	{
		if (Keys.IsEmpty())
		{
			return FVector::ZeroVector;
		}
		if (Keys.Num() == 1 || Time <= Keys[0].Time)
		{
			return Keys[0].Position;
		}
		for (int32 Index = 1; Index < Keys.Num(); ++Index)
		{
			if (Time <= Keys[Index].Time)
			{
				const FMT2EffectPositionKey& Previous = Keys[Index - 1];
				const FMT2EffectPositionKey& Next = Keys[Index];
				const float Span = Next.Time - Previous.Time;
				const float Alpha = Span > UE_SMALL_NUMBER ? (Time - Previous.Time) / Span : 1.0f;
				if (Previous.bBezier)
				{
					const FVector Control = Previous.Position + Previous.ControlPoint;
					return FMath::Square(1.0f - Alpha) * Previous.Position
						+ 2.0f * (1.0f - Alpha) * Alpha * Control
						+ FMath::Square(Alpha) * Next.Position;
				}
				return FMath::Lerp(Previous.Position, Next.Position, Alpha);
			}
		}
		return Keys.Last().Position;
	}

	void SetPositionCurve(FRawDistributionVector& Distribution, UObject* Outer, const TArray<FMT2EffectPositionKey>& Keys)
	{
		if (Keys.IsEmpty())
		{
			SetVectorConstant(Distribution, Outer, FVector::ZeroVector);
			return;
		}
		UDistributionVectorConstantCurve* Curve = NewObject<UDistributionVectorConstantCurve>(Outer);
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			const int32 Samples = Index + 1 < Keys.Num() && Keys[Index].bBezier ? 8 : 1;
			for (int32 Sample = 0; Sample < Samples; ++Sample)
			{
				const float Alpha = static_cast<float>(Sample) / Samples;
				const float EndTime = Keys.IsValidIndex(Index + 1) ? Keys[Index + 1].Time : Keys[Index].Time;
				const float Time = FMath::Lerp(Keys[Index].Time, EndTime, Alpha);
				const FVector Value = ConvertLegacyEffectVector(SamplePosition(Keys, Time));
				const int32 KeyIndex = Curve->CreateNewKey(Time);
				Curve->SetKeyOut(0, KeyIndex, Value.X);
				Curve->SetKeyOut(1, KeyIndex, Value.Y);
				Curve->SetKeyOut(2, KeyIndex, Value.Z);
			}
		}
		const FVector LastValue = ConvertLegacyEffectVector(Keys.Last().Position);
		const int32 LastIndex = Curve->CreateNewKey(Keys.Last().Time);
		Curve->SetKeyOut(0, LastIndex, LastValue.X);
		Curve->SetKeyOut(1, LastIndex, LastValue.Y);
		Curve->SetKeyOut(2, LastIndex, LastValue.Z);
		Curve->bIsDirty = true;
		Distribution.Distribution = Curve;
	}

	UTexture2D* CreateLegacyTextureAtlas(
		const FMT2ImportContext& Context, const FMT2AssetRecord& Record, int32 EmitterIndex,
		const TArray<UTexture2D*>& Frames)
	{
		if (Frames.Num() < 2 || !Frames[0])
		{
			return Frames.IsEmpty() ? nullptr : Frames[0];
		}

		const int32 FrameWidth = Frames[0]->Source.GetSizeX();
		const int32 FrameHeight = Frames[0]->Source.GetSizeY();
		if (FrameWidth <= 0 || FrameHeight <= 0 ||
			Frames[0]->Source.GetFormat() != TSF_BGRA8)
		{
			return nullptr;
		}

		TArray<TArray64<uint8>> FramePixels;
		FramePixels.SetNum(Frames.Num());
		for (int32 FrameIndex = 0; FrameIndex < Frames.Num(); ++FrameIndex)
		{
			UTexture2D* Frame = Frames[FrameIndex];
			if (!Frame || Frame->Source.GetSizeX() != FrameWidth ||
				Frame->Source.GetSizeY() != FrameHeight ||
				Frame->Source.GetFormat() != TSF_BGRA8 ||
				!Frame->Source.GetMipData(FramePixels[FrameIndex], 0) ||
				FramePixels[FrameIndex].Num() != static_cast<int64>(FrameWidth) * FrameHeight * 4)
			{
				return nullptr;
			}
		}

		const int32 AtlasWidth = FrameWidth * Frames.Num();
		TArray64<uint8> AtlasPixels;
		AtlasPixels.SetNumUninitialized(static_cast<int64>(AtlasWidth) * FrameHeight * 4);
		const int64 SourceRowBytes = static_cast<int64>(FrameWidth) * 4;
		const int64 AtlasRowBytes = static_cast<int64>(AtlasWidth) * 4;
		for (int32 Y = 0; Y < FrameHeight; ++Y)
		{
			for (int32 FrameIndex = 0; FrameIndex < Frames.Num(); ++FrameIndex)
			{
				FMemory::Memcpy(
					AtlasPixels.GetData() + static_cast<int64>(Y) * AtlasRowBytes +
						static_cast<int64>(FrameIndex) * SourceRowBytes,
					FramePixels[FrameIndex].GetData() + static_cast<int64>(Y) * SourceRowBytes,
					SourceRowBytes);
			}
		}

		FString EffectStem = MakeParticleAssetName(Record);
		EffectStem.RemoveFromStart(TEXT("PS_"), ESearchCase::IgnoreCase);
		const FString TextureName =
			FString::Printf(TEXT("T_PS_%s_SubUV_%02d"), *EffectStem, EmitterIndex);
		const FString TexturePackagePath =
			FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Record) / TextureName;
		const FString TextureObjectPath = TexturePackagePath + TEXT(".") + TextureName;
		UPackage* TexturePackage = CreatePackage(*TexturePackagePath);
		UTexture2D* Atlas = LoadObject<UTexture2D>(nullptr, *TextureObjectPath);
		const bool bCreated = Atlas == nullptr;
		if (!Atlas)
		{
			Atlas = NewObject<UTexture2D>(
				TexturePackage, *TextureName, RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Atlas)
		{
			return nullptr;
		}

		Atlas->Modify();
		Atlas->Source.Init(AtlasWidth, FrameHeight, 1, 1, TSF_BGRA8, AtlasPixels.GetData());
		Atlas->SRGB = Frames[0]->SRGB;
		Atlas->CompressionSettings = Frames[0]->CompressionSettings;
		Atlas->MipGenSettings = TMGS_FromTextureGroup;
		Atlas->NeverStream = false;
		Atlas->PreEditChange(nullptr);
		Atlas->PostEditChange();
		Atlas->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Atlas);
		}
		return Atlas;
	}

	UMaterial* CreateLegacyEffectMaterial(
		const FMT2ImportContext& Context, const FMT2AssetRecord& Record, int32 EmitterIndex,
		UTexture2D* Texture, int32 SrcBlendType, int32 DestBlendType, bool bUseSubUV)
	{
		if (!Texture)
		{
			return nullptr;
		}
		FString EffectStem = MakeParticleAssetName(Record);
		EffectStem.RemoveFromStart(TEXT("PS_"), ESearchCase::IgnoreCase);
		const FString MaterialName = FString::Printf(TEXT("M_PS_%s_%02d"), *EffectStem, EmitterIndex);
		const FString MaterialPackagePath =
			FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Record) / MaterialName;
		const FString MaterialObjectPath = MaterialPackagePath + TEXT(".") + MaterialName;
		UPackage* MaterialPackage = CreatePackage(*MaterialPackagePath);
		UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialObjectPath);
		const bool bCreated = Material == nullptr;
		if (!Material)
		{
			Material = NewObject<UMaterial>(
				MaterialPackage, *MaterialName, RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Material)
		{
			return nullptr;
		}

		Material->Modify();
		Material->BlendMode = SrcBlendType == 5 && DestBlendType == 6
			? BLEND_Translucent : BLEND_Additive;
		Material->bUsedWithParticleSprites = true;
		Material->TwoSided = true;
		Material->SetShadingModel(MSM_Unlit);

		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		Data->ExpressionCollection.Empty();
		UMaterialExpressionTextureSample* Sample = bUseSubUV
			? static_cast<UMaterialExpressionTextureSample*>(
				NewObject<UMaterialExpressionParticleSubUV>(Material))
			: static_cast<UMaterialExpressionTextureSample*>(
				NewObject<UMaterialExpressionTextureSampleParameter2D>(Material));
		Sample->Material = Material;
		if (UMaterialExpressionTextureSampleParameter2D* Parameter =
			Cast<UMaterialExpressionTextureSampleParameter2D>(Sample))
		{
			Parameter->ParameterName = TEXT("Diffuse");
		}
		Sample->Texture = Texture;
		Sample->SamplerType = SAMPLERTYPE_Color;
		Sample->MaterialExpressionEditorX = -500;
		Sample->MaterialExpressionEditorY = -80;
		Data->ExpressionCollection.AddExpression(Sample);

		UMaterialExpressionParticleColor* ParticleColor = NewObject<UMaterialExpressionParticleColor>(Material);
		ParticleColor->Material = Material;
		ParticleColor->MaterialExpressionEditorX = -500;
		ParticleColor->MaterialExpressionEditorY = 120;
		Data->ExpressionCollection.AddExpression(ParticleColor);

		UMaterialExpressionMultiply* ColorMultiply = NewObject<UMaterialExpressionMultiply>(Material);
		ColorMultiply->Material = Material;
		ColorMultiply->A.Expression = Sample;
		ColorMultiply->B.Expression = ParticleColor;
		ColorMultiply->MaterialExpressionEditorX = -220;
		ColorMultiply->MaterialExpressionEditorY = -80;
		Data->ExpressionCollection.AddExpression(ColorMultiply);

		UMaterialExpressionMultiply* AlphaMultiply = NewObject<UMaterialExpressionMultiply>(Material);
		AlphaMultiply->Material = Material;
		AlphaMultiply->A.Expression = Sample;
		AlphaMultiply->A.OutputIndex = 4;
		AlphaMultiply->A.Mask = 1;
		AlphaMultiply->A.MaskA = 1;
		AlphaMultiply->B.Expression = ParticleColor;
		AlphaMultiply->B.OutputIndex = 4;
		AlphaMultiply->B.Mask = 1;
		AlphaMultiply->B.MaskA = 1;
		AlphaMultiply->MaterialExpressionEditorX = -220;
		AlphaMultiply->MaterialExpressionEditorY = 120;
		Data->ExpressionCollection.AddExpression(AlphaMultiply);

		Data->EmissiveColor.Expression = ColorMultiply;
		Data->Opacity.Expression = AlphaMultiply;
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Material);
		}
		return Material;
	}

	void AddLegacyLocationModules(
		UParticleSpriteEmitter* Emitter, UParticleLODLevel* LOD, const FMT2LegacyParticleSettings& Settings)
	{
		UParticleSystem* ParticleSystem = CastChecked<UParticleSystem>(Emitter->GetOuter());
		UParticleModuleLocation* Position = NewObject<UParticleModuleLocation>(ParticleSystem);
		SetPositionCurve(Position->StartLocation, Position, Settings.PositionKeys);
		Position->LODValidity = 1;
		LOD->Modules.Add(Position);

		if (Settings.EmitterShape == 1 || Settings.EmitterShape == 3)
		{
			UParticleModuleLocationPrimitiveSphere* Sphere =
				NewObject<UParticleModuleLocationPrimitiveSphere>(ParticleSystem);
			SetFloatConstant(Sphere->StartRadius, Sphere, Settings.EmittingRadius);
			Sphere->SurfaceOnly = Settings.bEmitFromEdge;
			Sphere->Positive_Z = Settings.EmitterShape == 3;
			Sphere->Negative_Z = Settings.EmitterShape == 3;
			Sphere->Velocity = Settings.EmitterAdvancedType != 0;
			SetFloatConstant(
				Sphere->VelocityScale, Sphere, Settings.EmitterAdvancedType == 1 ? 100.0f : -100.0f);
			Sphere->LODValidity = 1;
			LOD->Modules.Add(Sphere);
		}
		else if (Settings.EmitterShape == 2)
		{
			UParticleModuleLocation* Box = NewObject<UParticleModuleLocation>(ParticleSystem);
			UDistributionVectorUniform* Uniform = NewObject<UDistributionVectorUniform>(Box);
			const FVector ConvertedSize(Settings.EmittingSize.Y, Settings.EmittingSize.X, Settings.EmittingSize.Z);
			Uniform->Min = ConvertedSize * -0.5f;
			Uniform->Max = ConvertedSize * 0.5f;
			Uniform->bIsDirty = true;
			Box->StartLocation.Distribution = Uniform;
			Box->LODValidity = 1;
			LOD->Modules.Add(Box);
		}
	}

	void ApplyLegacyParticleSettings(
		UParticleSpriteEmitter* Emitter, const FMT2LegacyParticleSettings& Settings,
		int32 TextureFrameCount)
	{
		if (!Emitter || Emitter->LODLevels.IsEmpty() || !Emitter->LODLevels[0])
		{
			return;
		}
		UParticleLODLevel* LOD = Emitter->LODLevels[0];
		UParticleSystem* ParticleSystem = CastChecked<UParticleSystem>(Emitter->GetOuter());
		UParticleModuleRequired* Required = LOD->RequiredModule;
		if (Required)
		{
			Required->EmitterDelay = FMath::Max(0.0f, Settings.StartTime);
			Required->EmitterDuration = FMath::Max(0.01f, Settings.CycleLength);
			Required->EmitterDurationLow = Required->EmitterDuration;
			Required->EmitterLoops = Settings.bCycleLoop
				? FMath::Max(0, Settings.LoopCount) : 1;
			Required->bUseLocalSpace = Settings.bAttachParticles;
			Required->ScreenAlignment = Settings.bStretch ? PSA_Velocity : PSA_Rectangle;
			Required->bUseMaxDrawCount = Settings.MaxEmissionCount > 0;
			Required->MaxDrawCount = FMath::Max(0, Settings.MaxEmissionCount);
			Required->SortMode = PSORTMODE_Age_OldestFirst;
			if (TextureFrameCount > 1)
			{
				Required->SubImages_Horizontal = TextureFrameCount;
				Required->SubImages_Vertical = 1;
				Required->InterpolationMethod = Settings.TextureAnimationType == 3
					? PSUVIM_Random : PSUVIM_Linear;
				if (Settings.TextureAnimationType == 3)
				{
					const float Lifetime = FMath::Max(
						0.01f, SampleFloatKeys(Settings.LifetimeKeys, 0.0f, 1.0f));
					Required->RandomImageChanges = FMath::Max(
						1, FMath::RoundToInt(Lifetime /
							FMath::Max(0.001f, Settings.TextureAnimationDelay)));
					Required->RandomImageTime =
						1.0f / static_cast<float>(Required->RandomImageChanges);
				}
			}
		}
		if (LOD->SpawnModule)
		{
			SetFloatCurve(LOD->SpawnModule->Rate, LOD->SpawnModule, Settings.SpawnRateKeys, 0.0f);
			SetFloatConstant(LOD->SpawnModule->RateScale, LOD->SpawnModule, 1.0f);
			LOD->SpawnModule->BurstList.Reset();
		}

		for (UParticleModule* Module : LOD->Modules)
		{
			if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
			{
				SetFloatCurve(Lifetime->Lifetime, Lifetime, Settings.LifetimeKeys, 1.0f);
			}
			else if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
			{
				// Metin2 stores sprite half-size. Cascade expects the full width and height.
				SetCombinedVectorCurve(
					Size->StartSize, Size, Settings.SizeYKeys, Settings.SizeXKeys, {},
					FVector(1.0f, 1.0f, 1.0f), FVector(2.0f, 2.0f, 1.0f));
			}
			else if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
			{
				SetVelocityCurve(Velocity->StartVelocity, Velocity, Settings);
				SetFloatConstant(Velocity->StartVelocityRadial, Velocity, 0.0f);
			}
			else if (UParticleModuleColorOverLife* Color = Cast<UParticleModuleColorOverLife>(Module))
			{
				SetCombinedVectorCurve(
					Color->ColorOverLife, Color,
					Settings.ColorRKeys, Settings.ColorGKeys, Settings.ColorBKeys, FVector::OneVector);
				SetFloatCurve(Color->AlphaOverLife, Color, Settings.AlphaKeys, 1.0f);
			}
		}

		UParticleModuleSizeScale* SizeScale = NewObject<UParticleModuleSizeScale>(ParticleSystem);
		SetCombinedVectorCurve(
			SizeScale->SizeScale, SizeScale,
			Settings.ScaleYKeys, Settings.ScaleXKeys, {}, FVector::OneVector);
		SizeScale->LODValidity = 1;
		LOD->Modules.Add(SizeScale);

		AddLegacyLocationModules(Emitter, LOD, Settings);

		if (TextureFrameCount > 1)
		{
			UParticleModuleSubUVMovie* Movie = NewObject<UParticleModuleSubUVMovie>(ParticleSystem);
			SetFloatConstant(
				Movie->FrameRate, Movie,
				1.0f / FMath::Max(0.001f, Settings.TextureAnimationDelay));
			Movie->StartingFrame =
				Settings.bRandomTextureStartFrame || Settings.TextureAnimationType == 4 ? 0 : 1;
			Movie->bUseEmitterTime = false;
			Movie->LODValidity = 1;
			LOD->Modules.Add(Movie);
		}

		if (!Settings.GravityKeys.IsEmpty())
		{
			UParticleModuleAccelerationOverLifetime* Acceleration =
				NewObject<UParticleModuleAccelerationOverLifetime>(ParticleSystem);
			SetCombinedVectorCurve(
				Acceleration->AccelOverLife, Acceleration, {}, {}, Settings.GravityKeys,
				FVector::ZeroVector, FVector(1.0f, 1.0f, -1.0f));
			Acceleration->LODValidity = 1;
			LOD->Modules.Add(Acceleration);
		}
		if (!Settings.AirResistanceKeys.IsEmpty())
		{
			UParticleModuleAccelerationDrag* Drag =
				NewObject<UParticleModuleAccelerationDrag>(ParticleSystem);
			SetFloatCurve(
				Drag->DragCoefficientRaw, Drag, Settings.AirResistanceKeys,
				SampleFloatKeys(Settings.AirResistanceKeys, 0.0f, 0.0f));
			Drag->LODValidity = 1;
			LOD->Modules.Add(Drag);
		}
		if (Settings.RotationStartMax > Settings.RotationStartMin || !FMath::IsNearlyZero(Settings.RotationStartMin))
		{
			UParticleModuleRotation* Rotation = NewObject<UParticleModuleRotation>(ParticleSystem);
			UDistributionFloatUniform* Uniform = NewObject<UDistributionFloatUniform>(Rotation);
			Uniform->Min = Settings.RotationStartMin / 360.0f;
			Uniform->Max = Settings.RotationStartMax / 360.0f;
			Uniform->bIsDirty = true;
			Rotation->StartRotation.Distribution = Uniform;
			Rotation->LODValidity = 1;
			LOD->Modules.Add(Rotation);
		}
		if (!FMath::IsNearlyZero(Settings.RotationSpeed) || !Settings.RotationKeys.IsEmpty())
		{
			UParticleModuleRotationRate* RotationRate =
				NewObject<UParticleModuleRotationRate>(ParticleSystem);
			const float Sign = Settings.RotationType == 2 ? -1.0f : 1.0f;
			if (Settings.RotationType == 1 && !Settings.RotationKeys.IsEmpty())
			{
				SetFloatCurve(
					RotationRate->StartRotationRate, RotationRate, Settings.RotationKeys,
					0.0f, 1.0f / 360.0f);
			}
			else
			{
				SetFloatConstant(
					RotationRate->StartRotationRate, RotationRate,
					Sign * Settings.RotationSpeed / 360.0f);
			}
			RotationRate->LODValidity = 1;
			LOD->Modules.Add(RotationRate);
		}
		if (Settings.BillboardType == 3)
		{
			UParticleModuleOrientationAxisLock* AxisLock =
				NewObject<UParticleModuleOrientationAxisLock>(ParticleSystem);
			AxisLock->LockAxisFlags = EPAL_Z;
			AxisLock->LODValidity = 1;
			LOD->Modules.Add(AxisLock);
		}
		LOD->UpdateModuleLists();
	}

	bool NormalizeCascadeModuleOuters(UParticleSystem* ParticleSystem, FString& OutError)
	{
		if (!ParticleSystem)
		{
			OutError = TEXT("Particle system is null.");
			return false;
		}

		TSet<UParticleModule*> ModulesToMove;
		for (UParticleEmitter* Emitter : ParticleSystem->Emitters)
		{
			if (!Emitter)
			{
				continue;
			}

			TArray<UObject*> EmitterObjects;
			GetObjectsWithOuter(Emitter, EmitterObjects, true);
			for (UObject* Object : EmitterObjects)
			{
				if (UParticleModule* Module = Cast<UParticleModule>(Object))
				{
					ModulesToMove.Add(Module);
				}
			}
		}

		for (UParticleModule* Module : ModulesToMove)
		{
			const FName UniqueName = MakeUniqueObjectName(
				ParticleSystem, Module->GetClass(), Module->GetFName());
			if (!Module->Rename(
				*UniqueName.ToString(), ParticleSystem,
				REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty))
			{
				OutError = FString::Printf(
					TEXT("Could not move Cascade module %s to particle system %s."),
					*Module->GetPathName(), *ParticleSystem->GetPathName());
				return false;
			}
		}

		for (UParticleEmitter* Emitter : ParticleSystem->Emitters)
		{
			if (!Emitter)
			{
				continue;
			}
			for (UParticleLODLevel* LOD : Emitter->LODLevels)
			{
				if (!LOD)
				{
					continue;
				}
				TArray<UParticleModule*> ReferencedModules = LOD->Modules;
				ReferencedModules.Add(LOD->RequiredModule);
				ReferencedModules.Add(LOD->SpawnModule);
				ReferencedModules.Add(LOD->TypeDataModule);
				for (const UParticleModule* Module : ReferencedModules)
				{
					if (Module && Module->GetOuter() != ParticleSystem)
					{
						OutError = FString::Printf(
							TEXT("Cascade module %s still has invalid outer %s."),
							*Module->GetPathName(), *GetNameSafe(Module->GetOuter()));
						return false;
					}
				}
			}
		}
		return true;
	}
}

FMT2EffectImporter::FMT2EffectImporter()
	: FMT2ImporterBase(EMT2ImportDomain::Effects, TEXT("EffectImporter"))
{
}

bool FMT2EffectImporter::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
{
	OutDiscovery.Domain = GetDomain();
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	for (const FMT2AssetRecord& Record : ScanResult.GetRecordsByKind(EMT2AssetKind::EffectScript))
	{
		const FString ObjectPath = BuildObjectPath(Context, Record);
		if (Context.bReplaceExisting ||
			!AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid())
		{
			OutDiscovery.AssetRecords.Add(Record);
			OutDiscovery.EntryNames.Add(Record.VirtualPath.IsEmpty() ? Record.ContentPath : Record.VirtualPath);
		}
	}
	OutDiscovery.ItemsDiscovered = OutDiscovery.AssetRecords.Num();
	return true;
}

bool FMT2EffectImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	if (Request.Selection.AssetRecords.Num() == 0)
	{
		OutResult.AddWarning(TEXT("No effect records were selected."));
		OutResult.bSucceeded = true;
		return true;
	}

	const int32 MaxItems = Request.MaxItems > 0 ? Request.MaxItems : Request.Selection.AssetRecords.Num();
	FScopedSlowTask EffectProgress(
		static_cast<float>(FMath::Max(1, MaxItems * 3)),
		NSLOCTEXT("FMT2EffectImporter", "ImportEffectsProgress", "Importing particle effects..."));
	EffectProgress.MakeDialog(true);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	int32 ConsideredCount = 0;
	int32 CreatedCount = 0;

	for (const FMT2AssetRecord& Record : Request.Selection.AssetRecords)
	{
		if (ConsideredCount >= MaxItems)
		{
			break;
		}

		EffectProgress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("FMT2EffectImporter", "ResolveEffectProgressFormat", "Resolving effect {0} of {1}: {2}"),
			FText::AsNumber(ConsideredCount + 1),
			FText::AsNumber(MaxItems),
			FText::FromString(Record.VirtualPath.IsEmpty() ? Record.ContentPath : Record.VirtualPath)));
		if (Request.Context.IsStopRequested() || EffectProgress.ShouldCancel())
		{
			OutResult.AddWarning(TEXT("Effect import stopped by user."));
			break;
		}

		ConsideredCount++;
		OutResult.ItemsDiscovered++;

		if (Record.Kind != EMT2AssetKind::EffectScript || !IFileManager::Get().FileExists(*Record.AbsolutePath))
		{
			OutResult.ItemsSkipped++;
			continue;
		}

		FMT2LegacyEffectSettings LegacyEffect;
		if (!ParseLegacyEffect(Record, LegacyEffect))
		{
			OutResult.ItemsSkipped++;
			OutResult.AddError(TEXT("Effect script contains no readable particle, mesh, or light elements."), Record.AbsolutePath);
			continue;
		}

		const FString ObjectPath = BuildObjectPath(Request.Context, Record);
		if (!Request.Context.bReplaceExisting &&
			AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid())
		{
			OutResult.ItemsSkipped++;
			continue;
		}

		TArray<FMT2AssetRecord> TextureRecordsToImport;
		TArray<FString> TextureObjectPaths;
		TSet<FString> TextureRecordPaths;
		TArray<FString> TextureReferences;
		ExtractTextureReferences(Record, TextureReferences);
		if (Request.Context.ScanResult && Request.Context.bImportReferencedTextures)
		{
			for (const FString& TextureReference : TextureReferences)
			{
				if (const FMT2AssetRecord* TextureRecord =
					FMT2TextureImporter::FindRecordForReference(*Request.Context.ScanResult, TextureReference, Record))
				{
					const FString TextureObjectPath = FMT2TextureImporter::BuildObjectPath(Request.Context, *TextureRecord);
					TextureObjectPaths.AddUnique(TextureObjectPath);
					if (!AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(TextureObjectPath)).IsValid() &&
						!TextureRecordPaths.Contains(TextureRecord->AbsolutePath))
					{
						TextureRecordsToImport.Add(*TextureRecord);
						TextureRecordPaths.Add(TextureRecord->AbsolutePath);
					}
				}
				else if (Request.Context.bEnableDebugLogs)
				{
					OutResult.AddWarning(FString::Printf(TEXT("Effect texture reference not found: %s"), *TextureReference), Record.AbsolutePath);
				}
			}
		}

		if (TextureRecordsToImport.Num() > 0)
		{
			EffectProgress.EnterProgressFrame(1.0f, FText::Format(
				NSLOCTEXT("FMT2EffectImporter", "ImportEffectTexturesProgressFormat", "Importing {0} texture(s) for {1}"),
				FText::AsNumber(TextureRecordsToImport.Num()),
				FText::FromString(MakeParticleAssetName(Record))));

			FMT2ImportRequest TextureRequest;
			TextureRequest.Context = Request.Context;
			TextureRequest.Selection.Domain = EMT2ImportDomain::Textures;
			TextureRequest.Selection.AssetRecords = TextureRecordsToImport;
			TextureRequest.MaxItems = TextureRecordsToImport.Num();

			FMT2ImportResult TextureResult;
			FMT2TextureImporter TextureImporter;
			TextureImporter.Import(TextureRequest, TextureResult);
			OutResult.Messages.Append(TextureResult.Messages);
		}
		else
		{
			EffectProgress.EnterProgressFrame(1.0f);
		}

		EffectProgress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("FMT2EffectImporter", "CreateEffectProgressFormat", "Creating particle system {0}"),
			FText::FromString(MakeParticleAssetName(Record))));

		OutResult.CreatedPackages.Add(ObjectPath);
		if (Request.Context.bDryRun)
		{
			OutResult.ItemsSkipped++;
			continue;
		}

		const FString PackagePath = FPackageName::ObjectPathToPackageName(ObjectPath);
		// Never reuse an existing particle object. Older importer versions created modules under
		// emitters instead of the particle system. Re-saving over one of those files can preserve its
		// invalid nested exports when the package was not loaded, so discard both the resident object
		// tree and generated package file before creating the replacement.
		if (UParticleSystem* ExistingParticleSystem =
			FindObject<UParticleSystem>(nullptr, *ObjectPath))
		{
			ExistingParticleSystem->Modify();
			ExistingParticleSystem->ClearFlags(RF_Public | RF_Standalone);
			const FName DiscardedName = MakeUniqueObjectName(
				GetTransientPackage(), UParticleSystem::StaticClass(),
				FName(*FString::Printf(TEXT("Discarded_%s"), *MakeParticleAssetName(Record))));
			ExistingParticleSystem->Rename(
				*DiscardedName.ToString(), GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		}
		if (Request.Context.bReplaceExisting)
		{
			bool bCouldRemoveGeneratedPackage = true;
			if (UPackage* ExistingPackage = FindPackage(nullptr, *PackagePath))
			{
				ResetLoaders(ExistingPackage);
			}

			const FString PackageFilename = FPackageName::LongPackageNameToFilename(
				PackagePath, FPackageName::GetAssetPackageExtension());
			const TArray<FString> GeneratedFiles = {
				PackageFilename,
				FPaths::ChangeExtension(PackageFilename, TEXT("uexp")),
				FPaths::ChangeExtension(PackageFilename, TEXT("ubulk"))
			};
			for (const FString& GeneratedFile : GeneratedFiles)
			{
				if (IFileManager::Get().FileExists(*GeneratedFile) &&
					!IFileManager::Get().Delete(*GeneratedFile, false, true, true))
				{
					OutResult.ItemsSkipped++;
					OutResult.AddError(FString::Printf(
						TEXT("Could not replace generated effect package file: %s"),
						*GeneratedFile), Record.AbsolutePath);
					bCouldRemoveGeneratedPackage = false;
					break;
				}
			}
			if (!bCouldRemoveGeneratedPackage)
			{
				continue;
			}
		}

		UPackage* Package = CreatePackage(*PackagePath);
		UParticleSystem* ParticleSystem = NewObject<UParticleSystem>(
			Package,
			*MakeParticleAssetName(Record),
			RF_Public | RF_Standalone | RF_Transactional);
		if (!ParticleSystem)
		{
			OutResult.ItemsSkipped++;
			OutResult.AddError(FString::Printf(TEXT("Failed to create particle system: %s"), *ObjectPath), Record.AbsolutePath);
			continue;
		}

		for (int32 EmitterIndex = 0; EmitterIndex < LegacyEffect.Particles.Num(); ++EmitterIndex)
		{
			const FMT2LegacyParticleSettings& Settings = LegacyEffect.Particles[EmitterIndex];
			TArray<UTexture2D*> EmitterFrames;
			if (Request.Context.ScanResult)
			{
				for (const FString& TextureReference : Settings.TextureReferences)
				{
					if (const FMT2AssetRecord* TextureRecord =
						FMT2TextureImporter::FindRecordForReference(
							*Request.Context.ScanResult, TextureReference, Record))
					{
						if (UTexture2D* Frame = LoadObject<UTexture2D>(
							nullptr, *FMT2TextureImporter::BuildObjectPath(
								Request.Context, *TextureRecord)))
						{
							EmitterFrames.Add(Frame);
						}
					}
				}
			}
			if (EmitterFrames.IsEmpty() && TextureObjectPaths.IsValidIndex(EmitterIndex))
			{
				if (UTexture2D* Frame =
					LoadObject<UTexture2D>(nullptr, *TextureObjectPaths[EmitterIndex]))
				{
					EmitterFrames.Add(Frame);
				}
			}
			if (Settings.TextureAnimationType == 2)
			{
				Algo::Reverse(EmitterFrames);
			}

			UTexture2D* EmitterTexture = EmitterFrames.IsEmpty() ? nullptr : EmitterFrames[0];
			int32 ImportedFrameCount = 1;
			if (EmitterFrames.Num() > 1)
			{
				if (UTexture2D* Atlas = CreateLegacyTextureAtlas(
					Request.Context, Record, EmitterIndex, EmitterFrames))
				{
					EmitterTexture = Atlas;
					ImportedFrameCount = EmitterFrames.Num();
				}
				else
				{
					OutResult.AddWarning(FString::Printf(
						TEXT("Could not create SubUV atlas for emitter %d; using its first frame."),
						EmitterIndex), Record.AbsolutePath);
				}
			}

			UMaterial* EffectMaterial = CreateLegacyEffectMaterial(
				Request.Context, Record, EmitterIndex, EmitterTexture,
				Settings.SrcBlendType, Settings.DestBlendType, ImportedFrameCount > 1);
			UParticleSpriteEmitter* SpriteEmitter =
				NewObject<UParticleSpriteEmitter>(ParticleSystem, NAME_None, RF_Transactional);
			if (!SpriteEmitter)
			{
				continue;
			}
			SpriteEmitter->EmitterName = FName(*FString::Printf(
				TEXT("%s_%02d"), *MakeParticleAssetName(Record), EmitterIndex));
			SpriteEmitter->CreateLODLevel(0, false);
			SpriteEmitter->SetToSensibleDefaults();
			ApplyLegacyParticleSettings(SpriteEmitter, Settings, ImportedFrameCount);
			if (EffectMaterial && !SpriteEmitter->LODLevels.IsEmpty() &&
				SpriteEmitter->LODLevels[0] && SpriteEmitter->LODLevels[0]->RequiredModule)
			{
				SpriteEmitter->LODLevels[0]->RequiredModule->Material = EffectMaterial;
			}
			ParticleSystem->Emitters.Add(SpriteEmitter);

			FLODSoloTrack SoloTrack;
			SoloTrack.SoloEnableSetting.Add(true);
			ParticleSystem->SoloTracking.Add(SoloTrack);
		}

		if (LegacyEffect.BoundingSphereRadius > 0.0f)
		{
			const FVector Center = ConvertLegacyEffectVector(LegacyEffect.BoundingSpherePosition);
			const FVector Extent(LegacyEffect.BoundingSphereRadius);
			ParticleSystem->bUseFixedRelativeBoundingBox = true;
			ParticleSystem->FixedRelativeBoundingBox = FBox(Center - Extent, Center + Extent);
		}
		if (LegacyEffect.MeshElementCount > 0)
		{
			OutResult.AddWarning(FString::Printf(
				TEXT("%s contains %d animated MDE mesh element(s); particle elements were imported, MDE translation is pending."),
				*Record.VirtualPath, LegacyEffect.MeshElementCount), Record.AbsolutePath);
		}
		if (LegacyEffect.LightElementCount > 0)
		{
			OutResult.AddWarning(FString::Printf(
				TEXT("%s contains %d legacy light element(s); particle elements were imported."),
				*Record.VirtualPath, LegacyEffect.LightElementCount), Record.AbsolutePath);
		}
		if (LegacyEffect.Particles.IsEmpty())
		{
			OutResult.ItemsSkipped++;
			OutResult.AddWarning(TEXT("Effect has no particle elements that can be represented yet."), Record.AbsolutePath);
			continue;
		}

		FString ModuleOuterError;
		if (!NormalizeCascadeModuleOuters(ParticleSystem, ModuleOuterError))
		{
			OutResult.ItemsSkipped++;
			OutResult.AddError(ModuleOuterError, Record.AbsolutePath);
			continue;
		}

		ParticleSystem->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(ParticleSystem);
		CreatedCount++;
	}

	OutResult.ItemsImported = CreatedCount;
	if (CreatedCount > 0)
	{
		OutResult.AddInfo(TEXT("Created Cascade systems with one emitter per legacy particle element and imported their source curves, textures, materials, blend modes, shapes, delays, and loops."));
	}
	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}

FString FMT2EffectImporter::BuildDestinationPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
{
	return FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Record);
}

FString FMT2EffectImporter::BuildObjectPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
{
	const FString AssetName = MakeParticleAssetName(Record);
	const FString PackagePath = BuildDestinationPath(Context, Record) / AssetName;
	return PackagePath + TEXT(".") + AssetName;
}

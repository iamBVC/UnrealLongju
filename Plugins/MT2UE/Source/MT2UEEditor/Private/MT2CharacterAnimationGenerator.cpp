/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2CharacterAnimationGenerator.h"
#include "Config/MT2PathSettings.h"
#include "MT2AnimBlueprintBuilder.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/MT2AnimationMotionData.h"
#include "Animation/MT2CharacterAnimInstance.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "FileHelpers.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Importers/MT2GrannyMeshConverter.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"

namespace
{
	bool SanitizeAttackRootTrack(UAnimSequence* Sequence)
	{
		if (!Sequence || !Sequence->GetSkeleton()) return false;
		const FReferenceSkeleton& RefSkeleton = Sequence->GetSkeleton()->GetReferenceSkeleton();
		if (RefSkeleton.GetNum() == 0) return false;

		const FName RootName = RefSkeleton.GetBoneName(0);
		const FTransform& ReferenceRoot = RefSkeleton.GetRefBonePose()[0];
		const IAnimationDataModel* DataModel = Sequence->GetDataModel();
		if (!DataModel || !DataModel->IsValidBoneTrackName(RootName)) return false;

		TArray<FTransform> RootTransforms;
		DataModel->GetBoneTrackTransforms(RootName, RootTransforms);
		if (RootTransforms.IsEmpty()) return false;

		TArray<FVector3f> TranslationKeys;
		TArray<FQuat4f> RotationKeys;
		TArray<FVector3f> ScaleKeys;
		TranslationKeys.Reserve(RootTransforms.Num());
		RotationKeys.Reserve(RootTransforms.Num());
		ScaleKeys.Reserve(RootTransforms.Num());
		for (const FTransform& Transform : RootTransforms)
		{
			// GrannyUpdateModelMatrix consumes horizontal root motion separately. Keep the animated Z
			// in the visual pose so feet follow the ground, but root X/Y and rotation must stay in the
			// skeleton basis or individual combo clips tilt/turn the whole character.
			TranslationKeys.Emplace(
				static_cast<float>(ReferenceRoot.GetTranslation().X),
				static_cast<float>(ReferenceRoot.GetTranslation().Y),
				static_cast<float>(Transform.GetTranslation().Z));
			RotationKeys.Emplace(ReferenceRoot.GetRotation());
			ScaleKeys.Emplace(ReferenceRoot.GetScale3D());
		}

		IAnimationDataController& Controller = Sequence->GetController();
		IAnimationDataController::FScopedBracket Bracket(
			Controller,
			NSLOCTEXT("MT2CharacterAnimationGenerator", "SanitizeAttackRoot", "Sanitizing attack root track"),
			false);
		return Controller.SetBoneTrackKeys(
			RootName, TranslationKeys, RotationKeys, ScaleKeys, false);
	}

	struct FCharacterProfile
	{
		const TCHAR* Race;
		const TCHAR* Sex;
		const TCHAR* SourceFolder;
	};

	const FCharacterProfile CharacterProfiles[] =
	{
		{TEXT("Warrior"), TEXT("Male"), TEXT("pc")},
		{TEXT("Warrior"), TEXT("Female"), TEXT("pc2")},
		{TEXT("Assassin"), TEXT("Female"), TEXT("pc")},
		{TEXT("Assassin"), TEXT("Male"), TEXT("pc2")},
		{TEXT("Sura"), TEXT("Male"), TEXT("pc")},
		{TEXT("Sura"), TEXT("Female"), TEXT("pc2")},
		{TEXT("Shaman"), TEXT("Female"), TEXT("pc")},
		{TEXT("Shaman"), TEXT("Male"), TEXT("pc2")}
	};

	FString GetRaceLower(const FCharacterProfile& Profile)
	{
		return FString(Profile.Race).ToLower();
	}

	bool ReadGeneratedMotionFloat(const FString& Script, const FString& SettingName, float& OutValue)
	{
		TArray<FString> Lines;
		Script.ParseIntoArrayLines(Lines, false);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (!Line.StartsWith(SettingName, ESearchCase::IgnoreCase)) continue;
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() >= 2)
			{
				OutValue = FCString::Atof(*Tokens[1]);
				return true;
			}
		}
		return false;
	}

	bool ReadGeneratedMotionVector(const FString& Script, const FString& SettingName, FVector& OutValue)
	{
		TArray<FString> Lines;
		Script.ParseIntoArrayLines(Lines, false);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (!Line.StartsWith(SettingName, ESearchCase::IgnoreCase)) continue;
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() >= 4)
			{
				OutValue = FVector(
					FCString::Atof(*Tokens[1]), FCString::Atof(*Tokens[2]), FCString::Atof(*Tokens[3]));
				return true;
			}
		}
		return false;
	}

	void ApplySourceMotionMetadata(
		const FString& SourceRoot,
		const FCharacterProfile& Profile,
		const FString& RelativeFolder,
		const FString& MotionName,
		UAnimSequence* Sequence)
	{
		if (!Sequence) return;
		const FString ScriptPath = SourceRoot / Profile.SourceFolder / GetRaceLower(Profile) /
			RelativeFolder / (MotionName + TEXT(".msa"));
		FString Script;
		if (!FFileHelper::LoadFileToString(Script, *ScriptPath)) return;

		UMT2AnimationMotionData* MotionData = Sequence->GetAssetUserData<UMT2AnimationMotionData>();
		if (!MotionData)
		{
			MotionData = NewObject<UMT2AnimationMotionData>(Sequence);
			Sequence->AddAssetUserData(MotionData);
		}
		ReadGeneratedMotionFloat(Script, TEXT("MotionDuration"), MotionData->MotionDuration);
		// .msa AttackingData: knockback strength + hit kind (1 = blow, 2 = normal).
		UMT2AnimationMotionData::ReadKnockbackMetadata(Script, MotionData->ExternalForce, MotionData->HittingType);
		UMT2AnimationMotionData::ReadAttackEvents(Script, MotionData->AttackEvents);
		MotionData->bHasAccumulation = ReadGeneratedMotionVector(Script, TEXT("Accumulation"), MotionData->Accumulation) &&
			!MotionData->Accumulation.IsNearlyZero();
		MotionData->bHasComboInputData =
			ReadGeneratedMotionFloat(Script, TEXT("DirectInputTime"), MotionData->DirectInputTime);
		ReadGeneratedMotionFloat(Script, TEXT("PreInputTime"), MotionData->PreInputTime);
		ReadGeneratedMotionFloat(Script, TEXT("InputLimitTime"), MotionData->InputLimitTime);
		ReadGeneratedMotionFloat(Script, TEXT("LinkTime"), MotionData->LinkTime);
	}

	FString BuildMeshPath(const FCharacterProfile& Profile)
	{
		const FString Race = GetRaceLower(Profile);
		const FString MeshName = FString::Printf(TEXT("SK_%s_novice"), *Race);
		return UMT2PathSettings::Format(TEXT("ymir_work_Name_Name_Name_novice_SkeletalMeshes_Name"), TEXT("%s%s%s%s%s"),
			Profile.SourceFolder, *Race, *Race, *MeshName, *MeshName);
	}

	FString BuildAnimationPath(const FCharacterProfile& Profile, const TCHAR* AnimationName)
	{
		const FString AssetName = FString::Printf(TEXT("A_%s"), AnimationName);
		return UMT2PathSettings::Format(TEXT("ymir_work_Name_Name_general_Name"), TEXT("%s%s%s%s"),
			Profile.SourceFolder, *GetRaceLower(Profile), *AssetName, *AssetName);
	}

	UAnimSequence* ResolveAnimation(const FCharacterProfile& Profile, const TCHAR* AnimationName)
	{
		return LoadObject<UAnimSequence>(nullptr, *BuildAnimationPath(Profile, AnimationName));
	}

	FString BuildBlueprintName(const FCharacterProfile& Profile)
	{
		return FString::Printf(TEXT("ABP_%s_%s"), Profile.Race, Profile.Sex);
	}

	UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction, int32 Index = 0)
	{
		int32 CurrentIndex = 0;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction &&
				Pin->PinType.PinSubCategoryObject.Get() == FPoseLink::StaticStruct())
			{
				if (CurrentIndex++ == Index)
				{
					return Pin;
				}
			}
		}
		return nullptr;
	}

	UEdGraphPin* FindBooleanInputPin(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input &&
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	void SetBlendTimes(UAnimGraphNode_BlendListByBool* Node, float BlendTime)
	{
		if (!Node)
		{
			return;
		}

		Node->ReconstructNode();
		FArrayProperty* BlendTimeProperty =
			FindFProperty<FArrayProperty>(FAnimNode_BlendListBase::StaticStruct(), TEXT("BlendTime"));
		FFloatProperty* FloatProperty = BlendTimeProperty
			? CastField<FFloatProperty>(BlendTimeProperty->Inner)
			: nullptr;
		if (!BlendTimeProperty || !FloatProperty)
		{
			return;
		}

		FScriptArrayHelper BlendTimeHelper(BlendTimeProperty, BlendTimeProperty->ContainerPtrToValuePtr<void>(&Node->Node));
		if (BlendTimeHelper.Num() < 2)
		{
			BlendTimeHelper.AddValues(2 - BlendTimeHelper.Num());
		}
		for (int32 Index = 0; Index < BlendTimeHelper.Num(); ++Index)
		{
			FloatProperty->SetFloatingPointPropertyValue(BlendTimeHelper.GetRawPtr(Index), BlendTime);
		}
	}

	UEdGraphPin* ExposeFloatInputPin(UEdGraphNode* Node, const TCHAR* PinName)
	{
		UEdGraphPin* Pin = Node ? Node->FindPin(PinName) : nullptr;
		if (Pin)
		{
			Pin->bHidden = false;
		}
		return Pin;
	}

	UEdGraphPin* ExposeSequencePlayerPlayRatePin(UAnimGraphNode_SequencePlayer* Node)
	{
		if (!Node)
		{
			return nullptr;
		}

		const FName PlayRateName(TEXT("PlayRate"));
		FOptionalPinFromProperty* OptionalPin = Node->ShowPinForProperties.FindByPredicate(
			[PlayRateName](const FOptionalPinFromProperty& Candidate)
			{
				return Candidate.PropertyName == PlayRateName;
			});

		if (OptionalPin)
		{
			OptionalPin->bShowPin = true;
		}
		else
		{
			Node->ShowPinForProperties.Add(FOptionalPinFromProperty(
				PlayRateName,
				true,
				true,
				TEXT("Play Rate"),
				NSLOCTEXT("MT2CharacterAnimationGenerator", "PlayRatePinTooltip", "Scales animation playback speed."),
				false,
				TEXT("Settings"),
				false));
		}

		Node->ReconstructNode();
		return ExposeFloatInputPin(Node, TEXT("PlayRate"));
	}

	template <typename NodeType>
	NodeType* AddGraphNode(UEdGraph* Graph, int32 X, int32 Y)
	{
		FGraphNodeCreator<NodeType> Creator(*Graph);
		NodeType* Node = Creator.CreateNode();
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		return Node;
	}

	bool ImportBaseAnimation(
		const FString& SourceRoot,
		const FCharacterProfile& Profile,
		const TCHAR* AnimationName,
		FMT2CharacterAnimationGenerationResult& OutResult,
		TArray<UPackage*>& OutPackages)
	{
		const FString ObjectPath = BuildAnimationPath(Profile, AnimationName);
		if (UAnimSequence* ExistingSequence = ResolveAnimation(Profile, AnimationName))
		{
			return ExistingSequence->GetSkeleton() != nullptr;
		}

		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *BuildMeshPath(Profile));
		if (!Mesh || !Mesh->GetSkeleton())
		{
			OutResult.Errors.Add(FString::Printf(TEXT("Missing target mesh for %s %s."), Profile.Race, Profile.Sex));
			return false;
		}

		const FString Race = GetRaceLower(Profile);
		const FString AnimationGrannyPath = SourceRoot / Profile.SourceFolder / Race / UMT2PathSettings::Path(TEXT("Part_general")) /
			(FString(AnimationName) + TEXT(".gr2"));
		const FString ModelGrannyPath = SourceRoot / Profile.SourceFolder / Race /
			FString::Printf(TEXT("%s_novice.gr2"), *Race);
		if (!FPaths::FileExists(AnimationGrannyPath) || !FPaths::FileExists(ModelGrannyPath))
		{
			OutResult.Errors.Add(FString::Printf(TEXT("Missing Granny source for %s."), *ObjectPath));
			return false;
		}

		FMT2GrannyAnimationClip Clip;
		FString Error;
		if (!FMT2GrannyMeshConverter::ExtractAnimationClipForModel(
			AnimationGrannyPath,
			ModelGrannyPath,
			Clip,
			Error,
			&Mesh->GetRefSkeleton()))
		{
			OutResult.Errors.Add(FString::Printf(TEXT("Animation extraction failed for %s: %s"), *ObjectPath, *Error));
			return false;
		}

		const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
		const FString AssetName = FPackageName::ObjectPathToObjectName(ObjectPath);
		UPackage* Package = CreatePackage(*PackageName);
		UAnimSequence* Sequence = NewObject<UAnimSequence>(
			Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Sequence)
		{
			OutResult.Errors.Add(FString::Printf(TEXT("Could not create %s."), *ObjectPath));
			return false;
		}

		Sequence->SetSkeleton(Mesh->GetSkeleton());
		Sequence->SetPreviewMesh(Mesh, false);
		IAnimationDataController& Controller = Sequence->GetController();
		Controller.InitializeModel();
		int32 ImportedTrackCount = 0;
		{
			IAnimationDataController::FScopedBracket Bracket(
				Controller,
				NSLOCTEXT("MT2CharacterAnimationGenerator", "ImportAnimation", "Importing player animation"),
				false);
			Controller.ResetModel(false);
			Controller.SetFrameRate(FFrameRate(Clip.FrameRate, 1), false);
			Controller.SetNumberOfFrames(FFrameNumber(Clip.NumberOfFrames), false);
			const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
			for (const FMT2GrannyAnimationTrack& Track : Clip.Tracks)
			{
				const FName BoneName(*Track.BoneName);
				if (RefSkeleton.FindBoneIndex(BoneName) != INDEX_NONE &&
					Controller.AddBoneCurve(BoneName, false) &&
					Controller.SetBoneTrackKeys(
						BoneName, Track.TranslationKeys, Track.RotationKeys, Track.ScaleKeys, false))
				{
					ImportedTrackCount++;
				}
			}
			Controller.NotifyPopulated();
		}
		if (ImportedTrackCount == 0)
		{
			OutResult.Errors.Add(FString::Printf(TEXT("No tracks mapped for %s."), *ObjectPath));
			return false;
		}

		Sequence->PostEditChange();
		Sequence->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Sequence);
		OutPackages.AddUnique(Package);
		OutResult.AnimationsImported++;
		return true;
	}

	bool ImportMissingBaseAnimations(
		const FString& SourceRoot,
		FMT2CharacterAnimationGenerationResult& OutResult,
		TArray<UPackage*>& OutPackages)
	{
		if (!FPaths::DirectoryExists(SourceRoot))
		{
			OutResult.Errors.Add(FString::Printf(TEXT("Animation source folder does not exist: %s"), *SourceRoot));
			return false;
		}

		bool bSucceeded = true;
		for (const FCharacterProfile& Profile : CharacterProfiles)
		{
			for (const TCHAR* Name : {TEXT("wait"), TEXT("walk"), TEXT("run")})
			{
				bSucceeded &= ImportBaseAnimation(SourceRoot, Profile, Name, OutResult, OutPackages);
			}
		}
		return bSucceeded;
	}

	bool CreateAnimationBlueprint(
		const FCharacterProfile& Profile,
		UClass* ParentClass,
		FMT2CharacterAnimationGenerationResult& OutResult,
		TArray<UPackage*>& OutPackages)
	{
		const FString BlueprintName = BuildBlueprintName(Profile);
		const FString ObjectPath = UMT2PathSettings::Format(TEXT("Characters_Animations_Name"), TEXT("%s%s"), *BlueprintName, *BlueprintName);
		UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
		const bool bUpdatingExisting = Blueprint != nullptr;

		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *BuildMeshPath(Profile));
		UAnimSequence* Idle = ResolveAnimation(Profile, TEXT("wait"));
		UAnimSequence* Walk = ResolveAnimation(Profile, TEXT("walk"));
		UAnimSequence* Run = ResolveAnimation(Profile, TEXT("run"));
		if (!Mesh || !Idle || !Walk || !Run)
		{
			OutResult.Errors.Add(FString::Printf(
				TEXT("Missing mesh, wait, walk, or run animation for %s %s."), Profile.Race, Profile.Sex));
			return false;
		}
		if (Mesh->GetSkeleton() != Idle->GetSkeleton() || Mesh->GetSkeleton() != Walk->GetSkeleton() ||
			Mesh->GetSkeleton() != Run->GetSkeleton())
		{
			OutResult.Errors.Add(FString::Printf(
				TEXT("Animation skeleton mismatch for %s %s."), Profile.Race, Profile.Sex));
			return false;
		}

		if (!Blueprint)
		{
			UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
			Factory->ParentClass = ParentClass;
			Factory->TargetSkeleton = Mesh->GetSkeleton();
			Factory->PreviewSkeletalMesh = Mesh;
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			Blueprint = Cast<UAnimBlueprint>(AssetTools.CreateAsset(
				BlueprintName,
				UMT2PathSettings::Path(TEXT("CharacterAnimationRoot")),
				UAnimBlueprint::StaticClass(),
				Factory));
		}
		if (!Blueprint)
		{
			OutResult.Errors.Add(FString::Printf(TEXT("Could not create %s."), *BlueprintName));
			return false;
		}

		UAnimationGraph* AnimGraph = nullptr;
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if ((AnimGraph = Cast<UAnimationGraph>(Graph)))
			{
				break;
			}
		}
		UAnimGraphNode_Root* Root = AnimGraph ? FBlueprintEditorUtils::GetAnimGraphRoot(AnimGraph) : nullptr;
		FProperty* IsMovingProperty = ParentClass->FindPropertyByName(TEXT("bIsMoving"));
		FProperty* ShouldWalkProperty = ParentClass->FindPropertyByName(TEXT("bShouldWalk"));
		FProperty* MovementPlayRateProperty = ParentClass->FindPropertyByName(TEXT("MovementPlayRate"));
		if (!AnimGraph || !Root || !IsMovingProperty || !ShouldWalkProperty || !MovementPlayRateProperty)
		{
			OutResult.Errors.Add(FString::Printf(TEXT("Could not build animation graph for %s."), *BlueprintName));
			return false;
		}

		const TArray<TObjectPtr<UEdGraphNode>> ExistingNodes = AnimGraph->Nodes;
		for (UEdGraphNode* ExistingNode : ExistingNodes)
		{
			if (ExistingNode && ExistingNode != Root)
			{
				ExistingNode->DestroyNode();
			}
		}
		Root->BreakAllNodeLinks();

		UAnimGraphNode_SequencePlayer* IdleNode = AddGraphNode<UAnimGraphNode_SequencePlayer>(AnimGraph, -700, -150);
		IdleNode->SetAnimationAsset(Idle);
		IdleNode->ReconstructNode();
		UAnimGraphNode_SequencePlayer* WalkNode = AddGraphNode<UAnimGraphNode_SequencePlayer>(AnimGraph, -700, 150);
		WalkNode->SetAnimationAsset(Walk);
		WalkNode->ReconstructNode();
		UAnimGraphNode_SequencePlayer* RunNode = AddGraphNode<UAnimGraphNode_SequencePlayer>(AnimGraph, -700, 450);
		RunNode->SetAnimationAsset(Run);
		RunNode->ReconstructNode();

		FGraphNodeCreator<UAnimGraphNode_BlendListByBool> WalkBlendCreator(*AnimGraph);
		UAnimGraphNode_BlendListByBool* WalkBlendNode = WalkBlendCreator.CreateNode();
		WalkBlendNode->NodePosX = -350;
		WalkBlendNode->NodePosY = 300;
		WalkBlendCreator.Finalize();
		SetBlendTimes(WalkBlendNode, 0.2f);

		FGraphNodeCreator<UAnimGraphNode_BlendListByBool> MovingBlendCreator(*AnimGraph);
		UAnimGraphNode_BlendListByBool* MovingBlendNode = MovingBlendCreator.CreateNode();
		MovingBlendNode->NodePosX = -50;
		MovingBlendNode->NodePosY = 0;
		MovingBlendCreator.Finalize();
		SetBlendTimes(MovingBlendNode, 0.2f);

		UAnimGraphNode_Slot* SlotNode = AddGraphNode<UAnimGraphNode_Slot>(AnimGraph, 250, 0);
		SlotNode->Node.SlotName = TEXT("DefaultSlot");
		SlotNode->ReconstructNode();

		FGraphNodeCreator<UK2Node_VariableGet> VariableCreator(*AnimGraph);
		UK2Node_VariableGet* MovingNode = VariableCreator.CreateNode();
		MovingNode->SetFromProperty(IsMovingProperty, true, ParentClass);
		MovingNode->NodePosX = -700;
		MovingNode->NodePosY = 700;
		VariableCreator.Finalize();

		FGraphNodeCreator<UK2Node_VariableGet> WalkVariableCreator(*AnimGraph);
		UK2Node_VariableGet* ShouldWalkNode = WalkVariableCreator.CreateNode();
		ShouldWalkNode->SetFromProperty(ShouldWalkProperty, true, ParentClass);
		ShouldWalkNode->NodePosX = -700;
		ShouldWalkNode->NodePosY = 800;
		WalkVariableCreator.Finalize();

		FGraphNodeCreator<UK2Node_VariableGet> PlayRateCreator(*AnimGraph);
		UK2Node_VariableGet* PlayRateNode = PlayRateCreator.CreateNode();
		PlayRateNode->SetFromProperty(MovementPlayRateProperty, true, ParentClass);
		PlayRateNode->NodePosX = -700;
		PlayRateNode->NodePosY = 900;
		PlayRateCreator.Finalize();

		UEdGraphPin* WalkPlayRateInput = ExposeSequencePlayerPlayRatePin(WalkNode);
		UEdGraphPin* RunPlayRateInput = ExposeSequencePlayerPlayRatePin(RunNode);

		const UEdGraphSchema* Schema = AnimGraph->GetSchema();
		UEdGraphPin* IdleOutput = FindPosePin(IdleNode, EGPD_Output);
		UEdGraphPin* WalkOutput = FindPosePin(WalkNode, EGPD_Output);
		UEdGraphPin* RunOutput = FindPosePin(RunNode, EGPD_Output);
		UEdGraphPin* WalkInput = FindPosePin(WalkBlendNode, EGPD_Input, 0);
		UEdGraphPin* RunInput = FindPosePin(WalkBlendNode, EGPD_Input, 1);
		UEdGraphPin* WalkBlendOutput = FindPosePin(WalkBlendNode, EGPD_Output);
		UEdGraphPin* LocomotionInput = FindPosePin(MovingBlendNode, EGPD_Input, 0);
		UEdGraphPin* IdleInput = FindPosePin(MovingBlendNode, EGPD_Input, 1);
		UEdGraphPin* MovingBlendOutput = FindPosePin(MovingBlendNode, EGPD_Output);
		UEdGraphPin* SlotInput = FindPosePin(SlotNode, EGPD_Input);
		UEdGraphPin* SlotOutput = FindPosePin(SlotNode, EGPD_Output);
		UEdGraphPin* RootInput = FindPosePin(Root, EGPD_Input);
		UEdGraphPin* ShouldWalkInput = FindBooleanInputPin(WalkBlendNode);
		UEdGraphPin* ActiveInput = FindBooleanInputPin(MovingBlendNode);
		if (!Schema || !IdleOutput || !WalkOutput || !RunOutput || !WalkInput || !RunInput ||
			!WalkBlendOutput || !LocomotionInput || !IdleInput || !MovingBlendOutput || !SlotInput || !SlotOutput || !RootInput ||
			!ShouldWalkInput || !ActiveInput || !MovingNode->GetValuePin() || !ShouldWalkNode->GetValuePin() ||
			!PlayRateNode->GetValuePin() || !WalkPlayRateInput || !RunPlayRateInput ||
			!Schema->TryCreateConnection(WalkOutput, WalkInput) ||
			!Schema->TryCreateConnection(RunOutput, RunInput) ||
			!Schema->TryCreateConnection(ShouldWalkNode->GetValuePin(), ShouldWalkInput) ||
			!Schema->TryCreateConnection(PlayRateNode->GetValuePin(), WalkPlayRateInput) ||
			!Schema->TryCreateConnection(PlayRateNode->GetValuePin(), RunPlayRateInput) ||
			!Schema->TryCreateConnection(WalkBlendOutput, LocomotionInput) ||
			!Schema->TryCreateConnection(IdleOutput, IdleInput) ||
			!Schema->TryCreateConnection(MovingNode->GetValuePin(), ActiveInput) ||
			!Schema->TryCreateConnection(MovingBlendOutput, SlotInput) ||
			!Schema->TryCreateConnection(SlotOutput, RootInput))
		{
			OutResult.Errors.Add(FString::Printf(TEXT("Could not connect animation graph for %s."), *BlueprintName));
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		OutPackages.AddUnique(Blueprint->GetOutermost());
		if (bUpdatingExisting)
		{
			OutResult.BlueprintsUpdated++;
		}
		else
		{
			OutResult.BlueprintsCreated++;
		}
		return true;
	}

	struct FImportedAnimationBinding
	{
		FName AnimationSet;
		FName Action;
		int32 VariantOrder = 0;
		FString AssetName;
		TObjectPtr<UAnimSequence> Sequence;
	};

	void ParseActionAndVariant(const FString& InName, FName& OutAction, int32& OutVariantOrder)
	{
		FString ActionName = InName;
		OutVariantOrder = 0;
		int32 SeparatorIndex = INDEX_NONE;
		if (ActionName.FindLastChar(TEXT('_'), SeparatorIndex))
		{
			const FString Suffix = ActionName.Mid(SeparatorIndex + 1);
			if (!Suffix.IsEmpty() && Suffix.IsNumeric())
			{
				OutVariantOrder = FCString::Atoi(*Suffix);
				ActionName.LeftInline(SeparatorIndex);
			}
		}
		ActionName.ToLowerInline();
		OutAction = FName(*ActionName);
	}

	int32 PopulateAnimationAssets(
		const FCharacterProfile& Profile,
		USkeletalMesh* Mesh,
		UMT2CharacterAnimInstance* Defaults,
		const FString& SourceRoot,
		TArray<UPackage*>& OutPackages)
	{
		Defaults->Modify();
		Defaults->DefaultAnimationSet = TEXT("general");
		Defaults->AnimationSets.Reset();

		const FString RootPath = UMT2PathSettings::Format(TEXT("ymir_work_Name_Name"), TEXT("%s%s"), Profile.SourceFolder, *GetRaceLower(Profile));
		TArray<FAssetData> Assets;
		TArray<FImportedAnimationBinding> Bindings;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
			.Get().GetAssetsByPath(FName(*RootPath), Assets, true, false);
		for (const FAssetData& Asset : Assets)
		{
			UAnimSequence* Sequence = Cast<UAnimSequence>(Asset.GetAsset());
			if (!Sequence || Sequence->GetSkeleton() != Mesh->GetSkeleton())
			{
				continue;
			}

			FString RelativeFolder = Asset.PackagePath.ToString();
			RelativeFolder.RemoveFromStart(RootPath + TEXT("/"));
			const FString RelativeSourceFolder = RelativeFolder;
			RelativeFolder.ReplaceInline(TEXT("/"), TEXT("."));
			RelativeFolder.ToLowerInline();
			if (RelativeFolder.IsEmpty())
			{
				continue;
			}
			FString MotionName = Asset.AssetName.ToString();
			MotionName.RemoveFromStart(TEXT("A_"));
			ApplySourceMotionMetadata(SourceRoot, Profile, RelativeSourceFolder, MotionName, Sequence);

			FImportedAnimationBinding& Binding = Bindings.AddDefaulted_GetRef();
			Binding.AnimationSet = FName(*RelativeFolder);
			ParseActionAndVariant(MotionName, Binding.Action, Binding.VariantOrder);
			Binding.AssetName = Asset.AssetName.ToString();
			Binding.Sequence = Sequence;

			if (Binding.Action == TEXT("attack") || Binding.Action == TEXT("combo"))
			{
				// Horizontal/rotational root data is sanitized while animated Z stays in the pose for
				// foot contact. Pawn translation comes from .msa Accumulation via CharacterMovement.
				Sequence->Modify();
				SanitizeAttackRootTrack(Sequence);
				Sequence->bEnableRootMotion = false;
				Sequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
				Sequence->bForceRootLock = false;
				Sequence->MarkPackageDirty();
				OutPackages.AddUnique(Sequence->GetOutermost());
			}
		}

		Bindings.Sort([](const FImportedAnimationBinding& A, const FImportedAnimationBinding& B)
		{
			if (A.AnimationSet != B.AnimationSet) return A.AnimationSet.LexicalLess(B.AnimationSet);
			if (A.Action != B.Action) return A.Action.LexicalLess(B.Action);
			if (A.VariantOrder != B.VariantOrder) return A.VariantOrder < B.VariantOrder;
			return A.AssetName < B.AssetName;
		});
		for (const FImportedAnimationBinding& Binding : Bindings)
		{
			Defaults->AnimationSets.FindOrAdd(Binding.AnimationSet).Actions
				.FindOrAdd(Binding.Action).Animations.AddUnique(Binding.Sequence);
		}
		Defaults->SetAnimationSet(Defaults->DefaultAnimationSet);
		return Bindings.Num();
	}

	bool CreateDerivedAnimationBlueprint(
		const FCharacterProfile& Profile,
		UAnimBlueprint* BaseBlueprint,
		const FString& SourceRoot,
		FMT2CharacterAnimationGenerationResult& OutResult,
		TArray<UPackage*>& OutPackages)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *BuildMeshPath(Profile));
		if (!Mesh || !Mesh->GetSkeleton() || !ResolveAnimation(Profile, TEXT("wait")) ||
			!ResolveAnimation(Profile, TEXT("walk")) || !ResolveAnimation(Profile, TEXT("run")))
		{
			OutResult.Errors.Add(FString::Printf(
				TEXT("Missing mesh, wait, walk, or run animation for %s %s."), Profile.Race, Profile.Sex));
			return false;
		}

		bool bCreated = false;
		UAnimBlueprint* Blueprint = MT2AnimBlueprintBuilder::CreateOrUpdateChild(
			UMT2PathSettings::Path(TEXT("CharacterAnimationRoot")), BuildBlueprintName(Profile), BaseBlueprint, Mesh,
			OutResult.Errors, bCreated);
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			return false;
		}
		UMT2CharacterAnimInstance* Defaults =
			Cast<UMT2CharacterAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject());
		if (!Defaults)
		{
			OutResult.Errors.Add(FString::Printf(TEXT("Could not configure %s defaults."), *Blueprint->GetName()));
			return false;
		}
		OutResult.AnimationBindings += PopulateAnimationAssets(Profile, Mesh, Defaults, SourceRoot, OutPackages);
		Blueprint->MarkPackageDirty();
		OutPackages.AddUnique(Blueprint->GetOutermost());
		bCreated ? ++OutResult.BlueprintsCreated : ++OutResult.BlueprintsUpdated;
		return true;
	}
}

FString FMT2CharacterAnimationGenerationResult::BuildSummary() const
{
	return FString::Printf(
		TEXT("Imported animations: %d\nBound animation sequences: %d\nCreated AnimBPs: %d\nUpdated AnimBPs: %d\nErrors: %d"),
		AnimationsImported, AnimationBindings, BlueprintsCreated, BlueprintsUpdated, Errors.Num());
}

bool FMT2CharacterAnimationGenerator::BindFishingAnimations(FMT2CharacterAnimationGenerationResult& OutResult)
{
	auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	TArray<UPackage*> Packages;
	for (const FCharacterProfile& Profile : CharacterProfiles)
	{
		const FString Name = BuildBlueprintName(Profile);
		const FString Path = UMT2PathSettings::Format(TEXT("Characters_Animations_Name"), TEXT("%s%s"), *Name, *Name);
		auto* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *Path);
		auto* Defaults = Blueprint && Blueprint->GeneratedClass ? Cast<UMT2CharacterAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!Defaults) { OutResult.Errors.Add(FString::Printf(TEXT("Missing AnimBP %s"), *Name)); continue; }
		const FString Folder = UMT2PathSettings::Format(TEXT("ymir_work_Name_Name"), TEXT("%s%s"), Profile.SourceFolder, *GetRaceLower(Profile)) /
			UMT2PathSettings::Path(TEXT("Part_fishing"));
		TArray<FAssetData> Assets; Registry.GetAssetsByPath(FName(*Folder), Assets, false, false);
		FMT2AnimationActionSet Fishing;
		for (const FAssetData& Asset : Assets)
		{
			auto* Sequence = Cast<UAnimSequence>(Asset.GetAsset());
			if (!Sequence || Sequence->GetSkeleton() != Blueprint->TargetSkeleton) { continue; }
			FString Motion = Asset.AssetName.ToString(); Motion.RemoveFromStart(TEXT("A_"));
			FName Action; int32 Variant; ParseActionAndVariant(Motion, Action, Variant);
			Fishing.Actions.FindOrAdd(Action).Animations.AddUnique(Sequence);
		}
		bool Valid = true;
		for (const TCHAR* Action : {TEXT("wait"), TEXT("walk"), TEXT("run"), TEXT("throw"), TEXT("fishing_wait"), TEXT("fishing_react"), TEXT("fishing_catch"), TEXT("fishing_fail"), TEXT("fishing_cancel")})
		{
			if (!Fishing.Actions.Contains(Action)) { OutResult.Errors.Add(FString::Printf(TEXT("%s lacks fishing action %s"), *Name, Action)); Valid = false; }
		}
		if (!Valid) { continue; }
		Defaults->Modify(); Defaults->AnimationSets.Add(TEXT("fishing"), MoveTemp(Fishing));
		Blueprint->MarkPackageDirty(); Packages.AddUnique(Blueprint->GetOutermost()); ++OutResult.BlueprintsUpdated;
	}
	if (!OutResult.Errors.IsEmpty()) { return false; }
	if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, true)) { OutResult.Errors.Add(TEXT("Could not save fishing AnimBP bindings.")); }
	return OutResult.Errors.IsEmpty();
}

bool FMT2CharacterAnimationGenerator::Generate(
	const FString& SourceRoot,
	bool bImportMissingAnimations,
	FMT2CharacterAnimationGenerationResult& OutResult)
{
	FScopedSlowTask Progress(10.0f, NSLOCTEXT("MT2CharacterAnimationGenerator", "Progress", "Generating player animations..."));
	Progress.MakeDialog(true);
	TArray<UPackage*> PackagesToSave;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().SearchAllAssets(true);

	Progress.EnterProgressFrame(2.0f, NSLOCTEXT("MT2CharacterAnimationGenerator", "Import", "Importing missing base animations..."));
	if (bImportMissingAnimations)
	{
		ImportMissingBaseAnimations(SourceRoot, OutResult, PackagesToSave);
	}

	UClass* ParentClass = LoadClass<UAnimInstance>(nullptr, TEXT("/Script/Metin2.MT2CharacterAnimInstance"));
	if (!ParentClass)
	{
		OutResult.Errors.Add(TEXT("Could not load UMT2CharacterAnimInstance."));
		return false;
	}
	bool bBaseCreated = false;
	UAnimBlueprint* BaseBlueprint = MT2AnimBlueprintBuilder::CreateOrUpdateBase(
		UMT2PathSettings::Path(TEXT("CharacterAnimationRoot")), TEXT("ABP_MT2CharacterBase"), ParentClass,
		OutResult.Errors, bBaseCreated);
	if (!BaseBlueprint)
	{
		return false;
	}
	PackagesToSave.AddUnique(BaseBlueprint->GetOutermost());
	bBaseCreated ? ++OutResult.BlueprintsCreated : ++OutResult.BlueprintsUpdated;

	for (const FCharacterProfile& Profile : CharacterProfiles)
	{
		Progress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("MT2CharacterAnimationGenerator", "Profile", "Generating {0} {1}..."),
			FText::FromString(Profile.Race), FText::FromString(Profile.Sex)));
		if (Progress.ShouldCancel())
		{
			OutResult.Errors.Add(TEXT("Generation cancelled."));
			break;
		}
		CreateDerivedAnimationBlueprint(Profile, BaseBlueprint, SourceRoot, OutResult, PackagesToSave);
	}

	if (!PackagesToSave.IsEmpty())
	{
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	}
	return OutResult.Errors.IsEmpty();
}

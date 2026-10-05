/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2AnimBlueprintBuilder.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimNodeBase.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"

namespace MT2AnimBlueprintBuilderPrivate
{
	template <typename T>
	T* AddNode(UEdGraph* Graph, int32 X, int32 Y)
	{
		FGraphNodeCreator<T> Creator(*Graph);
		T* Node = Creator.CreateNode();
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		return Node;
	}

	UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction, int32 Index = 0)
	{
		int32 CurrentIndex = 0;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction &&
				Pin->PinType.PinSubCategoryObject.Get() == FPoseLink::StaticStruct() && CurrentIndex++ == Index)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UEdGraphPin* FindBooleanInput(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	void SetBlendTimes(UAnimGraphNode_BlendListByBool* Node)
	{
		Node->ReconstructNode();
		FArrayProperty* Property = FindFProperty<FArrayProperty>(FAnimNode_BlendListBase::StaticStruct(), TEXT("BlendTime"));
		FFloatProperty* FloatProperty = Property ? CastField<FFloatProperty>(Property->Inner) : nullptr;
		if (!Property || !FloatProperty)
		{
			return;
		}
		FScriptArrayHelper Helper(Property, Property->ContainerPtrToValuePtr<void>(&Node->Node));
		if (Helper.Num() < 2)
		{
			Helper.AddValues(2 - Helper.Num());
		}
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			FloatProperty->SetFloatingPointPropertyValue(Helper.GetRawPtr(Index), 0.2f);
		}
	}

	UEdGraphPin* ExposeOptionalPin(UAnimGraphNode_SequencePlayer* Node, FName PropertyName)
	{
		FOptionalPinFromProperty* Optional = Node->ShowPinForProperties.FindByPredicate(
			[PropertyName](const FOptionalPinFromProperty& Value) { return Value.PropertyName == PropertyName; });
		if (Optional)
		{
			Optional->bShowPin = true;
		}
		else
		{
			Node->ShowPinForProperties.Add(FOptionalPinFromProperty(
				PropertyName, true, true, PropertyName.ToString(), FText::GetEmpty(), false, TEXT("Settings"), false));
		}
		Node->ReconstructNode();
		return Node->FindPin(PropertyName);
	}

	UK2Node_VariableGet* AddVariable(UEdGraph* Graph, UClass* OwnerClass, FName PropertyName, int32 Y)
	{
		FProperty* Property = OwnerClass ? OwnerClass->FindPropertyByName(PropertyName) : nullptr;
		if (!Property)
		{
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
		UK2Node_VariableGet* Node = Creator.CreateNode();
		Node->SetFromProperty(Property, true, OwnerClass);
		Node->NodePosX = -1050;
		Node->NodePosY = Y;
		Creator.Finalize();
		return Node;
	}

	UK2Node_CallFunction* AddFloatLiteral(UEdGraph* Graph, double Value, int32 X, int32 Y)
	{
		UFunction* Function = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralDouble));
		if (!Function)
		{
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode();
		Node->SetFromFunction(Function);
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		if (UEdGraphPin* ValuePin = Node->FindPin(TEXT("Value")))
		{
			ValuePin->DefaultValue = FString::SanitizeFloat(Value);
		}
		return Node;
	}

	TArray<UEdGraphPin*> FindBlendTimePins(UAnimGraphNode_BlendListByBool* Node)
	{
		TArray<UEdGraphPin*> Result;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinName.ToString().StartsWith(TEXT("BlendTime")))
			{
				Result.Add(Pin);
			}
		}
		return Result;
	}

	bool Connect(const UEdGraphSchema* Schema, UEdGraphPin* Output, UEdGraphPin* Input)
	{
		return Schema && Output && Input && Schema->TryCreateConnection(Output, Input);
	}

	bool BuildLocomotionGraph(UAnimBlueprint* Blueprint, UClass* NativeParentClass, TArray<FString>& OutErrors)
	{
		UAnimationGraph* Graph = nullptr;
		for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
		{
			if ((Graph = Cast<UAnimationGraph>(Candidate)))
			{
				break;
			}
		}
		UAnimGraphNode_Root* Root = Graph ? FBlueprintEditorUtils::GetAnimGraphRoot(Graph) : nullptr;
		if (!Graph || !Root)
		{
			OutErrors.Add(FString::Printf(TEXT("%s has no animation graph."), *Blueprint->GetName()));
			return false;
		}

		const TArray<TObjectPtr<UEdGraphNode>> ExistingNodes = Graph->Nodes;
		for (UEdGraphNode* ExistingNode : ExistingNodes)
		{
			if (ExistingNode && ExistingNode != Root)
			{
				ExistingNode->DestroyNode();
			}
		}
		Root->BreakAllNodeLinks();

		UAnimGraphNode_SequencePlayer* Idle = AddNode<UAnimGraphNode_SequencePlayer>(Graph, -700, -200);
		UAnimGraphNode_SequencePlayer* Walk = AddNode<UAnimGraphNode_SequencePlayer>(Graph, -700, 100);
		UAnimGraphNode_SequencePlayer* Run = AddNode<UAnimGraphNode_SequencePlayer>(Graph, -700, 400);
		UAnimGraphNode_BlendListByBool* WalkBlend = AddNode<UAnimGraphNode_BlendListByBool>(Graph, -350, 250);
		UAnimGraphNode_BlendListByBool* MoveBlend = AddNode<UAnimGraphNode_BlendListByBool>(Graph, -50, 0);
		UAnimGraphNode_Slot* Slot = AddNode<UAnimGraphNode_Slot>(Graph, 250, 0);
		SetBlendTimes(WalkBlend);
		SetBlendTimes(MoveBlend);
		Slot->Node.SlotName = TEXT("DefaultSlot");
		Slot->ReconstructNode();

		UK2Node_VariableGet* WaitAsset = AddVariable(Graph, NativeParentClass, TEXT("WaitAnimation"), -250);
		UK2Node_VariableGet* WalkAsset = AddVariable(Graph, NativeParentClass, TEXT("WalkAnimation"), 50);
		UK2Node_VariableGet* RunAsset = AddVariable(Graph, NativeParentClass, TEXT("RunAnimation"), 350);
		UK2Node_VariableGet* IsMoving = AddVariable(Graph, NativeParentClass, TEXT("bIsMoving"), 650);
		UK2Node_VariableGet* ShouldWalk = AddVariable(Graph, NativeParentClass, TEXT("bShouldWalk"), 750);
		UK2Node_VariableGet* PlayRate = AddVariable(Graph, NativeParentClass, TEXT("MovementPlayRate"), 850);
		UK2Node_CallFunction* BlendTimeLiteral = AddFloatLiteral(Graph, 0.2f, -350, 750);
		if (!WaitAsset || !WalkAsset || !RunAsset || !IsMoving || !ShouldWalk || !PlayRate || !BlendTimeLiteral)
		{
			OutErrors.Add(FString::Printf(TEXT("%s is missing required native animation properties."), *Blueprint->GetName()));
			return false;
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		UEdGraphPin* BlendTimeOutput = BlendTimeLiteral->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
		const TArray<UEdGraphPin*> WalkBlendTimes = FindBlendTimePins(WalkBlend);
		const TArray<UEdGraphPin*> MoveBlendTimes = FindBlendTimePins(MoveBlend);
		bool bBlendTimesConnected = BlendTimeOutput && WalkBlendTimes.Num() == 2 && MoveBlendTimes.Num() == 2;
		for (UEdGraphPin* BlendTimePin : WalkBlendTimes)
		{
			bBlendTimesConnected &= Connect(Schema, BlendTimeOutput, BlendTimePin);
		}
		for (UEdGraphPin* BlendTimePin : MoveBlendTimes)
		{
			bBlendTimesConnected &= Connect(Schema, BlendTimeOutput, BlendTimePin);
		}
		const bool bConnected =
			bBlendTimesConnected &&
			Connect(Schema, WaitAsset->GetValuePin(), ExposeOptionalPin(Idle, TEXT("Sequence"))) &&
			Connect(Schema, WalkAsset->GetValuePin(), ExposeOptionalPin(Walk, TEXT("Sequence"))) &&
			Connect(Schema, RunAsset->GetValuePin(), ExposeOptionalPin(Run, TEXT("Sequence"))) &&
			Connect(Schema, PlayRate->GetValuePin(), ExposeOptionalPin(Walk, TEXT("PlayRate"))) &&
			Connect(Schema, PlayRate->GetValuePin(), ExposeOptionalPin(Run, TEXT("PlayRate"))) &&
			Connect(Schema, FindPosePin(Walk, EGPD_Output), FindPosePin(WalkBlend, EGPD_Input, 0)) &&
			Connect(Schema, FindPosePin(Run, EGPD_Output), FindPosePin(WalkBlend, EGPD_Input, 1)) &&
			Connect(Schema, ShouldWalk->GetValuePin(), FindBooleanInput(WalkBlend)) &&
			Connect(Schema, FindPosePin(WalkBlend, EGPD_Output), FindPosePin(MoveBlend, EGPD_Input, 0)) &&
			Connect(Schema, FindPosePin(Idle, EGPD_Output), FindPosePin(MoveBlend, EGPD_Input, 1)) &&
			Connect(Schema, IsMoving->GetValuePin(), FindBooleanInput(MoveBlend)) &&
			Connect(Schema, FindPosePin(MoveBlend, EGPD_Output), FindPosePin(Slot, EGPD_Input)) &&
			Connect(Schema, FindPosePin(Slot, EGPD_Output), FindPosePin(Root, EGPD_Input));
		if (!bConnected)
		{
			OutErrors.Add(FString::Printf(TEXT("Could not connect locomotion graph for %s."), *Blueprint->GetName()));
			return false;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		return Blueprint->GeneratedClass != nullptr;
	}
}

using namespace MT2AnimBlueprintBuilderPrivate;

UAnimBlueprint* MT2AnimBlueprintBuilder::CreateOrUpdateBase(
	const FString& PackagePath,
	const FString& AssetName,
	UClass* NativeParentClass,
	TArray<FString>& OutErrors,
	bool& bOutCreated)
{
	bOutCreated = false;
	const FString ObjectPath = PackagePath / AssetName + TEXT(".") + AssetName;
	UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	if (!Blueprint)
	{
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->ParentClass = NativeParentClass;
		Factory->bTemplate = true;
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		Blueprint = Cast<UAnimBlueprint>(AssetTools.CreateAsset(AssetName, PackagePath, UAnimBlueprint::StaticClass(), Factory));
		bOutCreated = Blueprint != nullptr;
	}
	if (!Blueprint)
	{
		OutErrors.Add(FString::Printf(TEXT("Could not create %s."), *AssetName));
		return nullptr;
	}

	Blueprint->ParentClass = NativeParentClass;
	Blueprint->bIsTemplate = true;
	Blueprint->TargetSkeleton = nullptr;
	if (!BuildLocomotionGraph(Blueprint, NativeParentClass, OutErrors))
	{
		return nullptr;
	}
	return Blueprint;
}

UAnimBlueprint* MT2AnimBlueprintBuilder::CreateOrUpdateChild(
	const FString& PackagePath,
	const FString& AssetName,
	UAnimBlueprint* BaseBlueprint,
	USkeletalMesh* PreviewMesh,
	TArray<FString>& OutErrors,
	bool& bOutCreated)
{
	bOutCreated = false;
	if (!BaseBlueprint || !BaseBlueprint->GeneratedClass || !PreviewMesh || !PreviewMesh->GetSkeleton())
	{
		OutErrors.Add(FString::Printf(TEXT("Invalid base AnimBP or preview mesh for %s."), *AssetName));
		return nullptr;
	}

	const FString ObjectPath = PackagePath / AssetName + TEXT(".") + AssetName;
	UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath);
	if (!Blueprint)
	{
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->ParentClass = BaseBlueprint->GeneratedClass;
		Factory->TargetSkeleton = PreviewMesh->GetSkeleton();
		Factory->PreviewSkeletalMesh = PreviewMesh;
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		Blueprint = Cast<UAnimBlueprint>(AssetTools.CreateAsset(AssetName, PackagePath, UAnimBlueprint::StaticClass(), Factory));
		bOutCreated = Blueprint != nullptr;
	}
	if (!Blueprint)
	{
		OutErrors.Add(FString::Printf(TEXT("Could not create %s."), *AssetName));
		return nullptr;
	}

	Blueprint->Modify();
	Blueprint->ParentClass = BaseBlueprint->GeneratedClass;
	Blueprint->bIsTemplate = false;
	Blueprint->TargetSkeleton = PreviewMesh->GetSkeleton();
	Blueprint->SetPreviewMesh(PreviewMesh, false);
	const TArray<UEdGraph*> LocalGraphs = Blueprint->FunctionGraphs;
	for (UEdGraph* Graph : LocalGraphs)
	{
		if (Cast<UAnimationGraph>(Graph))
		{
			FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::MarkTransient);
		}
	}
	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Blueprint->MarkPackageDirty();
	return Blueprint->GeneratedClass ? Blueprint : nullptr;
}

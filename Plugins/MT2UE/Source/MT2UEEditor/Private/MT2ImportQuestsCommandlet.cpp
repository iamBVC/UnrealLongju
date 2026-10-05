/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2ImportQuestsCommandlet.h"

#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Importers/MT2QuestImporter.h"
#include "Misc/Parse.h"
#include "Quests/MT2Quest.h"
#include "Quests/MT2QuestExpression.h"
#include "Quests/MT2QuestNode.h"
#include "Quests/MT2QuestTableAsset.h"

namespace
{
	void DumpNode(const UMT2QuestNode* Node, const FString& Indent);

	// Walks whatever child nodes a node holds, wherever they live (branch bodies, choice-branch structs,
	// loop bodies). Reflection keeps this working as node types are added.
	void DumpChildNodes(const void* Container, const UStruct* Struct, const FString& Indent)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			const void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Container);

			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper Helper(ArrayProperty, ValueAddress);
				for (int32 ElementIndex = 0; ElementIndex < Helper.Num(); ++ElementIndex)
				{
					const void* Element = Helper.GetRawPtr(ElementIndex);
					if (const FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(ArrayProperty->Inner))
					{
						DumpNode(Cast<UMT2QuestNode>(Inner->GetObjectPropertyValue(Element)), Indent);
					}
					else if (const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProperty->Inner))
					{
						DumpChildNodes(Element, InnerStruct->Struct, Indent);
					}
				}
			}
			else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				DumpNode(Cast<UMT2QuestNode>(ObjectProperty->GetObjectPropertyValue(ValueAddress)), Indent);
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				DumpChildNodes(ValueAddress, StructProperty->Struct, Indent);
			}
		}
	}

	// Prints one node and everything under it.
	void DumpNode(const UMT2QuestNode* Node, const FString& Indent)
	{
		if (!Node)
		{
			return;
		}
		if (const UMT2QuestNode_Say* Say = Cast<UMT2QuestNode_Say>(Node))
		{
			UE_LOG(LogTemp, Display, TEXT("[Verify] %sSay title='%s'"), *Indent, *Say->Title.ToString());
			for (const FText& Line : Say->Lines)
			{
				UE_LOG(LogTemp, Display, TEXT("[Verify] %s  \"%s\""), *Indent, *Line.ToString());
			}
		}
		else if (const UMT2QuestNode_Select* Select = Cast<UMT2QuestNode_Select>(Node))
		{
			if (!Select->TableExpression.IsEmpty())
			{
				UE_LOG(LogTemp, Display, TEXT("[Verify] %sSelect table='%s' result='%s'"),
					*Indent, *Select->TableExpression, *Select->ResultVariable.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("[Verify] %sSelect (%d option(s))"),
					*Indent, Select->Choices.Num());
			}
			for (const FMT2QuestChoice& Choice : Select->Choices)
			{
				UE_LOG(LogTemp, Display, TEXT("[Verify] %s  option \"%s\""),
					*Indent, *Choice.Label.ToString());
			}
		}
		else if (const UMT2QuestNode_Warp* Warp = Cast<UMT2QuestNode_Warp>(Node))
		{
			UE_LOG(LogTemp, Display, TEXT("[Verify] %sWarp village=%s mapLocal=%s map='%s' x=%.0f y=%.0f xExpression='%s' yExpression='%s'"),
				*Indent, Warp->bToEmpireVillage ? TEXT("true") : TEXT("false"),
				Warp->bUsesLegacyMapLocalCoordinates ? TEXT("true") : TEXT("false"),
				*Warp->MapIndexExpression, Warp->Position.X, Warp->Position.Y,
				*Warp->PositionXExpression, *Warp->PositionYExpression);
		}
		else if (const UMT2QuestNode_OpenShop* Shop = Cast<UMT2QuestNode_OpenShop>(Node))
		{
			UE_LOG(LogTemp, Display, TEXT("[Verify] %sOpenShop continueQuest=%s"),
				*Indent, Shop->bContinueQuest ? TEXT("true") : TEXT("false"));
		}
		else if (const UMT2QuestNode_SetVariable* Variable = Cast<UMT2QuestNode_SetVariable>(Node))
		{
			UE_LOG(LogTemp, Display, TEXT("[Verify] %sSetVariable name='%s' expression='%s'"),
				*Indent, *Variable->VariableName.ToString(), *Variable->Expression);
		}
		else if (const UMT2QuestNode_AssignValues* Assignment = Cast<UMT2QuestNode_AssignValues>(Node))
		{
			UE_LOG(LogTemp, Display, TEXT("[Verify] %sAssignValues %d target(s), %d RHS expression(s)"),
				*Indent, Assignment->VariableNames.Num(), Assignment->Expressions.Num());
			for (int32 Index = 0; Index < Assignment->VariableNames.Num(); ++Index)
			{
				UE_LOG(LogTemp, Display, TEXT("[Verify] %s  target='%s' questScoped=%s"), *Indent,
					*Assignment->VariableNames[Index].ToString(),
					Assignment->QuestScopedTargets.IsValidIndex(Index) && Assignment->QuestScopedTargets[Index] ? TEXT("true") : TEXT("false"));
			}
			for (const FString& Expression : Assignment->Expressions)
			{
				UE_LOG(LogTemp, Display, TEXT("[Verify] %s  RHS='%s'"), *Indent, *Expression);
			}
		}
		else if (const UMT2QuestNode_CallFunction* Function = Cast<UMT2QuestNode_CallFunction>(Node))
		{
			UE_LOG(LogTemp, Display, TEXT("[Verify] %sCallFunction '%s' scalarResult='%s' multipleTargets=%d"),
				*Indent, *Function->FunctionName.ToString(), *Function->ResultVariable.ToString(), Function->ResultVariables.Num());
			for (int32 Index = 0; Index < Function->ResultVariables.Num(); ++Index)
			{
				UE_LOG(LogTemp, Display, TEXT("[Verify] %s  target='%s' questScoped=%s"), *Indent,
					*Function->ResultVariables[Index].ToString(),
					Function->ResultQuestScopes.IsValidIndex(Index) && Function->ResultQuestScopes[Index] ? TEXT("true") : TEXT("false"));
			}
		}
		else if (const UMT2QuestNode_Return* Return = Cast<UMT2QuestNode_Return>(Node))
		{
			UE_LOG(LogTemp, Display, TEXT("[Verify] %sReturn scalar='%s' expressions='%s'"),
				*Indent, *Return->ValueExpression, *FString::Join(Return->ValueExpressions, TEXT(", ")));
		}
		else if (const UMT2QuestNode_TableCallback* Callback = Cast<UMT2QuestNode_TableCallback>(Node))
		{
			UE_LOG(LogTemp, Display, TEXT("[Verify] %sTableCallback %s table='%s' parameters='%s,%s'"),
				*Indent, Callback->bSequence ? TEXT("foreachi") : TEXT("foreach"),
				*Callback->TableExpression, *Callback->KeyVariable.ToString(), *Callback->ValueVariable.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("[Verify] %s%s"), *Indent, *Node->GetClass()->GetName());
		}
		DumpChildNodes(Node, Node->GetClass(), Indent + TEXT("  "));
	}

	// Loads a generated quest back and prints its structure, so a conversion can be verified without
	// opening the editor (asset export data is compressed, so inspecting the file directly proves nothing).
	void DumpQuest(const FString& QuestAssetName)
	{
		const FString ObjectPath =
			FString::Printf(TEXT("/Game/Quests/%s.%s"), *QuestAssetName, *QuestAssetName);
		const UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		const UMT2Quest* Quest = (Blueprint && Blueprint->GeneratedClass)
			? Cast<UMT2Quest>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!Quest)
		{
			UE_LOG(LogTemp, Error, TEXT("[Verify] Could not load quest '%s'."), *QuestAssetName);
			return;
		}

		UE_LOG(LogTemp, Display, TEXT("[Verify] Quest '%s' (%d state(s))"),
			*Quest->GetQuestId().ToString(), Quest->States.Num());
		for (const FMT2QuestState& State : Quest->States)
		{
			UE_LOG(LogTemp, Display, TEXT("[Verify]   state '%s' (%d trigger(s))"),
				*State.StateName.ToString(), State.Triggers.Num());
			for (const FMT2QuestTrigger& Trigger : State.Triggers)
			{
				UE_LOG(LogTemp, Display, TEXT("[Verify]     trigger event=%s name='%s' vnum=%d chat='%s' nodes=%d"),
					*StaticEnum<EMT2QuestEvent>()->GetNameStringByValue(static_cast<int64>(Trigger.Event)),
					*Trigger.TriggerName.ToString(), Trigger.Vnum,
					*Trigger.ChatOption.ToString(), Trigger.Nodes.Num());
				UE_LOG(LogTemp, Display, TEXT("[Verify]       pre-dispatch gate nodes=%d conditions=%d"),
					Trigger.GatePrelude.Num(), Trigger.GateConditions.Num());
				for (const TObjectPtr<UMT2QuestNode>& Node : Trigger.GatePrelude)
				{
					DumpNode(Node.Get(), TEXT("      gate "));
				}
				for (const TObjectPtr<UMT2QuestNode>& Node : Trigger.Nodes)
				{
					DumpNode(Node.Get(), TEXT("      "));
				}
			}
		}
	}

	// Evaluates an expression against an empty context and prints the result. Only expressions that do
	// not need a player (table reads, arithmetic, mob_name) are meaningful here, which is exactly what
	// the converted Lua tables are for.
	void DumpExpression(const FString& InExpression)
	{
		// The engine's command-line parser eats trailing ')' characters, so a call like
		// mob_name(x) arrives unbalanced. Close it back up rather than requiring odd quoting.
		FString Expression = InExpression;
		int32 Unclosed = 0;
		for (const TCHAR Character : Expression)
		{
			if (Character == TEXT('(')) { ++Unclosed; }
			else if (Character == TEXT(')')) { --Unclosed; }
		}
		for (int32 Missing = 0; Missing < Unclosed; ++Missing)
		{
			Expression.AppendChar(TEXT(')'));
		}

		const UMT2QuestTableAsset* Tables =
			LoadObject<UMT2QuestTableAsset>(nullptr, UMT2QuestTableAsset::GetAssetPath());
		UE_LOG(LogTemp, Display, TEXT("[VerifyExpr] table asset: %s (%d table(s))"),
			Tables ? TEXT("loaded") : TEXT("MISSING"), Tables ? Tables->Tables.Num() : 0);
		if (Tables)
		{
			for (const FMT2QuestTable& Table : Tables->Tables)
			{
				UE_LOG(LogTemp, Display, TEXT("[VerifyExpr]   %s: %d node(s), %d entries"),
					*Table.Name.ToString(), Table.Nodes.Num(),
					Table.Nodes.IsValidIndex(0) ? Table.Nodes[0].Children.Num() : 0);
			}
		}

		FMT2QuestContext Context;
		bool bOk = false;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		UE_LOG(LogTemp, Display, TEXT("[VerifyExpr] '%s' -> ok=%d text='%s' number=%f"),
			*Expression, bOk ? 1 : 0, *Value.Text, Value.Number);
	}
}

UMT2ImportQuestsCommandlet::UMT2ImportQuestsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMT2ImportQuestsCommandlet::Main(const FString& Params)
{
	FString SourceRoot =
		TEXT("D:/Giochi/Metin2/Development/my_server/server_src/share/locale/italy/quest");
	FString DestinationRoot = TEXT("/Game");
	FString ClientSourceRoot = TEXT("D:/Giochi/Metin2/Development/Dumps/my_dump");
	FParse::Value(*Params, TEXT("Source="), SourceRoot);
	FParse::Value(*Params, TEXT("Destination="), DestinationRoot);
	FParse::Value(*Params, TEXT("ClientSource="), ClientSourceRoot);

	FMT2QuestImportResult Result;
	const bool bSucceeded = FMT2QuestImporter::Import(
		SourceRoot, DestinationRoot, Result, ClientSourceRoot);

	for (const FString& Warning : Result.Warnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Warning);
	}
	for (const FString& ImportError : Result.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("%s"), *ImportError);
	}
	UE_LOG(LogTemp, Display, TEXT("%s"), *Result.BuildSummary());

	// The importer saves the generated packages itself (UEditorLoadingAndSavingUtils::SavePackages).

	// -Verify=BP_Quest_x loads a generated quest back and prints its structure.
	FString VerifyQuest;
	if (FParse::Value(*Params, TEXT("Verify="), VerifyQuest) && !VerifyQuest.IsEmpty())
	{
		DumpQuest(VerifyQuest);
	}

	// -VerifyExpr="..." evaluates one expression, so table reads can be checked without running PIE.
	FString VerifyExpression;
	if (FParse::Value(*Params, TEXT("VerifyExpr="), VerifyExpression) && !VerifyExpression.IsEmpty())
	{
		DumpExpression(VerifyExpression);
	}
	return bSucceeded ? 0 : 1;
}

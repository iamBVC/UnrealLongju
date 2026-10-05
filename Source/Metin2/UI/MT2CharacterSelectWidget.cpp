/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2CharacterSelectWidget.h"

#include "Authentication/MT2ClientSessionSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Player/MT2PlayerController.h"
#include "UI/MT2CharacterPreviewActor.h"

void UMT2CharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (SlotButton1) SlotButton1->OnClicked.AddUniqueDynamic(this, &UMT2CharacterSelectWidget::HandleSlotOneClicked);
	if (SlotButton2) SlotButton2->OnClicked.AddUniqueDynamic(this, &UMT2CharacterSelectWidget::HandleSlotTwoClicked);
	if (SlotButton3) SlotButton3->OnClicked.AddUniqueDynamic(this, &UMT2CharacterSelectWidget::HandleSlotThreeClicked);
	if (SlotButton4) SlotButton4->OnClicked.AddUniqueDynamic(this, &UMT2CharacterSelectWidget::HandleSlotFourClicked);
	if (StartButton) StartButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterSelectWidget::HandleStartClicked);
	if (CreateButton) CreateButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterSelectWidget::HandleCreateClicked);

	if (AMT2PlayerController* Controller = GetMT2Controller())
	{
		Controller->OnCharacterListUpdated.AddUniqueDynamic(
			this, &UMT2CharacterSelectWidget::HandleCharacterListUpdated);
		Controller->OnTravelFailed.AddUniqueDynamic(this, &UMT2CharacterSelectWidget::HandleTravelFailed);
	}
	OnNativeVisibilityChanged.RemoveAll(this);
	OnNativeVisibilityChanged.AddUObject(
		this, &UMT2CharacterSelectWidget::HandleVisibilityChanged);
	EnsureCharacterPreview();
	RefreshCharacterList();
}

void UMT2CharacterSelectWidget::NativeDestruct()
{
	if (AMT2PlayerController* Controller = GetMT2Controller())
	{
		Controller->OnCharacterListUpdated.RemoveDynamic(
			this, &UMT2CharacterSelectWidget::HandleCharacterListUpdated);
		Controller->OnTravelFailed.RemoveDynamic(this, &UMT2CharacterSelectWidget::HandleTravelFailed);
	}
	OnNativeVisibilityChanged.RemoveAll(this);
	if (AMT2CharacterPreviewActor* PreviewActor = CharacterPreviewActor.Get())
	{
		PreviewActor->Destroy();
	}
	CharacterPreviewActor.Reset();
	if (CharacterPreviewHost)
	{
		CharacterPreviewHost->SetContent(nullptr);
	}
	CharacterPreviewImage = nullptr;
	CharacterPreviewMaterial = nullptr;
	Super::NativeDestruct();
}

AMT2PlayerController* UMT2CharacterSelectWidget::GetMT2Controller() const
{
	return Cast<AMT2PlayerController>(GetOwningPlayer());
}

void UMT2CharacterSelectWidget::RefreshCharacterList()
{
	// The session subsystem caches the last character list the gateway sent.
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UMT2ClientSessionSubsystem* Session = GameInstance->GetSubsystem<UMT2ClientSessionSubsystem>())
		{
			CachedCharacters = Session->GetCharacters();
		}
	}

	UTextBlock* SlotNames[] = {SlotName1, SlotName2, SlotName3, SlotName4};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SlotNames); ++Index)
	{
		if (!SlotNames[Index])
		{
			continue;
		}
		if (CachedCharacters.IsValidIndex(Index))
		{
			SlotNames[Index]->SetText(FText::FromString(FString::Printf(
				TEXT("%s  Lv.%d"), *CachedCharacters[Index].CharacterName, CachedCharacters[Index].Level)));
		}
		else
		{
			SlotNames[Index]->SetText(FText::FromString(TEXT("- empty -")));
		}
	}
	if (!CachedCharacters.IsValidIndex(SelectedSlot))
	{
		SelectedSlot = CachedCharacters.IsEmpty() ? INDEX_NONE : 0;
	}
	RefreshCharacterPreview();
}

void UMT2CharacterSelectWidget::HandleCharacterListUpdated(const TArray<FMT2CharacterSummary>& Characters)
{
	CachedCharacters = Characters;
	RefreshCharacterList();
}

void UMT2CharacterSelectWidget::HandleTravelFailed(bool, const FString& Error)
{
	ShowError(Error);
}

void UMT2CharacterSelectWidget::ShowError(const FString& Error)
{
	SetStatus(Error.IsEmpty() ? TEXT("Could not enter the world.") : Error);
}

void UMT2CharacterSelectWidget::HandleSlotOneClicked() { SelectSlot(0); }
void UMT2CharacterSelectWidget::HandleSlotTwoClicked() { SelectSlot(1); }
void UMT2CharacterSelectWidget::HandleSlotThreeClicked() { SelectSlot(2); }
void UMT2CharacterSelectWidget::HandleSlotFourClicked() { SelectSlot(3); }

void UMT2CharacterSelectWidget::SelectSlot(int32 SlotIndex)
{
	if (!CachedCharacters.IsValidIndex(SlotIndex))
	{
		SelectedSlot = INDEX_NONE;
		SetStatus(TEXT("Empty slot - create a character first."));
		return;
	}
	SelectedSlot = SlotIndex;
	SetStatus(FString::Printf(TEXT("Selected %s."), *CachedCharacters[SlotIndex].CharacterName));
	RefreshCharacterPreview();
}

void UMT2CharacterSelectWidget::EnsureCharacterPreview()
{
	if (!CharacterPreviewImage && CharacterPreviewHost && WidgetTree)
	{
		CharacterPreviewImage = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), TEXT("RuntimeSelectedCharacterPreviewImage"));
		CharacterPreviewImage->SetVisibility(ESlateVisibility::Collapsed);
		CharacterPreviewHost->SetContent(CharacterPreviewImage);
	}
	if (!CharacterPreviewImage || CharacterPreviewActor.IsValid() || !GetWorld())
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	CharacterPreviewActor = GetWorld()->SpawnActor<AMT2CharacterPreviewActor>(
		AMT2CharacterPreviewActor::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -600000.0f)),
		SpawnParameters);
	if (AMT2CharacterPreviewActor* PreviewActor = CharacterPreviewActor.Get())
	{
		UTextureRenderTarget2D* RenderTarget =
			PreviewActor->InitializePreview(PreviewRenderResolution);
		UMaterialInterface* CompositeMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/UI/Materials/M_MT2CharacterPreviewComposite.M_MT2CharacterPreviewComposite"));
		if (CompositeMaterial)
		{
			CharacterPreviewMaterial =
				UMaterialInstanceDynamic::Create(CompositeMaterial, this);
			CharacterPreviewMaterial->SetTextureParameterValue(
				TEXT("PreviewTexture"), RenderTarget);
			CharacterPreviewImage->SetBrushFromMaterial(CharacterPreviewMaterial);
		}
		else
		{
			CharacterPreviewImage->SetBrushResourceObject(RenderTarget);
		}
	}
}

void UMT2CharacterSelectWidget::RefreshCharacterPreview()
{
	EnsureCharacterPreview();
	AMT2CharacterPreviewActor* PreviewActor = CharacterPreviewActor.Get();
	const bool bHasSelection = CachedCharacters.IsValidIndex(SelectedSlot);
	if (CharacterPreviewImage)
	{
		CharacterPreviewImage->SetVisibility(
			bHasSelection ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (PreviewActor)
	{
		if (bHasSelection)
		{
			PreviewActor->ApplyCharacterSummary(CachedCharacters[SelectedSlot]);
		}
		PreviewActor->SetPreviewActive(
			bHasSelection && GetVisibility() == ESlateVisibility::Visible);
	}
}

void UMT2CharacterSelectWidget::HandleVisibilityChanged(ESlateVisibility NewVisibility)
{
	if (NewVisibility == ESlateVisibility::Visible)
	{
		RefreshCharacterPreview();
	}
	else if (AMT2CharacterPreviewActor* PreviewActor = CharacterPreviewActor.Get())
	{
		PreviewActor->SetPreviewActive(false);
	}
}

void UMT2CharacterSelectWidget::HandleStartClicked()
{
	AMT2PlayerController* Controller = GetMT2Controller();
	if (!Controller || !CachedCharacters.IsValidIndex(SelectedSlot))
	{
		SetStatus(TEXT("Select a character first."));
		return;
	}
	SetStatus(TEXT("Entering the world..."));
	Controller->SelectCharacter(CachedCharacters[SelectedSlot].CharacterId);
}

void UMT2CharacterSelectWidget::HandleCreateClicked()
{
	if (CachedCharacters.Num() >= 4)
	{
		SetStatus(TEXT("This account already has four characters."));
		return;
	}
	OnCreateRequested.Broadcast();
}

void UMT2CharacterSelectWidget::SetStatus(const FString& Message)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Message));
	}
}

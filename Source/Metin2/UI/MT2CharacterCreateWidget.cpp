/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2CharacterCreateWidget.h"
#include "Config/MT2PathSettings.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Player/MT2PlayerController.h"
#include "UI/MT2CharacterPreviewActor.h"

void UMT2CharacterCreateWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (WarriorButton) WarriorButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterCreateWidget::HandleWarriorClicked);
	if (AssassinButton) AssassinButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterCreateWidget::HandleAssassinClicked);
	if (SuraButton) SuraButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterCreateWidget::HandleSuraClicked);
	if (ShamanButton) ShamanButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterCreateWidget::HandleShamanClicked);
	if (SexButton) SexButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterCreateWidget::HandleSexClicked);
	if (StyleButton) StyleButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterCreateWidget::HandleStyleClicked);
	if (EmpireRedButton) EmpireRedButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterCreateWidget::HandleEmpireRedClicked);
	if (EmpireYellowButton) EmpireYellowButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterCreateWidget::HandleEmpireYellowClicked);
	if (EmpireBlueButton) EmpireBlueButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterCreateWidget::HandleEmpireBlueClicked);
	if (CreateButton) CreateButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterCreateWidget::HandleCreateClicked);
	if (BackButton) BackButton->OnClicked.AddUniqueDynamic(this, &UMT2CharacterCreateWidget::HandleBackClicked);

	if (AMT2PlayerController* Controller = GetMT2Controller())
	{
		Controller->OnCharacterCreationCompleted.AddUniqueDynamic(
			this, &UMT2CharacterCreateWidget::HandleCreationCompleted);
	}
	OnNativeVisibilityChanged.RemoveAll(this);
	OnNativeVisibilityChanged.AddUObject(
		this, &UMT2CharacterCreateWidget::HandleVisibilityChanged);
	RefreshSelectionText();
	EnsureCharacterPreview();
	SetStatus(FString());
}

void UMT2CharacterCreateWidget::PrepareForDisplay()
{
	NameBox->SetText(FText::GetEmpty());
	SetStatus(FString());
	RefreshSelectionText();
	EnsureCharacterPreview();
	NameBox->SetKeyboardFocus();
}

void UMT2CharacterCreateWidget::NativeDestruct()
{
	if (AMT2PlayerController* Controller = GetMT2Controller())
	{
		Controller->OnCharacterCreationCompleted.RemoveDynamic(
			this, &UMT2CharacterCreateWidget::HandleCreationCompleted);
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

AMT2PlayerController* UMT2CharacterCreateWidget::GetMT2Controller() const
{
	return Cast<AMT2PlayerController>(GetOwningPlayer());
}

void UMT2CharacterCreateWidget::HandleWarriorClicked() { PendingAppearance.Race = EMT2CharacterRace::Warrior; RefreshSelectionText(); }
void UMT2CharacterCreateWidget::HandleAssassinClicked() { PendingAppearance.Race = EMT2CharacterRace::Assassin; RefreshSelectionText(); }
void UMT2CharacterCreateWidget::HandleSuraClicked() { PendingAppearance.Race = EMT2CharacterRace::Sura; RefreshSelectionText(); }
void UMT2CharacterCreateWidget::HandleShamanClicked() { PendingAppearance.Race = EMT2CharacterRace::Shaman; RefreshSelectionText(); }

void UMT2CharacterCreateWidget::HandleSexClicked()
{
	PendingAppearance.Sex = PendingAppearance.Sex == EMT2CharacterSex::Male
		? EMT2CharacterSex::Female : EMT2CharacterSex::Male;
	RefreshSelectionText();
}

void UMT2CharacterCreateWidget::HandleStyleClicked()
{
	PendingAppearance.Style = PendingAppearance.Style == EMT2CharacterStyle::Red
		? EMT2CharacterStyle::Blue : EMT2CharacterStyle::Red;
	RefreshSelectionText();
}

void UMT2CharacterCreateWidget::HandleEmpireRedClicked() { PendingEmpire = EMT2Empire::Shinsoo; RefreshSelectionText(); }
void UMT2CharacterCreateWidget::HandleEmpireYellowClicked() { PendingEmpire = EMT2Empire::Chunjo; RefreshSelectionText(); }
void UMT2CharacterCreateWidget::HandleEmpireBlueClicked() { PendingEmpire = EMT2Empire::Jinno; RefreshSelectionText(); }

void UMT2CharacterCreateWidget::HandleCreateClicked()
{
	AMT2PlayerController* Controller = GetMT2Controller();
	const FString Name = NameBox ? NameBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (!Controller || Name.Len() < 2 || Name.Len() > MT2PlayerLimits::MaxCharacterNameLength)
	{
		SetStatus(TEXT("Character name must contain between 2 and 24 characters."));
		return;
	}
	SetStatus(TEXT("Creating character..."));
	Controller->CreateCharacter(Name, PendingAppearance, PendingEmpire);
}

void UMT2CharacterCreateWidget::HandleBackClicked()
{
	OnFinished.Broadcast();
}

void UMT2CharacterCreateWidget::HandleCreationCompleted(bool bSucceeded, const FString& Error)
{
	if (bSucceeded)
	{
		SetStatus(FString());
		OnFinished.Broadcast();
	}
	else
	{
		SetStatus(Error.IsEmpty() ? TEXT("Character creation failed.") : Error);
	}
}

void UMT2CharacterCreateWidget::RefreshSelectionText()
{
	if (SelectionText)
	{
		static const TCHAR* RaceNames[] = {TEXT("Warrior"), TEXT("Assassin"), TEXT("Sura"), TEXT("Shaman")};
		static const TCHAR* EmpireNames[] = {TEXT("None"), TEXT("Shinsoo"), TEXT("Chunjo"), TEXT("Jinno")};
		const int32 RaceIndex = FMath::Clamp(static_cast<int32>(PendingAppearance.Race), 0, 3);
		const int32 EmpireIndex = FMath::Clamp(static_cast<int32>(PendingEmpire), 0, 3);
		SelectionText->SetText(FText::FromString(FString::Printf(TEXT("%s  |  %s  |  %s  |  %s"),
			RaceNames[RaceIndex],
			PendingAppearance.Sex == EMT2CharacterSex::Male ? TEXT("Male") : TEXT("Female"),
			PendingAppearance.Style == EMT2CharacterStyle::Red ? TEXT("Red style") : TEXT("Blue style"),
			EmpireNames[EmpireIndex])));
	}
	RefreshCharacterPreview();
}

void UMT2CharacterCreateWidget::EnsureCharacterPreview()
{
	if (!CharacterPreviewImage && CharacterPreviewHost && WidgetTree)
	{
		CharacterPreviewImage = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), TEXT("RuntimeCharacterPreviewImage"));
		CharacterPreviewImage->SetVisibility(ESlateVisibility::HitTestInvisible);
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
		FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -500000.0f)),
		SpawnParameters);
	if (AMT2CharacterPreviewActor* PreviewActor = CharacterPreviewActor.Get())
	{
		UTextureRenderTarget2D* RenderTarget =
			PreviewActor->InitializePreview(PreviewRenderResolution);
		UMaterialInterface* CompositeMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			UMT2PathSettings::Path(TEXT("CharacterPreviewMaterial")));
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
	RefreshCharacterPreview();
}

void UMT2CharacterCreateWidget::RefreshCharacterPreview()
{
	EnsureCharacterPreview();
	if (AMT2CharacterPreviewActor* PreviewActor = CharacterPreviewActor.Get())
	{
		PreviewActor->ApplyAppearance(PendingAppearance);
		PreviewActor->SetPreviewActive(GetVisibility() == ESlateVisibility::Visible);
	}
}

void UMT2CharacterCreateWidget::HandleVisibilityChanged(ESlateVisibility NewVisibility)
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

void UMT2CharacterCreateWidget::SetStatus(const FString& Message)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Message));
	}
}

/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2GenerateUIBlueprintsCommandlet.h"
#include "Config/MT2PathSettings.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/RetainerBox.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Components/Throbber.h"
#include "Components/VerticalBox.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionSphereMask.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "UI/MT2BoardWidget.h"
#include "UI/MT2AtlasButton.h"
#include "UI/MT2AtlasImage.h"
#include "UI/MT2AccountRegistrationWidget.h"
#include "UI/MT2CharacterCreateWidget.h"
#include "UI/MT2CharacterSelectWidget.h"
#include "UI/MT2CharacterWindowWidget.h"
#include "UI/MT2CurrencyPanelWidget.h"
#include "UI/MT2EquipmentPanelWidget.h"
#include "UI/MT2ExperienceGaugeWidget.h"
#include "UI/MT2GameHUDWidget.h"
#include "UI/MT2InventoryGridWidget.h"
#include "UI/MT2InventorySlotWidget.h"
#include "UI/MT2InventoryWidget.h"
#include "UI/MT2ItemDropDialogWidget.h"
#include "UI/MT2LoginWidget.h"
#include "UI/MT2LoadingScreenWidget.h"
#include "UI/MT2NameplateWidget.h"
#include "UI/MT2PartyInviteDialogWidget.h"
#include "UI/MT2PartyMemberWidget.h"
#include "UI/MT2PartyPanelWidget.h"
#include "UI/MT2QuickSlotBarWidget.h"
#include "UI/MT2QuickSlotWidget.h"
#include "UI/MT2ChatWidget.h"
#include "UI/MT2ResourceGaugeWidget.h"
#include "UI/MT2RespawnWidget.h"
#include "UI/MT2SystemMenuWidget.h"
#include "UI/MT2TaskbarWidget.h"
#include "UI/MT2TargetInfoWidget.h"
#include "UI/MT2MinimapWidget.h"
#include "UI/MT2FullMapWidget.h"
#include "UI/MT2GuildWidget.h"
#include "UI/MT2GuildInviteDialogWidget.h"
#include "UI/MT2TitleBarWidget.h"
#include "UI/MT2MessengerWidget.h"
#include "UI/MT2FriendAddDialogWidget.h"
#include "Components/WrapBox.h"
#include "UI/MT2FriendRequestDialogWidget.h"
#include "UI/MT2NotificationsWidget.h"
#include "Components/MultiLineEditableTextBox.h"
#include "UI/MT2WhisperWidget.h"
#include "UI/MT2TradeWidget.h"
#include "UI/MT2UIStyle.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"

namespace
{
	const TCHAR* UIPath() { return UMT2PathSettings::Path(TEXT("UIRoot")); }

	UWidgetBlueprint* LoadBP(const TCHAR* Name)
	{
		return LoadObject<UWidgetBlueprint>(nullptr, *FString::Printf(TEXT("%s%s.%s"), UIPath(), Name, Name));
	}

	bool Prepare(UWidgetBlueprint* BP)
	{
		if (!BP)
		{
			return false;
		}

		BP->Modify();
		BP->WidgetVariableNameToGuidMap.Reset();
		if (BP->WidgetTree)
		{
			BP->WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		}
		BP->WidgetTree = NewObject<UWidgetTree>(BP, TEXT("WidgetTree"), RF_Transactional);
		return true;
	}

	template <typename T>
	T* Make(UWidgetTree* Tree, const TCHAR* Name)
	{
		T* Widget = Tree->ConstructWidget<T>(T::StaticClass(), FName(Name));
		Widget->bIsVariable = true;
		return Widget;
	}

	UWidget* MakeChild(UWidgetTree* Tree, const TCHAR* AssetName, UClass* ExpectedClass, const TCHAR* Name)
	{
		const FString ClassPath = FString::Printf(TEXT("%s%s.%s_C"), UIPath(), AssetName, AssetName);
		UClass* ChildClass = LoadClass<UUserWidget>(nullptr, *ClassPath);
		if (!ChildClass || !ChildClass->IsChildOf(ExpectedClass))
		{
			UE_LOG(LogTemp, Error, TEXT("Required child widget class is missing or invalid: %s"), *ClassPath);
			return nullptr;
		}
		UWidget* Widget = Tree->ConstructWidget<UWidget>(ChildClass, FName(Name));
		Widget->bIsVariable = true;
		return Widget;
	}

	UCanvasPanelSlot* Place(UCanvasPanel* Canvas, UWidget* Widget, FVector2D Position, FVector2D Size,
		FAnchors Anchors = FAnchors(0.0f), FVector2D Alignment = FVector2D::ZeroVector)
	{
		if (!Canvas || !Widget) return nullptr;
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget);
		Slot->SetAnchors(Anchors);
		Slot->SetAlignment(Alignment);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
		return Slot;
	}

	void SetImageTexture(UImage* Image, const TCHAR* Path, bool TileX = false, bool TileY = false)
	{
		Image->SetBrush(FMT2UIStyle::TextureBrush(FMT2UIStyle::LoadTexture(Path), TileX, TileY));
	}

	void SetImageAtlas(UImage* Image, UTexture2D* Texture, const FMT2AtlasRegion& Region)
	{
		if (UMT2AtlasImage* AtlasImage = Cast<UMT2AtlasImage>(Image))
		{
			AtlasImage->SetAtlas(Texture, FMT2AtlasRect(Region.Left, Region.Top, Region.Right - Region.Left, Region.Bottom - Region.Top));
			return;
		}
		Image->SetBrush(FMT2UIStyle::AtlasBrush(Texture, Region));
	}

	UTextBlock* MakeText(UWidgetTree* Tree, const TCHAR* Name, const FString& Value, int32 FontSize = 9)
	{
		UTextBlock* Text = Make<UTextBlock>(Tree, Name);
		Text->SetText(FText::FromString(Value));
		Text->SetJustification(ETextJustify::Center);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.86f, 0.72f, 1.0f)));
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize;
		Text->SetFont(Font);
		return Text;
	}

	UButton* MakeButton(UWidgetTree* Tree, const TCHAR* Name, const FString& Label = FString())
	{
		UButton* Button = Make<UButton>(Tree, Name);
		Button->IsFocusable = false;
		if (!Label.IsEmpty())
		{
			Button->AddChild(MakeText(Tree, *FString::Printf(TEXT("%sLabel"), Name), Label));
		}
		return Button;
	}

	void SetButtonAtlas(UButton* Button, UTexture2D* Texture, const FMT2AtlasRegion& Normal,
		const FMT2AtlasRegion& Hovered, const FMT2AtlasRegion& Pressed);

	enum class EMT2GeneratedButtonSize : uint8
	{
		Small,
		Middle,
		Large,
		XLarge
	};

	UButton* MakeMT2Button(UWidgetTree* Tree, const TCHAR* Name, const FString& Label,
		EMT2GeneratedButtonSize Size)
	{
		UButton* Button = Make<UMT2AtlasButton>(Tree, Name);
		Button->IsFocusable = false;
		if (!Label.IsEmpty())
		{
			Button->AddChild(MakeText(Tree, *FString::Printf(TEXT("%sLabel"), Name), Label));
		}

		UTexture2D* Public = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
		switch (Size)
		{
		case EMT2GeneratedButtonSize::Small:
			SetButtonAtlas(Button, Public, {210,181,253,202}, {192,348,235,369}, {0,380,43,401});
			break;
		case EMT2GeneratedButtonSize::Middle:
			SetButtonAtlas(Button, Public, {194,142,255,163}, {88,181,149,202}, {149,181,210,202});
			break;
		case EMT2GeneratedButtonSize::Large:
			SetButtonAtlas(Button, Public, {155,0,243,21}, {106,142,194,163}, {0,181,88,202});
			break;
		case EMT2GeneratedButtonSize::XLarge:
			SetButtonAtlas(Button, Public, {0,31,180,56}, {0,56,180,81}, {0,81,180,106});
			break;
		}
		return Button;
	}

	void StyleMT2EditLine(UEditableTextBox* TextBox)
	{
		if (!TextBox) return;
		FSlateBrush NoDrawBrush;
		NoDrawBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
		FEditableTextBoxStyle Style = TextBox->GetWidgetStyle();
		Style.SetBackgroundImageNormal(NoDrawBrush);
		Style.SetBackgroundImageHovered(NoDrawBrush);
		Style.SetBackgroundImageFocused(NoDrawBrush);
		Style.SetBackgroundImageReadOnly(NoDrawBrush);
		FSlateFontInfo Font = Style.TextStyle.Font;
		Font.Size = 10;
		Style.SetFont(Font);
		Style.SetPadding(FMargin(3.0f, 0.0f));
		TextBox->SetWidgetStyle(Style);
		TextBox->SetForegroundColor(FLinearColor::White);
	}

	void PlaceMT2EditLine(UWidgetTree* Tree, UCanvasPanel* Root, UEditableTextBox* TextBox,
		const FVector2D& Position, const FVector2D& Size, const FAnchors& Anchors,
		const FVector2D& Alignment, bool bAddFrame = true)
	{
		if (bAddFrame)
		{
			UMT2AtlasImage* Frame = Make<UMT2AtlasImage>(Tree,
				*FString::Printf(TEXT("%sFrame"), *TextBox->GetName()));
			SetImageAtlas(Frame, FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas"))),
				FMT2AtlasRegion(0, 106, 220, 124));
			Place(Root, Frame, Position, Size, Anchors, Alignment);
		}
		StyleMT2EditLine(TextBox);
		Place(Root, TextBox, Position, Size, Anchors, Alignment);
	}

	void SetButtonAtlas(UButton* Button, UTexture2D* Texture, const FMT2AtlasRegion& Normal,
		const FMT2AtlasRegion& Hovered, const FMT2AtlasRegion& Pressed)
	{
		if (UMT2AtlasButton* AtlasButton = Cast<UMT2AtlasButton>(Button))
		{
			AtlasButton->SetAtlasRegions(Texture,
				FMT2AtlasRect(Normal.Left, Normal.Top, Normal.Right - Normal.Left, Normal.Bottom - Normal.Top),
				FMT2AtlasRect(Hovered.Left, Hovered.Top, Hovered.Right - Hovered.Left, Hovered.Bottom - Hovered.Top),
				FMT2AtlasRect(Pressed.Left, Pressed.Top, Pressed.Right - Pressed.Left, Pressed.Bottom - Pressed.Top));
			return;
		}
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal(FMT2UIStyle::AtlasBrush(Texture, Normal));
		Style.SetHovered(FMT2UIStyle::AtlasBrush(Texture, Hovered));
		Style.SetPressed(FMT2UIStyle::AtlasBrush(Texture, Pressed));
		Button->SetStyle(Style);
	}

	void SetButtonTextures(UButton* Button, const TCHAR* Normal, const TCHAR* Hovered, const TCHAR* Pressed)
	{
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal(FMT2UIStyle::TextureBrush(FMT2UIStyle::LoadTexture(Normal)));
		Style.SetHovered(FMT2UIStyle::TextureBrush(FMT2UIStyle::LoadTexture(Hovered)));
		Style.SetPressed(FMT2UIStyle::TextureBrush(FMT2UIStyle::LoadTexture(Pressed)));
		Button->SetStyle(Style);
	}

	bool CompileAndSave(UWidgetBlueprint* BP)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
		if (BP->Status == BS_Error)
		{
			UE_LOG(LogTemp, Error, TEXT("Widget Blueprint compile failed: %s"), *BP->GetPathName());
			return false;
		}

		BP->MarkPackageDirty();
		UPackage* Package = BP->GetOutermost();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, BP, *Filename, Args);
	}

	bool Begin(const TCHAR* Name, UWidgetBlueprint*& BP, UWidgetTree*& Tree)
	{
		BP = LoadBP(Name);
		if (!Prepare(BP))
		{
			UE_LOG(LogTemp, Error, TEXT("Existing Widget Blueprint not found: /Game/UI/%s"), Name);
			return false;
		}
		Tree = BP->WidgetTree;
		return true;
	}

	bool BeginOrCreate(const TCHAR* Name, UClass* ParentClass, UWidgetBlueprint*& BP, UWidgetTree*& Tree)
	{
		BP = LoadBP(Name);
		if (!BP)
		{
			const FString PackageName = FString::Printf(TEXT("%s%s"), UIPath(), Name);
			UPackage* Package = CreatePackage(*PackageName);
			BP = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				ParentClass, Package, FName(Name), BPTYPE_Normal,
				UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		}
		if (!Prepare(BP))
		{
			return false;
		}
		Tree = BP->WidgetTree;
		return true;
	}

	UMaterial* BuildMinimapCircleMaterial()
	{
		const FString PackageName = UMT2PathSettings::Path(TEXT("UI_Materials_M_MT2MinimapCircle"));
		const FString ObjectPath = PackageName + TEXT(".M_MT2MinimapCircle");
		UPackage* Package = CreatePackage(*PackageName);
		UMaterial* Material = LoadObject<UMaterial>(nullptr, *ObjectPath);
		const bool bNewMaterial = Material == nullptr;
		if (!Material)
		{
			Material = NewObject<UMaterial>(Package, TEXT("M_MT2MinimapCircle"),
				RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Material || !Material->GetEditorOnlyData()) return nullptr;

		Material->Modify();
		Material->MaterialDomain = MD_UI;
		Material->BlendMode = BLEND_Translucent;
		Material->TwoSided = true;
		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		Data->ExpressionCollection.Empty();
		Data->EmissiveColor.Expression = nullptr;
		Data->Opacity.Expression = nullptr;

		auto AddExpression = [Material, Data](UMaterialExpression* Expression, int32 X, int32 Y)
		{
			Expression->Material = Material;
			Expression->MaterialExpressionEditorX = X;
			Expression->MaterialExpressionEditorY = Y;
			Data->ExpressionCollection.AddExpression(Expression);
		};

		UMaterialExpressionTextureSampleParameter2D* Texture =
			NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
		Texture->ParameterName = TEXT("Texture");
		Texture->Texture = LoadObject<UTexture>(nullptr,
			UMT2PathSettings::Path(TEXT("Engine_EngineResources_WhiteSquareTexture")));
		Texture->SamplerType = SAMPLERTYPE_Color;
		AddExpression(Texture, -500, -100);

		UMaterialExpressionTextureCoordinate* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material);
		AddExpression(UV, -500, 160);
		UMaterialExpressionConstant2Vector* Center = NewObject<UMaterialExpressionConstant2Vector>(Material);
		Center->R = 0.5f;
		Center->G = 0.5f;
		AddExpression(Center, -500, 260);
		UMaterialExpressionSphereMask* Circle = NewObject<UMaterialExpressionSphereMask>(Material);
		Circle->A.Expression = UV;
		Circle->B.Expression = Center;
		Circle->AttenuationRadius = 0.5f;
		Circle->HardnessPercent = 100.0f;
		AddExpression(Circle, -250, 180);

		UMaterialExpressionMultiply* Alpha = NewObject<UMaterialExpressionMultiply>(Material);
		Alpha->A.Expression = Texture;
		Alpha->A.OutputIndex = 4;
		Alpha->A.Mask = 1;
		Alpha->A.MaskA = 1;
		Alpha->B.Expression = Circle;
		AddExpression(Alpha, -30, 80);
		Data->EmissiveColor.Expression = Texture;
		Data->Opacity.Expression = Alpha;

		Material->PostEditChange();
		Package->MarkPackageDirty();
		if (bNewMaterial) FAssetRegistryModule::AssetCreated(Material);
		const FString Filename = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		UPackage::SavePackage(Package, Material, *Filename, Args);
		return Material;
	}

	bool BuildBoard()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2Board"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("BoardRoot"));
		Tree->RootWidget = Root;
		UImage* Base = Make<UImage>(Tree, TEXT("BoardBase")); SetImageTexture(Base, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_base")), true, true);
		UImage* Top = Make<UImage>(Tree, TEXT("BoardTop")); SetImageTexture(Top, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_line_top")), true, false);
		UImage* Bottom = Make<UImage>(Tree, TEXT("BoardBottom")); SetImageTexture(Bottom, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_line_bottom")), true, false);
		UImage* Left = Make<UImage>(Tree, TEXT("BoardLeft")); SetImageTexture(Left, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_line_left")), false, true);
		UImage* Right = Make<UImage>(Tree, TEXT("BoardRight")); SetImageTexture(Right, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_line_right")), false, true);
		UImage* LT = Make<UImage>(Tree, TEXT("BoardLT")); SetImageTexture(LT, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_corner_lefttop")));
		UImage* RT = Make<UImage>(Tree, TEXT("BoardRT")); SetImageTexture(RT, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_corner_righttop")));
		UImage* LB = Make<UImage>(Tree, TEXT("BoardLB")); SetImageTexture(LB, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_corner_leftbottom")));
		UImage* RB = Make<UImage>(Tree, TEXT("BoardRB")); SetImageTexture(RB, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_corner_rightbottom")));
		Place(Root, Base, FVector2D::ZeroVector, FVector2D::ZeroVector, FAnchors(0,0,1,1))->SetOffsets(FMargin(0));
		Place(Root, Top, FVector2D::ZeroVector, FVector2D::ZeroVector, FAnchors(0,0,1,0))->SetOffsets(FMargin(32,0,32,32));
		Place(Root, Bottom, FVector2D::ZeroVector, FVector2D::ZeroVector, FAnchors(0,1,1,1))->SetOffsets(FMargin(32,-32,32,32));
		Place(Root, Left, FVector2D::ZeroVector, FVector2D::ZeroVector, FAnchors(0,0,0,1))->SetOffsets(FMargin(0,32,32,32));
		Place(Root, Right, FVector2D::ZeroVector, FVector2D::ZeroVector, FAnchors(1,0,1,1))->SetOffsets(FMargin(-32,32,32,32));
		Place(Root, LT, FVector2D(0,0), FVector2D(32)); Place(Root, RT, FVector2D(0,0), FVector2D(32), FAnchors(1,0), FVector2D(1,0));
		Place(Root, LB, FVector2D(0,0), FVector2D(32), FAnchors(0,1), FVector2D(0,1)); Place(Root, RB, FVector2D(0,0), FVector2D(32), FAnchors(1,1), FVector2D(1,1));
		return CompileAndSave(BP);
	}

	bool BuildTitleBar()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2TitleBar"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("TitleBarRoot")); Tree->RootWidget = Root;
		UImage* Left = Make<UImage>(Tree, TEXT("TitleLeft")); SetImageTexture(Left, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_titlebar_left")));
		UImage* Center = Make<UImage>(Tree, TEXT("TitleCenter")); SetImageTexture(Center, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_titlebar_center")), true, false);
		UImage* Right = Make<UImage>(Tree, TEXT("TitleRight")); SetImageTexture(Right, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_titlebar_right")));
		Place(Root, Left, FVector2D::ZeroVector, FVector2D(32,23));
		Place(Root, Center, FVector2D::ZeroVector, FVector2D::ZeroVector, FAnchors(0,0,1,0))->SetOffsets(FMargin(32,0,32,23));
		Place(Root, Right, FVector2D::ZeroVector, FVector2D(32,23), FAnchors(1,0), FVector2D(1,0));
		Place(Root, MakeText(Tree, TEXT("TitleText"), TEXT("Window"), 10), FVector2D::ZeroVector, FVector2D::ZeroVector, FAnchors(0,0,1,0))->SetOffsets(FMargin(20,2,20,18));
		UButton* Close = Make<UMT2AtlasButton>(Tree, TEXT("CloseButton"));
		SetButtonAtlas(Close, FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas"))), FMT2AtlasRegion(25,425,40,440), FMT2AtlasRegion(40,425,55,440), FMT2AtlasRegion(55,425,70,440));
		Place(Root, Close, FVector2D(-4,3), FVector2D(15), FAnchors(1,0), FVector2D(1,0));
		return CompileAndSave(BP);
	}

	bool BuildInventorySlot()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2InventorySlot"), BP, Tree)) return false;
		UOverlay* Root = Make<UOverlay>(Tree, TEXT("InventorySlotRoot")); Tree->RootWidget = Root;
		UMT2AtlasImage* Border = Make<UMT2AtlasImage>(Tree, TEXT("SlotBackground"));
		UTexture2D* Public = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
		SetImageAtlas(Border, Public, FMT2AtlasRegion(0,348,32,380));
		Root->AddChild(Border);
		UImage* Icon = Make<UImage>(Tree, TEXT("IconImage")); Icon->SetVisibility(ESlateVisibility::Hidden); Root->AddChild(Icon);
		UTextBlock* Label = MakeText(Tree, TEXT("LabelText"), TEXT(""), 7);
		if (UOverlaySlot* Slot = Root->AddChildToOverlay(Label)) { Slot->SetHorizontalAlignment(HAlign_Center); Slot->SetVerticalAlignment(VAlign_Center); }
		UTextBlock* Count = MakeText(Tree, TEXT("CountText"), TEXT(""), 8); Count->SetJustification(ETextJustify::Right);
		if (UOverlaySlot* Slot = Root->AddChildToOverlay(Count)) { Slot->SetHorizontalAlignment(HAlign_Right); Slot->SetVerticalAlignment(VAlign_Bottom); Slot->SetPadding(FMargin(0,0,2,1)); }
		UButton* HitButton = MakeButton(Tree, TEXT("HitButton"));
		FSlateBrush InvisibleBrush;
		InvisibleBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
		FButtonStyle HitStyle = HitButton->GetStyle();
		HitStyle.SetNormal(InvisibleBrush);
		HitStyle.SetHovered(InvisibleBrush);
		HitStyle.SetPressed(InvisibleBrush);
		HitStyle.SetDisabled(InvisibleBrush);
		HitButton->SetStyle(HitStyle);
		Root->AddChild(HitButton);
		return CompileAndSave(BP);
	}

	bool BuildQuickSlot()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2QuickSlot"), BP, Tree)) return false;
		UOverlay* Root = Make<UOverlay>(Tree, TEXT("QuickSlotRoot")); Tree->RootWidget = Root;
		UBorder* Background = Make<UBorder>(Tree, TEXT("SlotBackground"));
		Background->SetBrush(FMT2UIStyle::AtlasBrush(FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas"))), FMT2AtlasRegion(0,348,32,380)));
		Root->AddChild(Background);
		UImage* Icon = Make<UImage>(Tree, TEXT("IconImage")); Icon->SetVisibility(ESlateVisibility::Hidden); Root->AddChild(Icon);
		UTextBlock* Hotkey = MakeText(Tree, TEXT("HotkeyText"), TEXT("1"), 8);
		if (UOverlaySlot* Slot = Root->AddChildToOverlay(Hotkey)) { Slot->SetHorizontalAlignment(HAlign_Left); Slot->SetVerticalAlignment(VAlign_Top); Slot->SetPadding(FMargin(3,1,0,0)); }
		UTextBlock* Count = MakeText(Tree, TEXT("CountText"), TEXT(""), 8); Count->SetJustification(ETextJustify::Right);
		if (UOverlaySlot* Slot = Root->AddChildToOverlay(Count)) { Slot->SetHorizontalAlignment(HAlign_Right); Slot->SetVerticalAlignment(VAlign_Bottom); Slot->SetPadding(FMargin(0,0,2,1)); }
		return CompileAndSave(BP);
	}

	bool BuildCurrencyPanel()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2CurrencyPanel"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("CurrencyRoot")); Tree->RootWidget = Root;
		UTexture2D* Windows = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_WindowsAtlas")));
		UTexture2D* Public = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
		UImage* ChequeIcon = Make<UMT2AtlasImage>(Tree, TEXT("ChequeIcon")); SetImageAtlas(ChequeIcon, Windows, FMT2AtlasRegion(496,317,512,333)); Place(Root, ChequeIcon, FVector2D(0,1), FVector2D(16));
		UButton* Cheque = Make<UMT2AtlasButton>(Tree, TEXT("ChequeButton")); SetButtonAtlas(Cheque, Public, FMT2AtlasRegion(0,506,35,524), FMT2AtlasRegion(0,506,35,524), FMT2AtlasRegion(0,506,35,524));
		Cheque->AddChild(MakeText(Tree, TEXT("ChequeText"), TEXT("0"), 9)); Place(Root, Cheque, FVector2D(18,0), FVector2D(35,18));
		UImage* YangIcon = Make<UMT2AtlasImage>(Tree, TEXT("YangIcon")); SetImageAtlas(YangIcon, Windows, FMT2AtlasRegion(313,135,329,151)); Place(Root, YangIcon, FVector2D(47,1), FVector2D(16));
		UButton* Yang = Make<UMT2AtlasButton>(Tree, TEXT("YangButton")); SetButtonAtlas(Yang, Public, FMT2AtlasRegion(166,478,256,496), FMT2AtlasRegion(166,478,256,496), FMT2AtlasRegion(166,478,256,496));
		Yang->AddChild(MakeText(Tree, TEXT("YangText"), TEXT("0"), 9)); Place(Root, Yang, FVector2D(65,0), FVector2D(90,18));
		return CompileAndSave(BP);
	}

	bool BuildResourceGauge()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2ResourceGauge"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("ResourceGaugeRoot")); Tree->RootWidget = Root;
		UTexture2D* Taskbar = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_TaskbarAtlas")));
		UImage* Background = Make<UMT2AtlasImage>(Tree, TEXT("GaugeBackground")); SetImageAtlas(Background, Taskbar, FMT2AtlasRegion(0,0,158,47)); Place(Root, Background, FVector2D::ZeroVector, FVector2D(158,47));
		auto AddGauge = [&](const TCHAR* Name, FVector2D Pos, FVector2D Size)
		{
			UProgressBar* Bar = Make<UProgressBar>(Tree, Name); Bar->SetPercent(1.0f); Bar->SetFillColorAndOpacity(FLinearColor::White); Place(Root, Bar, Pos, Size);
		};
		AddGauge(TEXT("HealthGauge"), FVector2D(59,14), FVector2D(95,11));
		AddGauge(TEXT("ManaGauge"), FVector2D(59,24), FVector2D(95,11));
		AddGauge(TEXT("StaminaGauge"), FVector2D(59,38), FVector2D(95,6));
		return CompileAndSave(BP);
	}

	bool BuildExperienceGauge()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2ExperienceGauge"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("ExperienceRoot")); Tree->RootWidget = Root;
		UTexture2D* Taskbar = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_TaskbarAtlas")));
		UImage* Background = Make<UMT2AtlasImage>(Tree, TEXT("ExperienceBackground")); SetImageAtlas(Background, Taskbar, FMT2AtlasRegion(158,0,263,37)); Place(Root, Background, FVector2D::ZeroVector, FVector2D(105,37));
		for (int32 Index = 0; Index < 4; ++Index)
		{
			UProgressBar* Segment = Make<UProgressBar>(Tree, *FString::Printf(TEXT("ExperienceSegment%d"), Index + 1));
			FProgressBarStyle Style = Segment->GetWidgetStyle(); Style.BackgroundImage.DrawAs = ESlateBrushDrawType::NoDrawType; Style.FillImage = FMT2UIStyle::AtlasBrush(Taskbar, FMT2AtlasRegion(487,0,506,19)); Segment->SetWidgetStyle(Style);
			Place(Root, Segment, FVector2D(5 + Index * 25,9), FVector2D(19));
		}
		return CompileAndSave(BP);
	}

	bool BuildNameplate()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2Nameplate"), UMT2NameplateWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("NameplateRoot")); Tree->RootWidget = Root;

		auto MakeNameplateText = [&](const TCHAR* Name, int32 FontSize)
		{
			UTextBlock* Text = MakeText(Tree, Name, TEXT(""), FontSize);
			Text->SetShadowOffset(FVector2D(1.0f, 1.0f));
			Text->SetShadowColorAndOpacity(FLinearColor::Black);
			FSlateFontInfo Font = Text->GetFont();
			Font.OutlineSettings.OutlineSize = 1;
			Font.OutlineSettings.OutlineColor = FLinearColor::Black;
			Text->SetFont(Font);
			Text->SetVisibility(ESlateVisibility::HitTestInvisible);
			return Text;
		};

		UHorizontalBox* GuildLine = Make<UHorizontalBox>(Tree, TEXT("GuildLine"));
		UCanvasPanelSlot* GuildSlot = Place(Root, GuildLine, FVector2D(210.0f, 0.0f), FVector2D::ZeroVector,
			FAnchors(0.0f), FVector2D(0.5f, 0.0f));
		GuildSlot->SetAutoSize(true);
		UImage* GuildMark = Make<UImage>(Tree, TEXT("GuildMarkImage")); GuildMark->SetDesiredSizeOverride(FVector2D(16.0f, 12.0f));
		GuildLine->AddChildToHorizontalBox(GuildMark)->SetPadding(FMargin(0.0f, 1.0f, 3.0f, 0.0f));
		GuildLine->AddChildToHorizontalBox(MakeNameplateText(TEXT("GuildText"), 11));

		UHorizontalBox* MainLine = Make<UHorizontalBox>(Tree, TEXT("MainLine"));
		UCanvasPanelSlot* MainSlot = Place(Root, MainLine, FVector2D(210.0f, 24.0f), FVector2D::ZeroVector,
			FAnchors(0.0f), FVector2D(0.5f, 0.0f));
		MainSlot->SetAutoSize(true);
		for (UTextBlock* Text : {
			MakeNameplateText(TEXT("LevelText"), 11),
			MakeNameplateText(TEXT("KarmaText"), 11),
			MakeNameplateText(TEXT("NameText"), 11)})
		{
			UHorizontalBoxSlot* Slot = MainLine->AddChildToHorizontalBox(Text);
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			Slot->SetPadding(FMargin(2.0f, 0.0f));
			Slot->SetVerticalAlignment(VAlign_Center);
		}
		return CompileAndSave(BP);
	}

	bool BuildInventoryGrid()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2InventoryGrid"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("InventoryGridRoot")); Tree->RootWidget = Root;
		for (int32 Index = 0; Index < 45; ++Index)
		{
			UWidget* Slot = MakeChild(Tree, TEXT("MT2InventorySlot"), UMT2InventorySlotWidget::StaticClass(), *FString::Printf(TEXT("Slot%02d"), Index));
			if (!Slot) return false;
			Place(Root, Slot, FVector2D((Index % 5) * 32, (Index / 5) * 32), FVector2D(32));
		}
		return CompileAndSave(BP);
	}

	bool BuildEquipmentPanel()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2EquipmentPanel"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("EquipmentRoot")); Tree->RootWidget = Root;
		UImage* Background = Make<UImage>(Tree, TEXT("EquipmentBackground")); SetImageTexture(Background, UMT2PathSettings::Path(TEXT("ymir_work_ui_T_equipment_bg_without_ring"))); Place(Root, Background, FVector2D::ZeroVector, FVector2D(157,223));
		struct FSlotDef { const TCHAR* Name; FVector2D Position; FVector2D Size; };
		const FSlotDef Slots[] = {
			{TEXT("ArmorSlot"),{42,40},{32,64}}, {TEXT("HeadSlot"),{42,5},{32,32}}, {TEXT("BootsSlot"),{42,148},{32,32}},
			{TEXT("WristSlot"),{78,70},{32,32}}, {TEXT("WeaponSlot"),{6,6},{32,96}}, {TEXT("EarringSlot"),{117,70},{32,32}},
			{TEXT("NecklaceSlot"),{117,38},{32,32}}, {TEXT("BraceletSlot"),{5,148},{32,32}}, {TEXT("ShoesSlot"),{78,148},{32,32}},
			{TEXT("ArrowSlot"),{117,5},{32,32}}, {TEXT("ShieldSlot"),{78,38},{32,32}}, {TEXT("BeltSlot"),{42,109},{32,32}},
			{TEXT("ExtraSlot"),{6,109},{32,32}}, {TEXT("GloveSlot"),{78,109},{32,32}}
		};
		for (const FSlotDef& Def : Slots)
		{
			UWidget* Slot = MakeChild(Tree, TEXT("MT2InventorySlot"), UMT2InventorySlotWidget::StaticClass(), Def.Name);
			if (!Slot) return false;
			Place(Root, Slot, Def.Position, Def.Size);
		}
		UButton* DSS = MakeButton(Tree, TEXT("DragonSoulButton")); SetButtonTextures(DSS, UMT2PathSettings::Path(TEXT("ymir_work_ui_dragonsoul_T_dss_inventory_button_01")), UMT2PathSettings::Path(TEXT("ymir_work_ui_dragonsoul_T_dss_inventory_button_02")), UMT2PathSettings::Path(TEXT("ymir_work_ui_dragonsoul_T_dss_inventory_button_03"))); Place(Root, DSS, FVector2D(117,148), FVector2D(15));
		UButton* Mall = MakeButton(Tree, TEXT("MallButton")); Place(Root, Mall, FVector2D(133,148), FVector2D(15));
		UButton* Costume = MakeButton(Tree, TEXT("CostumeButton")); Place(Root, Costume, FVector2D(78,5), FVector2D(32));
		Place(Root, MakeButton(Tree, TEXT("EquipmentPageOneButton"), TEXT("I")), FVector2D(86,161), FVector2D(32,19));
		Place(Root, MakeButton(Tree, TEXT("EquipmentPageTwoButton"), TEXT("II")), FVector2D(118,161), FVector2D(32,19));
		return CompileAndSave(BP);
	}

	bool BuildQuickSlotBar()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2QuickSlotBar"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("QuickBarRoot")); Tree->RootWidget = Root;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			UWidget* Slot = MakeChild(Tree, TEXT("MT2QuickSlot"), UMT2QuickSlotWidget::StaticClass(), *FString::Printf(TEXT("QuickSlot%d"), Index + 1));
			if (!Slot) return false;
			Place(Root, Slot, FVector2D(Index < 4 ? Index * 32 : 142 + (Index - 4) * 32, 3), FVector2D(32));
		}
		UTexture2D* Taskbar = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_TaskbarAtlas")));
		UButton* Chat = Make<UMT2AtlasButton>(Tree, TEXT("ChatButton")); SetButtonAtlas(Chat, Taskbar, FMT2AtlasRegion(0,159,14,194), FMT2AtlasRegion(14,159,28,194), FMT2AtlasRegion(28,159,42,194)); Place(Root, Chat, FVector2D(128,1), FVector2D(14,35));
		UButton* Previous = Make<UMT2AtlasButton>(Tree, TEXT("PreviousPageButton")); SetButtonAtlas(Previous, Taskbar, FMT2AtlasRegion(272,32,281,37), FMT2AtlasRegion(281,32,290,37), FMT2AtlasRegion(290,32,299,37)); Place(Root, Previous, FVector2D(273,9), FVector2D(9,5));
		UButton* Next = Make<UMT2AtlasButton>(Tree, TEXT("NextPageButton")); SetButtonAtlas(Next, Taskbar, FMT2AtlasRegion(487,27,496,32), FMT2AtlasRegion(496,26,505,31), FMT2AtlasRegion(263,32,272,37)); Place(Root, Next, FVector2D(273,24), FVector2D(9,5));
		UImage* PageBG = Make<UMT2AtlasImage>(Tree, TEXT("PageBackground")); SetImageAtlas(PageBG, Taskbar, FMT2AtlasRegion(487,19,496,27)); Place(Root, PageBG, FVector2D(273,15), FVector2D(9,8));
		Place(Root, MakeText(Tree, TEXT("PageText"), TEXT("1"), 7), FVector2D(274,14), FVector2D(7,10));
		return CompileAndSave(BP);
	}

	bool BuildInventory()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2Inventory"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("InventoryRoot")); Tree->RootWidget = Root;
		struct FChild { const TCHAR* Asset; UClass* Type; const TCHAR* Name; FVector2D Pos; FVector2D Size; };
		const FChild Children[] = {
			{TEXT("MT2Board"), UMT2BoardWidget::StaticClass(), TEXT("BoardWidget"), {0,0}, {176,565}},
			{TEXT("MT2TitleBar"), UMT2TitleBarWidget::StaticClass(), TEXT("TitleBarWidget"), {8,7}, {161,23}},
			{TEXT("MT2EquipmentPanel"), UMT2EquipmentPanelWidget::StaticClass(), TEXT("EquipmentPanelWidget"), {10,33}, {157,223}},
			{TEXT("MT2InventoryGrid"), UMT2InventoryGridWidget::StaticClass(), TEXT("InventoryGridWidget"), {8,246}, {160,288}},
			{TEXT("MT2CurrencyPanel"), UMT2CurrencyPanelWidget::StaticClass(), TEXT("CurrencyPanelWidget"), {10,537}, {155,18}}
		};
		for (const FChild& Child : Children)
		{
			UWidget* Widget = MakeChild(Tree, Child.Asset, Child.Type, Child.Name); if (!Widget) return false; Place(Root, Widget, Child.Pos, Child.Size);
		}
		const TCHAR* Names[] = {TEXT("InventoryPageOneButton"), TEXT("InventoryPageTwoButton"), TEXT("InventoryPageThreeButton"), TEXT("InventoryPageFourButton")};
		const TCHAR* Labels[] = {TEXT("I"), TEXT("II"), TEXT("III"), TEXT("IV")};
		for (int32 Index = 0; Index < 4; ++Index) Place(Root, MakeButton(Tree, Names[Index], Labels[Index]), FVector2D(10 + Index * 39,224), FVector2D(39,19));
		return CompileAndSave(BP);
	}

	bool BuildTaskBar()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2TaskBar"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("TaskbarRoot")); Tree->RootWidget = Root;
		UImage* Base = Make<UImage>(Tree, TEXT("TaskbarBase")); SetImageTexture(Base, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_taskbar_base")), true, false);
		// Fills the (now 42px-tall) taskbar from the left section to the right edge - right inset 0.
		UCanvasPanelSlot* BaseSlot = Place(Root, Base, FVector2D(263,0), FVector2D(0,37), FAnchors(0,0,1,1)); if (BaseSlot) BaseSlot->SetOffsets(FMargin(263,0,0,0));
		UWidget* Resources = MakeChild(Tree, TEXT("MT2ResourceGauge"), UMT2ResourceGaugeWidget::StaticClass(), TEXT("ResourceGaugeWidget")); if (!Resources) return false; Place(Root, Resources, FVector2D(0,-10), FVector2D(158,47));
		UWidget* Experience = MakeChild(Tree, TEXT("MT2ExperienceGauge"), UMT2ExperienceGaugeWidget::StaticClass(), TEXT("ExperienceGaugeWidget")); if (!Experience) return false; Place(Root, Experience, FVector2D(158,0), FVector2D(105,37));
		UWidget* Quick = MakeChild(Tree, TEXT("MT2QuickSlotBar"), UMT2QuickSlotBarWidget::StaticClass(), TEXT("QuickSlotBarWidget")); if (!Quick) return false; Place(Root, Quick, FVector2D(-86,0), FVector2D(283,37), FAnchors(0.5f,0));
		UTexture2D* Taskbar = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_TaskbarAtlas")));
		auto AtlasButton = [&](const TCHAR* Name, FVector2D Pos, FAnchors Anchors, FMT2AtlasRegion N, FMT2AtlasRegion H, FMT2AtlasRegion P)
		{
			UButton* Button = Make<UMT2AtlasButton>(Tree, Name); SetButtonAtlas(Button, Taskbar, N,H,P); Place(Root, Button, Pos, FVector2D(32), Anchors);
		};
		AtlasButton(TEXT("LeftMouseButton"), FVector2D(-128,3), FAnchors(0.5f,0), {32,127,64,159},{64,127,96,159},{96,127,128,159});
		AtlasButton(TEXT("RightMouseButton"), FVector2D(198,3), FAnchors(0.5f,0), {32,127,64,159},{64,127,96,159},{96,127,128,159});
		UButton* Money = MakeButton(Tree, TEXT("MoneyButton")); SetButtonTextures(Money, UMT2PathSettings::Path(TEXT("ymir_work_ui_game_taskbar_T_ex_gemshop_button_01")), UMT2PathSettings::Path(TEXT("ymir_work_ui_game_taskbar_T_ex_gemshop_button_02")), UMT2PathSettings::Path(TEXT("ymir_work_ui_game_taskbar_T_ex_gemshop_button_03"))); Place(Root, Money, FVector2D(-168,3), FVector2D(32), FAnchors(1,0));
		AtlasButton(TEXT("CharacterButton"), FVector2D(-134,3), FAnchors(1,0), {263,0,295,32},{295,0,327,32},{327,0,359,32});
		AtlasButton(TEXT("InventoryButton"), FVector2D(-100,3), FAnchors(1,0), {455,0,487,32},{480,47,512,79},{200,87,232,119});
		AtlasButton(TEXT("MessengerButton"), FVector2D(-66,3), FAnchors(1,0), {359,0,391,32},{391,0,423,32},{423,0,455,32});
		AtlasButton(TEXT("SystemButton"), FVector2D(-32,3), FAnchors(1,0), {320,127,352,159},{352,127,384,159},{384,127,416,159});
		return CompileAndSave(BP);
	}

	bool BuildCharacterWindow()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2CharacterWindow"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("CharacterRoot")); Tree->RootWidget = Root;
		UWidget* Board = MakeChild(Tree, TEXT("MT2Board"), UMT2BoardWidget::StaticClass(), TEXT("BoardWidget"));
		if (!Board) return false;
		Place(Root, Board, FVector2D::ZeroVector, FVector2D(253,361));

		UCanvasPanel* StatsPage = Make<UCanvasPanel>(Tree, TEXT("StatsPage"));
		UCanvasPanel* SkillsPage = Make<UCanvasPanel>(Tree, TEXT("SkillsPage"));
		UCanvasPanel* EmotionsPage = Make<UCanvasPanel>(Tree, TEXT("EmotionsPage"));
		UCanvasPanel* QuestsPage = Make<UCanvasPanel>(Tree, TEXT("QuestsPage"));
		Place(Root, StatsPage, FVector2D::ZeroVector, FVector2D(253,324));
		Place(Root, SkillsPage, FVector2D(8,30), FVector2D(237,294));
		Place(Root, EmotionsPage, FVector2D(8,30), FVector2D(237,294));
		Place(Root, QuestsPage, FVector2D(8,30), FVector2D(237,294));

		UTexture2D* Public = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
		UTexture2D* Windows = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_WindowsAtlas")));
		auto AtlasImage = [&](UCanvasPanel* Parent, const TCHAR* Name, UTexture2D* Texture,
			const FMT2AtlasRegion& Region, FVector2D Position, FVector2D Size)
		{
			UMT2AtlasImage* Image = Make<UMT2AtlasImage>(Tree, Name);
			SetImageAtlas(Image, Texture, Region);
			Place(Parent, Image, Position, Size);
			return Image;
		};
		auto ValueSlot = [&](const TCHAR* Name, const TCHAR* TextName, FVector2D Position,
			const FMT2AtlasRegion& Region, const FString& Initial)
		{
			const FVector2D Size = Region.Size();
			AtlasImage(StatsPage, Name, Public, Region, Position, Size);
			UTextBlock* Value = MakeText(Tree, TextName, Initial, 9);
			Place(StatsPage, Value, Position, Size);
			return Value;
		};
		auto HorizontalBar = [&](UCanvasPanel* Parent, const TCHAR* Prefix, float Y)
		{
			UImage* Left = Make<UImage>(Tree, *FString::Printf(TEXT("%sLeft"), Prefix));
			SetImageTexture(Left, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_horizontalbar_left")));
			Place(Parent, Left, FVector2D(15,Y), FVector2D(16,17));
			UImage* Center = Make<UImage>(Tree, *FString::Printf(TEXT("%sCenter"), Prefix));
			SetImageTexture(Center, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_horizontalbar_center")), true, false);
			Place(Parent, Center, FVector2D(31,Y), FVector2D(191,17));
			UImage* Right = Make<UImage>(Tree, *FString::Printf(TEXT("%sRight"), Prefix));
			SetImageTexture(Right, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_horizontalbar_right")));
			Place(Parent, Right, FVector2D(222,Y), FVector2D(16,17));
		};

		UImage* Face = Make<UImage>(Tree, TEXT("FaceImage"));
		SetImageTexture(Face, UMT2PathSettings::Path(TEXT("icon_face_T_warrior_m")));
		Place(StatsPage, Face, FVector2D(11,11), FVector2D(45,45));
		AtlasImage(StatsPage, TEXT("FaceFrame"), Windows, FMT2AtlasRegion(227,0,280,53), FVector2D(7,7), FVector2D(53));
		ValueSlot(TEXT("GuildNameSlot"), TEXT("GuildNameText"), FVector2D(60,34), FMT2AtlasRegion(106,163,196,181), TEXT("-"));
		ValueSlot(TEXT("CharacterNameSlot"), TEXT("CharacterNameText"), FVector2D(153,34), FMT2AtlasRegion(106,163,196,181), TEXT("Character"));

		auto HeaderBox = [&](const TCHAR* Prefix, const TCHAR* Label, const TCHAR* ValueName,
			FVector2D Position, FVector2D Size, const TCHAR* InitialValue)
		{
			AtlasImage(StatsPage, *FString::Printf(TEXT("%sBackground"), Prefix), Public,
				FMT2AtlasRegion(106,163,196,181), Position, Size);
			UTextBlock* LabelText = MakeText(Tree, *FString::Printf(TEXT("%sLabel"), Prefix), Label, 8);
			LabelText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.33f, 0.22f, 1.0f)));
			Place(StatsPage, LabelText, Position + FVector2D(2,2), FVector2D(Size.X - 4,16));
			Place(StatsPage, MakeText(Tree, ValueName, InitialValue, 10),
				Position + FVector2D(2,20), FVector2D(Size.X - 4,18));
		};
		HeaderBox(TEXT("LevelHeader"), TEXT("Level"), TEXT("LevelValueText"), FVector2D(12,61), FVector2D(37,42), TEXT("1"));
		HeaderBox(TEXT("ExperienceHeader"), TEXT("Experience"), TEXT("ExperienceValueText"), FVector2D(56,61), FVector2D(90,42), TEXT("0"));
		HeaderBox(TEXT("RequiredExperienceHeader"), TEXT("Required XP"), TEXT("RequiredExperienceValueText"), FVector2D(153,61), FVector2D(90,42), TEXT("0"));

		HorizontalBar(StatsPage, TEXT("PrimaryStatsBar"), 108.0f);
		Place(StatsPage, MakeText(Tree, TEXT("PrimaryStatsLabel"), TEXT("Primary stats"), 8), FVector2D(17,109), FVector2D(78,15));
		Place(StatsPage, MakeText(Tree, TEXT("StatPointsLabel"), TEXT("Points"), 8), FVector2D(161,109), FVector2D(50,15));
		Place(StatsPage, MakeText(Tree, TEXT("StatPointsValueText"), TEXT("0"), 9), FVector2D(207,109), FVector2D(25,16));
		const TCHAR* PrimaryLeftLabels[] = {TEXT("VIT"), TEXT("INT"), TEXT("STR"), TEXT("DEX")};
		const TCHAR* PrimaryRightLabels[] = {TEXT("HP"), TEXT("MP"), TEXT("Attack"), TEXT("Defense")};
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const float Y = 132.0f + Index * 23.0f;
			Place(StatsPage, MakeText(Tree, *FString::Printf(TEXT("PrimaryLeftLabel%d"), Index), PrimaryLeftLabels[Index], 9),
				FVector2D(18,Y), FVector2D(38,18));
			Place(StatsPage, MakeText(Tree, *FString::Printf(TEXT("PrimaryRightLabel%d"), Index), PrimaryRightLabels[Index], 8),
				FVector2D(106,Y), FVector2D(42,18));
		}

		const FMT2AtlasRegion SmallValue(121,232,160,250);
		const FMT2AtlasRegion LargeValue(106,163,196,181);
		const TCHAR* PrimaryValueNames[] = {TEXT("ConstitutionValueText"), TEXT("IntelligenceValueText"), TEXT("StrengthValueText"), TEXT("DexterityValueText")};
		const TCHAR* PrimaryButtonNames[] = {TEXT("ConstitutionPlusButton"), TEXT("IntelligencePlusButton"), TEXT("StrengthPlusButton"), TEXT("DexterityPlusButton")};
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const float Y = 132.0f + Index * 23.0f;
			ValueSlot(*FString::Printf(TEXT("PrimaryValueSlot%d"), Index), PrimaryValueNames[Index], FVector2D(56,Y), SmallValue, TEXT("0"));
			UMT2AtlasButton* Plus = Make<UMT2AtlasButton>(Tree, PrimaryButtonNames[Index]);
			SetButtonAtlas(Plus, Windows, FMT2AtlasRegion(495,135,508,148), FMT2AtlasRegion(482,135,495,148), FMT2AtlasRegion(469,135,482,148));
			Place(StatsPage, Plus, FVector2D(97,Y + 3), FVector2D(13));
		}
		ValueSlot(TEXT("HealthValueSlot"), TEXT("HealthValueText"), FVector2D(148,132), LargeValue, TEXT("0/0"));
		ValueSlot(TEXT("ManaValueSlot"), TEXT("ManaValueText"), FVector2D(148,155), LargeValue, TEXT("0/0"));
		ValueSlot(TEXT("AttackValueSlot"), TEXT("AttackValueText"), FVector2D(148,178), LargeValue, TEXT("0-0"));
		ValueSlot(TEXT("DefenseValueSlot"), TEXT("DefenseValueText"), FVector2D(148,201), LargeValue, TEXT("0"));

		HorizontalBar(StatsPage, TEXT("SecondaryStatsBar"), 229.0f);
		Place(StatsPage, MakeText(Tree, TEXT("SecondaryStatsLabel"), TEXT("Secondary stats"), 8), FVector2D(17,230), FVector2D(90,15));
		const TCHAR* SecondaryLeftLabels[] = {TEXT("Move speed"), TEXT("Attack speed"), TEXT("Magic attack")};
		const TCHAR* SecondaryRightLabels[] = {TEXT("Magic defense"), TEXT("Evasion")};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Place(StatsPage, MakeText(Tree, *FString::Printf(TEXT("SecondaryLeftLabel%d"), Index), SecondaryLeftLabels[Index], 7),
				FVector2D(13,252.0f + Index * 23.0f), FVector2D(55,18));
		}
		for (int32 Index = 0; Index < 2; ++Index)
		{
			Place(StatsPage, MakeText(Tree, *FString::Printf(TEXT("SecondaryRightLabel%d"), Index), SecondaryRightLabels[Index], 7),
				FVector2D(127,252.0f + Index * 23.0f), FVector2D(58,18));
		}
		const FMT2AtlasRegion MiddleValue(196,163,248,181);
		ValueSlot(TEXT("MovementSpeedValueSlot"), TEXT("MovementSpeedValueText"), FVector2D(68,252), MiddleValue, TEXT("100"));
		ValueSlot(TEXT("AttackSpeedValueSlot"), TEXT("AttackSpeedValueText"), FVector2D(68,275), MiddleValue, TEXT("100"));
		ValueSlot(TEXT("MagicAttackValueSlot"), TEXT("MagicAttackValueText"), FVector2D(68,298), MiddleValue, TEXT("-"));
		ValueSlot(TEXT("MagicDefenseValueSlot"), TEXT("MagicDefenseValueText"), FVector2D(185,252), MiddleValue, TEXT("-"));
		ValueSlot(TEXT("EvasionValueSlot"), TEXT("EvasionValueText"), FVector2D(185,275), MiddleValue, TEXT("-"));

		HorizontalBar(SkillsPage, TEXT("ActiveSkillsBar"), 10.0f);
		Place(SkillsPage, MakeText(Tree, TEXT("ActiveSkillsLabel"), TEXT("Active Skills"), 9), FVector2D(22,10), FVector2D(120,17));
		Place(SkillsPage, MakeText(Tree, TEXT("SkillPointsLabel"), TEXT("Skill points"), 9), FVector2D(145,10), FVector2D(65,17));
		Place(SkillsPage, MakeText(Tree, TEXT("SkillPointsValueText"), TEXT("0"), 9), FVector2D(208,10), FVector2D(22,17));
		HorizontalBar(SkillsPage, TEXT("SupportSkillsBar"), 148.0f);
		Place(SkillsPage, MakeText(Tree, TEXT("SupportSkillsLabel"), TEXT("Support Skills"), 9), FVector2D(22,148), FVector2D(120,17));
		HorizontalBar(EmotionsPage, TEXT("EmotionsBar"), 10.0f);
		Place(EmotionsPage, MakeText(Tree, TEXT("EmotionsBody"), TEXT("Emotions"), 9), FVector2D(22,10), FVector2D(120,17));
		HorizontalBar(QuestsPage, TEXT("QuestsBar"), 10.0f);
		Place(QuestsPage, MakeText(Tree, TEXT("QuestsBody"), TEXT("Quest log"), 9), FVector2D(22,10), FVector2D(120,17));

		UWidget* Title = MakeChild(Tree, TEXT("MT2TitleBar"), UMT2TitleBarWidget::StaticClass(), TEXT("TitleBarWidget"));
		if (!Title) return false;
		Place(Root, Title, FVector2D(61,7), FVector2D(185,23));
		UMT2AtlasImage* Tabs = Make<UMT2AtlasImage>(Tree, TEXT("TabBackground"));
		SetImageAtlas(Tabs, Public, FMT2AtlasRegion(106,163,196,181));
		Place(Root, Tabs, FVector2D(0,324), FVector2D(253,37));
		auto TextTab = [&](const TCHAR* Name, const TCHAR* Label, FVector2D Position, FVector2D Size)
		{
			UMT2AtlasButton* Button = Make<UMT2AtlasButton>(Tree, Name);
			SetButtonAtlas(Button, Public, FMT2AtlasRegion(194,142,255,163),
				FMT2AtlasRegion(88,181,149,202), FMT2AtlasRegion(149,181,210,202));
			Button->AddChild(MakeText(Tree, *FString::Printf(TEXT("%sLabel"), Name), Label, 8));
			Place(Root, Button, Position, Size);
		};
		TextTab(TEXT("StatsTabButton"), TEXT("Stats"), FVector2D(5,330), FVector2D(57,23));
		TextTab(TEXT("SkillsTabButton"), TEXT("Skills"), FVector2D(66,330), FVector2D(57,23));
		TextTab(TEXT("EmotionsTabButton"), TEXT("Emotions"), FVector2D(127,330), FVector2D(57,23));
		TextTab(TEXT("QuestsTabButton"), TEXT("Quests"), FVector2D(188,330), FVector2D(57,23));
		return CompileAndSave(BP);
	}

	bool BuildRespawn()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2Respawn"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("RespawnRoot")); Tree->RootWidget = Root;

		// window["width"]/["height"] in uiscript/restartdialog.py, before the (unused here) conditional
		// "restart immediately"/"give up" buttons that only appear on special zones. Everything is
		// placed directly on Root (not a nested sub-canvas - BindWidget resolution in this commandlet
		// only reliably picks up widgets parented straight to the compiled Blueprint's root panel), with
		// each element's local (window-space) position offset by -DialogSize/2 so the 200x88 panel ends
		// up centered on screen via a single shared anchor/alignment of (0.5,0.5).
		constexpr float DialogWidth = 200.0f;
		constexpr float DialogHeight = 88.0f;
		constexpr float FrameTile = 16.0f;
		const FVector2D Center(-DialogWidth * 0.5f, -DialogHeight * 0.5f);
		const FAnchors ScreenCenter(0.5f);

		// window["children"][0] "board" (type "thinboard") r/g/b/a tint in restartdialog.py.
		const FLinearColor ThinBoardTint(0.3333f, 0.2941f, 0.2588f, 1.0f);
		auto Corner = [&](const TCHAR* Name, const TCHAR* Path, FVector2D LocalPos)
		{
			UImage* Image = Make<UImage>(Tree, Name); SetImageTexture(Image, Path); Image->SetColorAndOpacity(ThinBoardTint);
			Place(Root, Image, Center + LocalPos, FVector2D(FrameTile), ScreenCenter);
		};
		auto EdgeH = [&](const TCHAR* Name, const TCHAR* Path, float LocalY)
		{
			UImage* Image = Make<UImage>(Tree, Name); SetImageTexture(Image, Path, true, false); Image->SetColorAndOpacity(ThinBoardTint);
			Place(Root, Image, Center + FVector2D(FrameTile, LocalY), FVector2D(DialogWidth - FrameTile * 2.0f, FrameTile), ScreenCenter);
		};
		auto EdgeV = [&](const TCHAR* Name, const TCHAR* Path, float LocalX)
		{
			UImage* Image = Make<UImage>(Tree, Name); SetImageTexture(Image, Path, false, true); Image->SetColorAndOpacity(ThinBoardTint);
			Place(Root, Image, Center + FVector2D(LocalX, FrameTile), FVector2D(FrameTile, DialogHeight - FrameTile * 2.0f), ScreenCenter);
		};
		Corner(TEXT("FrameLT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_lefttop")), FVector2D(0.0f, 0.0f));
		Corner(TEXT("FrameRT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_righttop")), FVector2D(DialogWidth - FrameTile, 0.0f));
		Corner(TEXT("FrameLB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_leftbottom")), FVector2D(0.0f, DialogHeight - FrameTile));
		Corner(TEXT("FrameRB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_rightbottom")), FVector2D(DialogWidth - FrameTile, DialogHeight - FrameTile));
		EdgeH(TEXT("FrameTop"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_top")), 0.0f);
		EdgeH(TEXT("FrameBottom"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_bottom")), DialogHeight - FrameTile);
		EdgeV(TEXT("FrameLeft"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_left")), 0.0f);
		EdgeV(TEXT("FrameRight"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_right")), DialogWidth - FrameTile);

		// XLarge_Button_01/02/03.sub (default/over/down) are sub-images of Public.dds - same texture
		// this project already imported as T_public. Regions come straight from the .sub files'
		// left/top/right/bottom fields.
		UTexture2D* Public = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
		auto RestartButton = [&](const TCHAR* Name, float LocalY, const FString& Label)
		{
			UButton* Button = Make<UMT2AtlasButton>(Tree, Name);
			SetButtonAtlas(Button, Public, FMT2AtlasRegion(0.0f, 31.0f, 180.0f, 56.0f),
				FMT2AtlasRegion(0.0f, 56.0f, 180.0f, 81.0f), FMT2AtlasRegion(0.0f, 81.0f, 180.0f, 106.0f));
			Button->AddChild(MakeText(Tree, *FString::Printf(TEXT("%sLabel"), Name), Label, 11));
			Place(Root, Button, Center + FVector2D(10.0f, LocalY), FVector2D(180.0f, 25.0f), ScreenCenter);
		};
		// x/y match restart_here_button / restart_town_button in restartdialog.py exactly.
		RestartButton(TEXT("RespawnHereButton"), 17.0f, TEXT("Respawn Here"));
		RestartButton(TEXT("RespawnTownButton"), 47.0f, TEXT("Respawn in Town"));

		return CompileAndSave(BP);
	}

	// The old MessengerWindow: a board_with_titlebar holding the companion list, with the icon button
	// row along the bottom (uiscript/messengerwindow.py). The original was 170x300 and whispered in a
	// separate window; this one is 500x400 because the whisper log lives beside the list.
	// uiscript/whisperdialog.py: a 280x200 thinboard. The name slot sits top-left, the GM mark and the
	// minimize/close buttons top-right, the log fills the middle, and the "editbar" strip along the
	// bottom holds the chat line and the send button.
	// The notification strip: a row of icon + name entries for active quests and unread whispers.
	// It has no board of its own - it sits on the HUD, not in a window - and anchors bottom-left above
	// the chat, where the old client kept its quest and message reminders.
	bool BuildNotifications()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2Notifications"), UMT2NotificationsWidget::StaticClass(), BP, Tree)) return false;

		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("NotificationsRoot"));
		// The root canvas covers the viewport, so it must not take the mouse: a hit-testable one
		// swallows camera, movement and every other window's clicks. Only the entries are clickable.
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Tree->RootWidget = Root;

		// Entries are generated into this box; a WrapBox keeps a long list on screen instead of
		// running off the edge once several quests are active.
		UWrapBox* NotificationsBox = Make<UWrapBox>(Tree, TEXT("NotificationsBox"));
		NotificationsBox->SetInnerSlotPadding(FVector2D(4.0f, 4.0f));
		NotificationsBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UCanvasPanelSlot* BoxSlot = Place(Root, NotificationsBox, FVector2D(12.0f, -150.0f),
			FVector2D(520.0f, 120.0f), FAnchors(0.0f, 1.0f), FVector2D(0.0f, 1.0f));
		if (BoxSlot)
		{
			BoxSlot->SetAutoSize(true);
		}
		return CompileAndSave(BP);
	}

	// "{Name} sent you a friend request", with accept and deny. Same small centred board the party
	// invite uses, since it asks the same kind of question.
	bool BuildFriendRequestDialog()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2FriendRequestDialog"), UMT2FriendRequestDialogWidget::StaticClass(), BP, Tree)) return false;

		constexpr float DialogWidth = 300.0f;
		constexpr float DialogHeight = 110.0f;
		constexpr float FrameTile = 16.0f;
		const FLinearColor ThinBoardTint(0.3333f, 0.2941f, 0.2588f, 1.0f);
		const FAnchors ScreenCentre(0.5f);
		const FVector2D CentreAlignment(0.5f);

		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("FriendRequestRoot"));
		// Only the dialog itself takes the mouse; the rest of the screen keeps working behind it.
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Tree->RootWidget = Root;

		UCanvasPanel* Page = Make<UCanvasPanel>(Tree, TEXT("DialogPage"));
		Place(Root, Page, FVector2D::ZeroVector, FVector2D(DialogWidth, DialogHeight),
			ScreenCentre, CentreAlignment);

		UBorder* Backdrop = Make<UBorder>(Tree, TEXT("DialogBackdrop"));
		Backdrop->SetBrushColor(FLinearColor(0.03f, 0.03f, 0.04f, 0.96f));
		Place(Page, Backdrop, FVector2D(FrameTile * 0.5f, FrameTile * 0.5f),
			FVector2D(DialogWidth - FrameTile, DialogHeight - FrameTile));
		auto Frame = [&](const TCHAR* Name, const TCHAR* Path, FVector2D Pos, FVector2D Size, bool TileX, bool TileY)
		{
			UImage* Image = Make<UImage>(Tree, Name);
			SetImageTexture(Image, Path, TileX, TileY);
			Image->SetColorAndOpacity(ThinBoardTint);
			Place(Page, Image, Pos, Size);
		};
		Frame(TEXT("FrameLT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_lefttop")), FVector2D(0, 0), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameRT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_righttop")), FVector2D(DialogWidth - FrameTile, 0), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameLB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_leftbottom")), FVector2D(0, DialogHeight - FrameTile), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameRB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_rightbottom")), FVector2D(DialogWidth - FrameTile, DialogHeight - FrameTile), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameTop"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_top")), FVector2D(FrameTile, 0), FVector2D(DialogWidth - FrameTile * 2.0f, FrameTile), true, false);
		Frame(TEXT("FrameBottom"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_bottom")), FVector2D(FrameTile, DialogHeight - FrameTile), FVector2D(DialogWidth - FrameTile * 2.0f, FrameTile), true, false);
		Frame(TEXT("FrameLeft"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_left")), FVector2D(0, FrameTile), FVector2D(FrameTile, DialogHeight - FrameTile * 2.0f), false, true);
		Frame(TEXT("FrameRight"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_right")), FVector2D(DialogWidth - FrameTile, FrameTile), FVector2D(FrameTile, DialogHeight - FrameTile * 2.0f), false, true);

		UTextBlock* MessageText = MakeText(Tree, TEXT("MessageText"), TEXT("Friend request"), 10);
		MessageText->SetAutoWrapText(true);
		Place(Page, MessageText, FVector2D(16.0f, 22.0f), FVector2D(DialogWidth - 32.0f, 34.0f));

		UButton* AcceptButton = MakeMT2Button(Tree, TEXT("AcceptButton"), TEXT("Accept"), EMT2GeneratedButtonSize::Large);
		Place(Page, AcceptButton, FVector2D(30.0f, 66.0f), FVector2D(104.0f, 26.0f));
		UButton* DenyButton = MakeMT2Button(Tree, TEXT("DenyButton"), TEXT("Deny"), EMT2GeneratedButtonSize::Large);
		Place(Page, DenyButton, FVector2D(DialogWidth - 134.0f, 66.0f), FVector2D(104.0f, 26.0f));

		return CompileAndSave(BP);
	}

	bool BuildFriendAddDialog()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2FriendAddDialog"), UMT2FriendAddDialogWidget::StaticClass(), BP, Tree)) return false;

		constexpr float Width = 210.0f;
		constexpr float Height = 118.0f;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("FriendAddRoot"));
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Tree->RootWidget = Root;
		UWidget* Board = MakeChild(Tree, TEXT("MT2Board"), UMT2BoardWidget::StaticClass(), TEXT("BoardWidget"));
		if (!Board) return false;
		Place(Root, Board, FVector2D::ZeroVector, FVector2D(Width, Height));
		UTextBlock* TitleText = MakeText(Tree, TEXT("TitleText"), TEXT("Add Friend"), 10);
		Place(Root, TitleText, FVector2D(12, 12), FVector2D(Width - 24, 18));
		UEditableTextBox* NameBox = Make<UEditableTextBox>(Tree, TEXT("NameBox"));
		NameBox->SetHintText(FText::FromString(TEXT("Name")));
		PlaceMT2EditLine(Tree, Root, NameBox, FVector2D(16, 42), FVector2D(Width - 32, 22), FAnchors(0), FVector2D::ZeroVector);
		UButton* ConfirmButton = MakeMT2Button(Tree, TEXT("ConfirmButton"), TEXT("OK"), EMT2GeneratedButtonSize::Middle);
		Place(Root, ConfirmButton, FVector2D(22, 78), FVector2D(78, 22));
		UButton* CancelButton = MakeMT2Button(Tree, TEXT("CancelButton"), TEXT("Cancel"), EMT2GeneratedButtonSize::Middle);
		Place(Root, CancelButton, FVector2D(110, 78), FVector2D(78, 22));
		return CompileAndSave(BP);
	}

	bool BuildWhisper()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2Whisper"), UMT2WhisperWidget::StaticClass(), BP, Tree)) return false;

		// Exact geometry from uiscript/whisperdialog.py.
		constexpr float WindowWidth = 280.0f;
		constexpr float WindowHeight = 200.0f;
		constexpr float FrameTile = 16.0f;
		const FLinearColor ThinBoardTint(0.3333f, 0.2941f, 0.2588f, 1.0f);

		USizeBox* RootSizeBox = Make<USizeBox>(Tree, TEXT("RootSizeBox"));
		RootSizeBox->SetWidthOverride(WindowWidth);
		RootSizeBox->SetHeightOverride(WindowHeight);
		Tree->RootWidget = RootSizeBox;

		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("WhisperRoot"));
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		RootSizeBox->AddChild(Root);

		// window["children"][0] "board", type "thinboard".
		UBorder* Backdrop = Make<UBorder>(Tree, TEXT("BoardBackdrop"));
		Backdrop->SetBrushColor(FLinearColor(0.03f, 0.03f, 0.04f, 0.94f));
		Place(Root, Backdrop, FVector2D(FrameTile * 0.5f, FrameTile * 0.5f),
			FVector2D(WindowWidth - FrameTile, WindowHeight - FrameTile));
		auto Frame = [&](const TCHAR* Name, const TCHAR* Path, FVector2D Pos, FVector2D Size, bool TileX, bool TileY)
		{
			UImage* Image = Make<UImage>(Tree, Name);
			SetImageTexture(Image, Path, TileX, TileY);
			Image->SetColorAndOpacity(ThinBoardTint);
			Place(Root, Image, Pos, Size);
		};
		Frame(TEXT("FrameLT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_lefttop")), FVector2D(0, 0), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameRT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_righttop")), FVector2D(WindowWidth - FrameTile, 0), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameLB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_leftbottom")), FVector2D(0, WindowHeight - FrameTile), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameRB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_rightbottom")), FVector2D(WindowWidth - FrameTile, WindowHeight - FrameTile), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameTop"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_top")), FVector2D(FrameTile, 0), FVector2D(WindowWidth - FrameTile * 2.0f, FrameTile), true, false);
		Frame(TEXT("FrameBottom"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_bottom")), FVector2D(FrameTile, WindowHeight - FrameTile), FVector2D(WindowWidth - FrameTile * 2.0f, FrameTile), true, false);
		Frame(TEXT("FrameLeft"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_left")), FVector2D(0, FrameTile), FVector2D(FrameTile, WindowHeight - FrameTile * 2.0f), false, true);
		Frame(TEXT("FrameRight"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_right")), FVector2D(WindowWidth - FrameTile, FrameTile), FVector2D(FrameTile, WindowHeight - FrameTile * 2.0f), false, true);

		// "name_slot" at (10,10) with "titlename" inside it at (3,3).
		UTexture2D* Public = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
		UImage* NameSlot = Make<UImage>(Tree, TEXT("NameSlot"));
		SetImageAtlas(NameSlot, Public, {0, 124, 130, 142});
		Place(Root, NameSlot, FVector2D(10.0f, 10.0f), FVector2D(130.0f, 18.0f));
		UTextBlock* TitleNameText = MakeText(Tree, TEXT("TitleNameText"), FString(), 8);
		TitleNameText->SetJustification(ETextJustify::Left);
		Place(Root, TitleNameText, FVector2D(13.0f, 13.0f), FVector2D(124.0f, 16.0f));

		// "gamemastermark" at (206,6); hidden by the widget unless the companion is a GM.
		UTextBlock* GameMasterMark = MakeText(Tree, TEXT("GameMasterMark"), TEXT("GM"), 10);
		GameMasterMark->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.8f, 0.1f)));
		Place(Root, GameMasterMark, FVector2D(206.0f, 8.0f), FVector2D(26.0f, 18.0f));

		// "minimizebutton" at (280-41,12) and "closebutton" at (280-24,12).
		UMT2AtlasButton* MinimizeButton = Make<UMT2AtlasButton>(Tree, TEXT("MinimizeButton"));
		MinimizeButton->IsFocusable = false;
		SetButtonAtlas(MinimizeButton, Public, {70,425,85,440}, {85,425,100,440}, {100,425,115,440});
		Place(Root, MinimizeButton, FVector2D(239.0f, 12.0f), FVector2D(15.0f, 15.0f));
		UMT2AtlasButton* CloseButton = Make<UMT2AtlasButton>(Tree, TEXT("CloseButton"));
		CloseButton->IsFocusable = false;
		SetButtonAtlas(CloseButton, Public, {25,425,40,440}, {40,425,55,440}, {55,425,70,440});
		Place(Root, CloseButton, FVector2D(256.0f, 12.0f), FVector2D(15.0f, 15.0f));

		// The log fills the space between the title row and the edit bar; the old dialog put its
		// thin_scrollbar at x=280-25, y=35 with a size of 120.
		UScrollBox* ChatLogBox = Make<UScrollBox>(Tree, TEXT("ChatLogBox"));
		Place(Root, ChatLogBox, FVector2D(14.0f, 35.0f), FVector2D(236.0f, 100.0f));

		// "editbar": a 0x77000000 bar at (10, height-60), 262x50, holding the chatline and send button.
		UBorder* EditBar = Make<UBorder>(Tree, TEXT("EditBar"));
		EditBar->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.47f));
		Place(Root, EditBar, FVector2D(10.0f, WindowHeight - 60.0f), FVector2D(WindowWidth - 18.0f, 50.0f));

		UMultiLineEditableTextBox* ChatLine = Make<UMultiLineEditableTextBox>(Tree, TEXT("ChatLine"));
		Place(Root, ChatLine, FVector2D(15.0f, WindowHeight - 55.0f), FVector2D(180.0f, 40.0f));

		UButton* SendButton = MakeMT2Button(Tree, TEXT("SendButton"), TEXT("Send"), EMT2GeneratedButtonSize::Middle);
		Place(Root, SendButton, FVector2D(WindowWidth - 80.0f, WindowHeight - 50.0f), FVector2D(57.0f, 30.0f));

		return CompileAndSave(BP);
	}

	bool BuildTrade()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2Trade"), UMT2TradeWidget::StaticClass(), BP, Tree)) return false;
		constexpr float Width = 390.0f, Height = 190.0f;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("TradeRoot")); Tree->RootWidget = Root;
		UWidget* Board = MakeChild(Tree, TEXT("MT2Board"), UMT2BoardWidget::StaticClass(), TEXT("BoardWidget"));
		if (!Board) return false;
		Place(Root, Board, FVector2D::ZeroVector, FVector2D(Width, Height));
		UTextBlock* Title = MakeText(Tree, TEXT("TitleText"), TEXT("Trade"), 10);
		Place(Root, Title, FVector2D(12,8), FVector2D(Width-24,18));
		UTextBlock* OwnName = MakeText(Tree, TEXT("OwnNameText"), TEXT("You"), 9);
		Place(Root, OwnName, FVector2D(20,27), FVector2D(128,16));
		UTextBlock* PartnerName = MakeText(Tree, TEXT("PartnerNameText"), TEXT("Partner"), 9);
		Place(Root, PartnerName, FVector2D(242,27), FVector2D(128,16));
		for (int32 Side=0; Side<2; ++Side)
		{
			for (int32 Index=0; Index<12; ++Index)
			{
				const FString Name = FString::Printf(TEXT("%sSlot%02d"), Side==0 ? TEXT("Own") : TEXT("Partner"), Index);
				UWidget* Slot = MakeChild(Tree, TEXT("MT2InventorySlot"), UMT2InventorySlotWidget::StaticClass(), *Name);
				if (!Slot) return false;
				Place(Root, Slot, FVector2D((Side==0 ? 20.0f : 242.0f) + (Index%4)*32.0f,
					44.0f + (Index/4)*32.0f), FVector2D(32.0f));
			}
		}
		UEditableTextBox* OwnYang = Make<UEditableTextBox>(Tree, TEXT("OwnYangInput"));
		OwnYang->SetText(FText::FromString(TEXT("0")));
		PlaceMT2EditLine(Tree, Root, OwnYang, FVector2D(20,146), FVector2D(128,20), FAnchors(0), FVector2D::ZeroVector);
		UTextBlock* PartnerYang = MakeText(Tree, TEXT("PartnerYangText"), TEXT("0 Yang"), 9);
		Place(Root, PartnerYang, FVector2D(242,146), FVector2D(128,20));
		UTextBlock* Status = MakeText(Tree, TEXT("StatusText"), TEXT(""), 8);
		Place(Root, Status, FVector2D(150,148), FVector2D(90,18));
		UButton* Accept = MakeMT2Button(Tree, TEXT("AcceptButton"), TEXT("Accept"), EMT2GeneratedButtonSize::Middle);
		Place(Root, Accept, FVector2D(212,166), FVector2D(78,20));
		UButton* Cancel = MakeMT2Button(Tree, TEXT("CancelButton"), TEXT("Cancel"), EMT2GeneratedButtonSize::Middle);
		Place(Root, Cancel, FVector2D(296,166), FVector2D(78,20));
		return CompileAndSave(BP);
	}

	// Exact structure from uiscript/messengerwindow.py: a 170x300 board, the companion list at y=40,
	// and five 21x18 command sprites from T_windows, centred along the bottom.
	bool BuildMessenger()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2Messenger"), UMT2MessengerWidget::StaticClass(), BP, Tree)) return false;

		constexpr float WindowWidth = 170.0f;
		constexpr float WindowHeight = 300.0f;
		constexpr float FrameTile = 16.0f;
		const FLinearColor ThinBoardTint(0.3333f, 0.2941f, 0.2588f, 1.0f);

		USizeBox* RootSizeBox = Make<USizeBox>(Tree, TEXT("RootSizeBox"));
		RootSizeBox->SetWidthOverride(WindowWidth);
		RootSizeBox->SetHeightOverride(WindowHeight);
		Tree->RootWidget = RootSizeBox;

		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("MessengerRoot"));
		// Only what is drawn takes the mouse: the canvas covers the whole size box, so a hit-testable
		// one would eat clicks in the window's empty corners.
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		RootSizeBox->AddChild(Root);

		UBorder* Backdrop = Make<UBorder>(Tree, TEXT("BoardBackdrop"));
		Backdrop->SetBrushColor(FLinearColor(0.03f, 0.03f, 0.04f, 0.94f));
		Place(Root, Backdrop, FVector2D(FrameTile * 0.5f, FrameTile * 0.5f),
			FVector2D(WindowWidth - FrameTile, WindowHeight - FrameTile));
		auto Frame = [&](const TCHAR* Name, const TCHAR* Path, FVector2D Pos, FVector2D Size, bool TileX, bool TileY)
		{
			UImage* Image = Make<UImage>(Tree, Name);
			SetImageTexture(Image, Path, TileX, TileY);
			Image->SetColorAndOpacity(ThinBoardTint);
			Place(Root, Image, Pos, Size);
		};
		Frame(TEXT("FrameLT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_lefttop")), FVector2D(0, 0), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameRT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_righttop")), FVector2D(WindowWidth - FrameTile, 0), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameLB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_leftbottom")), FVector2D(0, WindowHeight - FrameTile), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameRB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_rightbottom")), FVector2D(WindowWidth - FrameTile, WindowHeight - FrameTile), FVector2D(FrameTile), false, false);
		Frame(TEXT("FrameTop"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_top")), FVector2D(FrameTile, 0), FVector2D(WindowWidth - FrameTile * 2.0f, FrameTile), true, false);
		Frame(TEXT("FrameBottom"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_bottom")), FVector2D(FrameTile, WindowHeight - FrameTile), FVector2D(WindowWidth - FrameTile * 2.0f, FrameTile), true, false);
		Frame(TEXT("FrameLeft"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_left")), FVector2D(0, FrameTile), FVector2D(FrameTile, WindowHeight - FrameTile * 2.0f), false, true);
		Frame(TEXT("FrameRight"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_right")), FVector2D(WindowWidth - FrameTile, FrameTile), FVector2D(FrameTile, WindowHeight - FrameTile * 2.0f), false, true);

		// "board_with_titlebar", titled MESSENGER_TITLE.
		if (UWidget* TitleBar = MakeChild(Tree, TEXT("MT2TitleBar"), UMT2TitleBarWidget::StaticClass(), TEXT("TitleBarWidget")))
		{
			Place(Root, TitleBar, FVector2D(12.0f, 7.0f), FVector2D(WindowWidth - 24.0f, 23.0f));
		}
		UTextBlock* TitleText = MakeText(Tree, TEXT("TitleText"), TEXT("Friends"), 10);
		Place(Root, TitleText, FVector2D(12.0f, 10.0f), FVector2D(WindowWidth - 24.0f, 18.0f));

		// The grouped list fills the board between the title bar and the button row.
		UScrollBox* FriendsBox = Make<UScrollBox>(Tree, TEXT("FriendsBox"));
		Place(Root, FriendsBox, FVector2D(20.0f, 40.0f), FVector2D(126.0f, 206.0f));

		UTexture2D* Windows = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_WindowsAtlas")));
		auto MessengerButton = [&](const TCHAR* Name, float X, FMT2AtlasRegion Normal, FMT2AtlasRegion Hovered, FMT2AtlasRegion Pressed)
		{
			UButton* Button = Make<UMT2AtlasButton>(Tree, Name);
			Button->IsFocusable = false;
			SetButtonAtlas(Button, Windows, Normal, Hovered, Pressed);
			Place(Root, Button, FVector2D(X, 252.0f), FVector2D(21.0f, 18.0f));
		};
		MessengerButton(TEXT("AddFriendButton"), 25.0f, {420,227,441,245}, {441,227,462,245}, {462,227,483,245});
		MessengerButton(TEXT("WhisperButton"), 55.0f, {340,340,361,358}, {361,340,382,358}, {382,340,403,358});
		MessengerButton(TEXT("MobileButton"), 85.0f, {172,340,193,358}, {193,340,214,358}, {214,340,235,358});
		MessengerButton(TEXT("RemoveButton"), 115.0f, {4,340,25,358}, {25,340,46,358}, {46,340,67,358});
		MessengerButton(TEXT("GuildButton"), 145.0f, {88,340,109,358}, {109,340,130,358}, {130,340,151,358});

		return CompileAndSave(BP);
	}

	bool BuildSystemMenu()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2SystemMenu"), UMT2SystemMenuWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("SystemMenuRoot")); Tree->RootWidget = Root;

		// Modal surface (same trick as the drop dialog): keeps clicks from leaking into
		// click-to-move / camera while the menu is open.
		UBorder* InputBlocker = Make<UBorder>(Tree, TEXT("InputBlocker"));
		InputBlocker->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f));
		Place(Root, InputBlocker, FVector2D::ZeroVector, FVector2D::ZeroVector, FAnchors(0, 0, 1, 1))->SetOffsets(FMargin(0));

		const FAnchors ScreenCenter(0.5f);
		const FVector2D CenterAlignment(0.5f);
		const FLinearColor ThinBoardTint(0.3333f, 0.2941f, 0.2588f, 1.0f);
		constexpr float FrameTile = 16.0f;

		// One centered page canvas + its thinboard frame (uiscript/systemdialog.py "board").
		auto MakePage = [&](const TCHAR* Name, float Height, float DialogWidth = 200.0f) -> UCanvasPanel*
		{
			UCanvasPanel* Page = Make<UCanvasPanel>(Tree, Name);
			Place(Root, Page, FVector2D::ZeroVector, FVector2D(DialogWidth, Height), ScreenCenter, CenterAlignment);
			auto Frame = [&](const TCHAR* Suffix, const TCHAR* Path, FVector2D Pos, FVector2D Size, bool TileX, bool TileY)
			{
				UImage* Image = Make<UImage>(Tree, *FString::Printf(TEXT("%s%s"), Name, Suffix));
				SetImageTexture(Image, Path, TileX, TileY);
				Image->SetColorAndOpacity(ThinBoardTint);
				Place(Page, Image, Pos, Size);
			};
			Frame(TEXT("FrameLT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_lefttop")), FVector2D(0, 0), FVector2D(FrameTile), false, false);
			Frame(TEXT("FrameRT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_righttop")), FVector2D(DialogWidth - FrameTile, 0), FVector2D(FrameTile), false, false);
			Frame(TEXT("FrameLB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_leftbottom")), FVector2D(0, Height - FrameTile), FVector2D(FrameTile), false, false);
			Frame(TEXT("FrameRB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_rightbottom")), FVector2D(DialogWidth - FrameTile, Height - FrameTile), FVector2D(FrameTile), false, false);
			Frame(TEXT("FrameTop"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_top")), FVector2D(FrameTile, 0), FVector2D(DialogWidth - FrameTile * 2.0f, FrameTile), true, false);
			Frame(TEXT("FrameBottom"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_bottom")), FVector2D(FrameTile, Height - FrameTile), FVector2D(DialogWidth - FrameTile * 2.0f, FrameTile), true, false);
			Frame(TEXT("FrameLeft"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_left")), FVector2D(0, FrameTile), FVector2D(FrameTile, Height - FrameTile * 2.0f), false, true);
			Frame(TEXT("FrameRight"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_right")), FVector2D(DialogWidth - FrameTile, FrameTile), FVector2D(FrameTile, Height - FrameTile * 2.0f), false, true);
			return Page;
		};
		auto PageButton = [&](UCanvasPanel* Page, const TCHAR* Name, const FString& Label, float Y)
		{
			UButton* Button = MakeMT2Button(Tree, Name, Label, EMT2GeneratedButtonSize::XLarge);
			Place(Page, Button, FVector2D(10.0f, Y), FVector2D(180.0f, 25.0f));
		};
		auto PageTitle = [&](UCanvasPanel* Page, const TCHAR* Name, const FString& Label)
		{
			Place(Page, MakeText(Tree, Name, Label, 11), FVector2D(10.0f, 12.0f), FVector2D(180.0f, 18.0f));
		};

		// Main page: the systemdialog.py button stack (30px pitch, wider gap before Cancel).
		UCanvasPanel* MainPanel = MakePage(TEXT("MainPanel"), 220.0f);
		PageButton(MainPanel, TEXT("AudioButton"), TEXT("Audio"), 17.0f);
		PageButton(MainPanel, TEXT("ControlsButton"), TEXT("Controls"), 47.0f);
		PageButton(MainPanel, TEXT("SettingsButton"), TEXT("Settings"), 77.0f);
		PageButton(MainPanel, TEXT("DisconnectButton"), TEXT("Disconnect"), 107.0f);
		PageButton(MainPanel, TEXT("ExitButton"), TEXT("Exit"), 137.0f);
		PageButton(MainPanel, TEXT("CancelButton"), TEXT("Cancel"), 177.0f);

		// Audio page: general/music/voice sliders + capture/output device pickers (the old
		// uiSystemOption sound block, extended with device selection).
		UCanvasPanel* AudioPanel = MakePage(TEXT("AudioPanel"), 326.0f);
		PageTitle(AudioPanel, TEXT("AudioTitle"), TEXT("Audio"));
		auto VolumeRow = [&](const TCHAR* Prefix, const FString& Label, float Y)
		{
			Place(AudioPanel, MakeText(Tree, *FString::Printf(TEXT("%sVolumeLabel"), Prefix), Label, 9), FVector2D(10.0f, Y), FVector2D(120.0f, 16.0f));
			Place(AudioPanel, MakeText(Tree, *FString::Printf(TEXT("%sVolumeValueText"), Prefix), TEXT("100%"), 9), FVector2D(140.0f, Y), FVector2D(48.0f, 16.0f));
			USlider* Slider = Make<USlider>(Tree, *FString::Printf(TEXT("%sVolumeSlider"), Prefix));
			Slider->SetMinValue(0.0f);
			Slider->SetMaxValue(1.0f);
			Slider->SetValue(1.0f);
			Place(AudioPanel, Slider, FVector2D(15.0f, Y + 18.0f), FVector2D(170.0f, 16.0f));
		};
		VolumeRow(TEXT("General"), TEXT("General"), 38.0f);
		VolumeRow(TEXT("Music"), TEXT("Music"), 80.0f);
		VolumeRow(TEXT("Voice"), TEXT("Voice Chat"), 122.0f);
		Place(AudioPanel, MakeText(Tree, TEXT("InputDeviceLabel"), TEXT("Voice input device"), 9), FVector2D(10.0f, 164.0f), FVector2D(180.0f, 16.0f));
		Place(AudioPanel, Make<UComboBoxString>(Tree, TEXT("InputDeviceCombo")), FVector2D(10.0f, 182.0f), FVector2D(180.0f, 22.0f));
		Place(AudioPanel, MakeText(Tree, TEXT("OutputDeviceLabel"), TEXT("Audio output device"), 9), FVector2D(10.0f, 212.0f), FVector2D(180.0f, 16.0f));
		Place(AudioPanel, Make<UComboBoxString>(Tree, TEXT("OutputDeviceCombo")), FVector2D(10.0f, 230.0f), FVector2D(180.0f, 22.0f));
		Place(AudioPanel, MakeText(Tree, TEXT("VoiceToggleModeLabel"), TEXT("Voice key toggle mode"), 9), FVector2D(10.0f, 258.0f), FVector2D(150.0f, 16.0f));
		Place(AudioPanel, Make<UCheckBox>(Tree, TEXT("VoiceToggleModeCheckBox")), FVector2D(168.0f, 255.0f), FVector2D(22.0f, 22.0f));
		PageButton(AudioPanel, TEXT("AudioBackButton"), TEXT("Back"), 284.0f);

		// Controls page: rebindable action keys. Rows are built at runtime by the widget class
		// into ControlsList; only the frame, scroll area and footer buttons are authored here.
		constexpr float ControlsWidth = 280.0f;
		UCanvasPanel* ControlsPanel = MakePage(TEXT("ControlsPanel"), 330.0f, ControlsWidth);
		PageTitle(ControlsPanel, TEXT("ControlsTitle"), TEXT("Controls"));
		Place(ControlsPanel, Make<UScrollBox>(Tree, TEXT("ControlsList")), FVector2D(14.0f, 38.0f), FVector2D(ControlsWidth - 28.0f, 196.0f));
		UButton* ResetAll = MakeMT2Button(Tree, TEXT("ResetAllKeysButton"), TEXT("Reset All"), EMT2GeneratedButtonSize::XLarge);
		Place(ControlsPanel, ResetAll, FVector2D((ControlsWidth - 180.0f) * 0.5f, 244.0f), FVector2D(180.0f, 25.0f));
		UButton* ControlsBack = MakeMT2Button(Tree, TEXT("ControlsBackButton"), TEXT("Back"), EMT2GeneratedButtonSize::XLarge);
		Place(ControlsPanel, ControlsBack, FVector2D((ControlsWidth - 180.0f) * 0.5f, 274.0f), FVector2D(180.0f, 25.0f));

		// Settings page: per-category scalability + motion blur + window mode (old uiSystemOption
		// video block, split into individual quality controls).
		constexpr float SettingsWidth = 240.0f;
		UCanvasPanel* SettingsPanel = MakePage(TEXT("SettingsPanel"), 368.0f, SettingsWidth);
		PageTitle(SettingsPanel, TEXT("SettingsTitle"), TEXT("Settings"));
		auto QualityRow = [&](const TCHAR* ComboName, const FString& Label, float Y)
		{
			UTextBlock* RowLabel = MakeText(Tree, *FString::Printf(TEXT("%sLabel"), ComboName), Label, 9);
			RowLabel->SetJustification(ETextJustify::Left);
			Place(SettingsPanel, RowLabel, FVector2D(12.0f, Y + 3.0f), FVector2D(98.0f, 16.0f));
			Place(SettingsPanel, Make<UComboBoxString>(Tree, ComboName), FVector2D(112.0f, Y), FVector2D(116.0f, 22.0f));
		};
		QualityRow(TEXT("ShadowQualityCombo"), TEXT("Shadows"), 38.0f);
		QualityRow(TEXT("TextureQualityCombo"), TEXT("Textures"), 64.0f);
		QualityRow(TEXT("EffectsQualityCombo"), TEXT("Effects"), 90.0f);
		QualityRow(TEXT("AntiAliasingQualityCombo"), TEXT("Anti-Aliasing"), 116.0f);
		QualityRow(TEXT("FoliageQualityCombo"), TEXT("Foliage"), 142.0f);
		QualityRow(TEXT("MotionBlurCombo"), TEXT("Motion Blur"), 168.0f);
		QualityRow(TEXT("FrameRateLimitCombo"), TEXT("FPS limit"), 194.0f);
		Place(SettingsPanel, MakeText(Tree, TEXT("VSyncLabel"), TEXT("VSync"), 9),
			FVector2D(12.0f, 223.0f), FVector2D(98.0f, 16.0f));
		Place(SettingsPanel, Make<UCheckBox>(Tree, TEXT("VSyncCheckBox")),
			FVector2D(112.0f, 220.0f), FVector2D(22.0f, 22.0f));
		Place(SettingsPanel, MakeText(Tree, TEXT("LanguageLabel"), TEXT("Language"), 9),
			FVector2D(12.0f, 247.0f), FVector2D(98.0f, 16.0f));
		Place(SettingsPanel, Make<UComboBoxString>(Tree, TEXT("LanguageCombo")),
			FVector2D(112.0f, 244.0f), FVector2D(116.0f, 22.0f));
		UButton* AggressiveMode = MakeMT2Button(
			Tree, TEXT("AggressiveModeButton"), FString(), EMT2GeneratedButtonSize::XLarge);
		AggressiveMode->AddChild(MakeText(
			Tree, TEXT("AggressiveModeText"), TEXT("PvP Mode: Neutral"), 9));
		Place(SettingsPanel, AggressiveMode, FVector2D(12.0f, 270.0f), FVector2D(216.0f, 21.0f));
		Place(SettingsPanel, MakeText(Tree, TEXT("WindowModeLabel"), TEXT("Window mode"), 9), FVector2D(10.0f, 296.0f), FVector2D(220.0f, 16.0f));
		auto SmallButton = [&](const TCHAR* Name, const FString& Label, FVector2D Pos, FVector2D Size)
		{
			UButton* Button = MakeMT2Button(Tree, Name, Label, EMT2GeneratedButtonSize::Middle);
			Place(SettingsPanel, Button, Pos, Size);
		};
		SmallButton(TEXT("WindowedButton"), TEXT("Windowed"), FVector2D(12.0f, 314.0f), FVector2D(105.0f, 21.0f));
		SmallButton(TEXT("FullscreenButton"), TEXT("Fullscreen"), FVector2D(123.0f, 314.0f), FVector2D(105.0f, 21.0f));
		UButton* SettingsBack = MakeMT2Button(Tree, TEXT("SettingsBackButton"), TEXT("Back"), EMT2GeneratedButtonSize::XLarge);
		Place(SettingsPanel, SettingsBack, FVector2D((SettingsWidth - 180.0f) * 0.5f, 338.0f), FVector2D(180.0f, 25.0f));

		return CompileAndSave(BP);
	}

	bool PatchSystemMenuAggression()
	{
		UWidgetBlueprint* BP = LoadBP(TEXT("MT2SystemMenu"));
		UWidgetTree* Tree = BP ? BP->WidgetTree : nullptr;
		UCanvasPanel* SettingsPanel = Tree
			? Cast<UCanvasPanel>(Tree->FindWidget(TEXT("SettingsPanel"))) : nullptr;
		if (!BP || !Tree || !SettingsPanel)
		{
			UE_LOG(LogTemp, Error, TEXT("Cannot patch /Game/UI/MT2SystemMenu: SettingsPanel is missing."));
			return false;
		}

		BP->Modify();
		Tree->Modify();
		SettingsPanel->Modify();
		if (!Tree->FindWidget(TEXT("AggressiveModeButton")))
		{
			UButton* Button = MakeMT2Button(
				Tree, TEXT("AggressiveModeButton"), FString(), EMT2GeneratedButtonSize::XLarge);
			Button->AddChild(MakeText(
				Tree, TEXT("AggressiveModeText"), TEXT("PvP Mode: Neutral"), 9));
			Place(SettingsPanel, Button, FVector2D(12.0f, 244.0f), FVector2D(216.0f, 21.0f));
		}

		// Widgets inserted into an existing Widget Blueprint do not receive variable GUIDs
		// automatically. Register them before structural compilation so BindWidget resolves.
		for (const FName WidgetName : { FName(TEXT("AggressiveModeButton")), FName(TEXT("AggressiveModeText")) })
		{
			if (UWidget* Widget = Tree->FindWidget(WidgetName))
			{
				Widget->bIsVariable = true;
				FGuid& VariableGuid = BP->WidgetVariableNameToGuidMap.FindOrAdd(WidgetName);
				if (!VariableGuid.IsValid())
				{
					VariableGuid = FGuid::NewGuid();
				}
			}
		}

		auto MoveWidget = [Tree](const TCHAR* Name, const FVector2D& Position)
		{
			if (UWidget* Widget = Tree->FindWidget(Name))
			{
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
				{
					Slot->SetPosition(Position);
				}
			}
		};
		MoveWidget(TEXT("WindowModeLabel"), FVector2D(10.0f, 270.0f));
		MoveWidget(TEXT("WindowedButton"), FVector2D(12.0f, 288.0f));
		MoveWidget(TEXT("FullscreenButton"), FVector2D(123.0f, 288.0f));
		MoveWidget(TEXT("SettingsBackButton"), FVector2D(30.0f, 312.0f));
		return CompileAndSave(BP);
	}

	bool PatchSystemMenuVoiceToggleMode()
	{
		UWidgetBlueprint* BP = LoadBP(TEXT("MT2SystemMenu"));
		UWidgetTree* Tree = BP ? BP->WidgetTree : nullptr;
		UCanvasPanel* AudioPanel = Tree
			? Cast<UCanvasPanel>(Tree->FindWidget(TEXT("AudioPanel"))) : nullptr;
		if (!BP || !Tree || !AudioPanel)
		{
			UE_LOG(LogTemp, Error, TEXT("Cannot patch /Game/UI/MT2SystemMenu: AudioPanel is missing."));
			return false;
		}

		BP->Modify();
		Tree->Modify();
		AudioPanel->Modify();
		if (!Tree->FindWidget(TEXT("VoiceToggleModeLabel")))
		{
			Place(AudioPanel, MakeText(Tree, TEXT("VoiceToggleModeLabel"),
				TEXT("Voice key toggle mode"), 9), FVector2D(10.0f, 258.0f), FVector2D(150.0f, 16.0f));
		}
		if (!Tree->FindWidget(TEXT("VoiceToggleModeCheckBox")))
		{
			Place(AudioPanel, Make<UCheckBox>(Tree, TEXT("VoiceToggleModeCheckBox")),
				FVector2D(168.0f, 255.0f), FVector2D(22.0f, 22.0f));
		}

		for (const FName WidgetName : { FName(TEXT("VoiceToggleModeLabel")), FName(TEXT("VoiceToggleModeCheckBox")) })
		{
			if (UWidget* Widget = Tree->FindWidget(WidgetName))
			{
				Widget->bIsVariable = true;
				FGuid& VariableGuid = BP->WidgetVariableNameToGuidMap.FindOrAdd(WidgetName);
				if (!VariableGuid.IsValid())
				{
					VariableGuid = FGuid::NewGuid();
				}
			}
		}

		auto SetPosition = [Tree](const TCHAR* Name, const FVector2D& Position)
		{
			if (UWidget* Widget = Tree->FindWidget(Name))
			{
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
				{
					Slot->SetPosition(Position);
				}
			}
		};
		auto SetSize = [Tree](const TCHAR* Name, const FVector2D& Size)
		{
			if (UWidget* Widget = Tree->FindWidget(Name))
			{
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
				{
					Slot->SetSize(Size);
				}
			}
		};

		SetSize(TEXT("AudioPanel"), FVector2D(200.0f, 326.0f));
		SetPosition(TEXT("AudioPanelFrameLB"), FVector2D(0.0f, 310.0f));
		SetPosition(TEXT("AudioPanelFrameRB"), FVector2D(184.0f, 310.0f));
		SetPosition(TEXT("AudioPanelFrameBottom"), FVector2D(16.0f, 310.0f));
		SetSize(TEXT("AudioPanelFrameLeft"), FVector2D(16.0f, 294.0f));
		SetSize(TEXT("AudioPanelFrameRight"), FVector2D(16.0f, 294.0f));
		SetPosition(TEXT("AudioBackButton"), FVector2D(10.0f, 284.0f));
		return CompileAndSave(BP);
	}

	bool PatchSystemMenuLanguage()
	{
		UWidgetBlueprint* BP = LoadBP(TEXT("MT2SystemMenu"));
		UWidgetTree* Tree = BP ? BP->WidgetTree : nullptr;
		UCanvasPanel* SettingsPanel = Tree
			? Cast<UCanvasPanel>(Tree->FindWidget(TEXT("SettingsPanel"))) : nullptr;
		if (!BP || !Tree || !SettingsPanel)
		{
			UE_LOG(LogTemp, Error, TEXT("Cannot patch /Game/UI/MT2SystemMenu: SettingsPanel is missing."));
			return false;
		}

		BP->Modify();
		Tree->Modify();
		SettingsPanel->Modify();
		if (!Tree->FindWidget(TEXT("LanguageLabel")))
		{
			Place(SettingsPanel, MakeText(Tree, TEXT("LanguageLabel"), TEXT("Language"), 9),
				FVector2D(12.0f, 247.0f), FVector2D(98.0f, 16.0f));
		}
		if (!Tree->FindWidget(TEXT("LanguageCombo")))
		{
			Place(SettingsPanel, Make<UComboBoxString>(Tree, TEXT("LanguageCombo")),
				FVector2D(112.0f, 244.0f), FVector2D(116.0f, 22.0f));
		}

		for (const FName WidgetName : {FName(TEXT("LanguageLabel")), FName(TEXT("LanguageCombo"))})
		{
			if (UWidget* Widget = Tree->FindWidget(WidgetName))
			{
				Widget->bIsVariable = true;
				FGuid& VariableGuid = BP->WidgetVariableNameToGuidMap.FindOrAdd(WidgetName);
				if (!VariableGuid.IsValid())
				{
					VariableGuid = FGuid::NewGuid();
				}
			}
		}

		auto SetPosition = [Tree](const TCHAR* Name, const FVector2D& Position)
		{
			if (UWidget* Widget = Tree->FindWidget(Name))
			{
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
				{
					Slot->SetPosition(Position);
				}
			}
		};
		auto SetSize = [Tree](const TCHAR* Name, const FVector2D& Size)
		{
			if (UWidget* Widget = Tree->FindWidget(Name))
			{
				if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot))
				{
					Slot->SetSize(Size);
				}
			}
		};

		SetSize(TEXT("SettingsPanel"), FVector2D(240.0f, 368.0f));
		SetPosition(TEXT("SettingsPanelFrameLB"), FVector2D(0.0f, 352.0f));
		SetPosition(TEXT("SettingsPanelFrameRB"), FVector2D(224.0f, 352.0f));
		SetPosition(TEXT("SettingsPanelFrameBottom"), FVector2D(16.0f, 352.0f));
		SetSize(TEXT("SettingsPanelFrameLeft"), FVector2D(16.0f, 336.0f));
		SetSize(TEXT("SettingsPanelFrameRight"), FVector2D(16.0f, 336.0f));
		SetPosition(TEXT("AggressiveModeButton"), FVector2D(12.0f, 270.0f));
		SetPosition(TEXT("WindowModeLabel"), FVector2D(10.0f, 296.0f));
		SetPosition(TEXT("WindowedButton"), FVector2D(12.0f, 314.0f));
		SetPosition(TEXT("FullscreenButton"), FVector2D(123.0f, 314.0f));
		SetPosition(TEXT("SettingsBackButton"), FVector2D(30.0f, 338.0f));
		return CompileAndSave(BP);
	}

	bool BuildItemDropDialog()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2ItemDropDialog"), UMT2ItemDropDialogWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("DropDialogRoot")); Tree->RootWidget = Root;

		// A nearly transparent full-screen surface makes the question modal and prevents the confirmation
		// click from also reaching terrain movement/camera input.
		UBorder* InputBlocker = Make<UBorder>(Tree, TEXT("InputBlocker"));
		InputBlocker->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f));
		Place(Root, InputBlocker, FVector2D::ZeroVector, FVector2D::ZeroVector, FAnchors(0, 0, 1, 1))->SetOffsets(FMargin(0));

		const FAnchors Center(0.5f);
		const FVector2D CenterAlignment(0.5f);
		UWidget* Board = MakeChild(Tree, TEXT("MT2Board"), UMT2BoardWidget::StaticClass(), TEXT("BoardWidget"));
		if (!Board) return false;
		Place(Root, Board, FVector2D::ZeroVector, FVector2D(340.0f, 105.0f), Center, CenterAlignment);

		Place(Root, MakeText(Tree, TEXT("MessageText"), TEXT("Drop this item on the ground?"), 10),
			FVector2D(0.0f, -14.0f), FVector2D(310.0f, 24.0f), Center, CenterAlignment);

		UTexture2D* Public = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
		auto DialogButton = [&](const TCHAR* Name, const TCHAR* Label, float X)
		{
			UButton* Button = Make<UMT2AtlasButton>(Tree, Name);
			SetButtonAtlas(Button, Public, FMT2AtlasRegion(194,142,255,163),
				FMT2AtlasRegion(88,181,149,202), FMT2AtlasRegion(149,181,210,202));
			Button->AddChild(MakeText(Tree, *FString::Printf(TEXT("%sLabel"), Name), Label, 9));
			Place(Root, Button, FVector2D(X, 28.0f), FVector2D(61.0f, 21.0f), Center, CenterAlignment);
		};
		DialogButton(TEXT("ConfirmButton"), TEXT("Yes"), -40.0f);
		DialogButton(TEXT("CancelButton"), TEXT("No"), 40.0f);
		return CompileAndSave(BP);
	}

	bool BuildPartyMember()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2PartyMember"), UMT2PartyMemberWidget::StaticClass(), BP, Tree)) return false;
		USizeBox* Root = Make<USizeBox>(Tree, TEXT("PartyMemberRoot"));
		Root->SetWidthOverride(190.0f);
		Root->SetHeightOverride(46.0f);
		Tree->RootWidget = Root;
		UCanvasPanel* Content = Make<UCanvasPanel>(Tree, TEXT("PartyMemberContent"));
		Root->AddChild(Content);

		UBorder* Background = Make<UBorder>(Tree, TEXT("MemberBackground"));
		Background->SetBrushColor(FLinearColor(0.02f, 0.025f, 0.02f, 0.82f));
		Place(Content, Background, FVector2D::ZeroVector, FVector2D(190.0f, 46.0f));

		UImage* Face = Make<UImage>(Tree, TEXT("FaceImage"));
		SetImageTexture(Face, UMT2PathSettings::Path(TEXT("icon_face_T_warrior_m")));
		Place(Content, Face, FVector2D(4.0f, 4.0f), FVector2D(38.0f, 38.0f));

		UTextBlock* Name = MakeText(Tree, TEXT("NameText"), TEXT("Party member"), 9);
		Name->SetJustification(ETextJustify::Left);
		Place(Content, Name, FVector2D(48.0f, 4.0f), FVector2D(91.0f, 16.0f));
		UTextBlock* Level = MakeText(Tree, TEXT("LevelText"), TEXT("Lv 1"), 8);
		Level->SetJustification(ETextJustify::Right);
		Place(Content, Level, FVector2D(139.0f, 4.0f), FVector2D(24.0f, 16.0f));
		Place(Content, MakeMT2Button(Tree, TEXT("KickButton"), TEXT("X"),
			EMT2GeneratedButtonSize::Small), FVector2D(166.0f, 3.0f), FVector2D(20.0f, 18.0f));

		UProgressBar* Health = Make<UProgressBar>(Tree, TEXT("HealthBar"));
		Health->SetPercent(1.0f);
		Health->SetFillColorAndOpacity(FLinearColor(0.78f, 0.05f, 0.04f, 1.0f));
		Place(Content, Health, FVector2D(48.0f, 25.0f), FVector2D(137.0f, 10.0f));
		return CompileAndSave(BP);
	}

	bool BuildPartyPanel()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2PartyPanel"), UMT2PartyPanelWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("PartyPanelRoot"));
		Tree->RootWidget = Root;
		UTextBlock* Title = MakeText(Tree, TEXT("PartyTitle"), TEXT("Party"), 10);
		Title->SetJustification(ETextJustify::Left);
		Place(Root, Title, FVector2D(2.0f, 0.0f), FVector2D(188.0f, 18.0f));
		Place(Root, Make<UVerticalBox>(Tree, TEXT("MemberList")),
			FVector2D(0.0f, 20.0f), FVector2D(190.0f, 368.0f));
		Place(Root, MakeMT2Button(Tree, TEXT("DisbandButton"), TEXT("Disband Party"),
			EMT2GeneratedButtonSize::Large), FVector2D(50.0f, 393.0f), FVector2D(90.0f, 21.0f));
		Place(Root, MakeMT2Button(Tree, TEXT("LeavePartyButton"), TEXT("Leave Party"),
			EMT2GeneratedButtonSize::Large), FVector2D(50.0f, 393.0f), FVector2D(90.0f, 21.0f));
		return CompileAndSave(BP);
	}

	bool BuildPartyInviteDialog()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2PartyInviteDialog"),
			UMT2PartyInviteDialogWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("PartyInviteRoot"));
		Tree->RootWidget = Root;

		UBorder* InputBlocker = Make<UBorder>(Tree, TEXT("InputBlocker"));
		InputBlocker->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.01f));
		Place(Root, InputBlocker, FVector2D::ZeroVector, FVector2D::ZeroVector,
			FAnchors(0, 0, 1, 1))->SetOffsets(FMargin(0));

		const FAnchors Center(0.5f);
		const FVector2D CenterAlignment(0.5f);
		UBorder* Panel = Make<UBorder>(Tree, TEXT("InvitePanel"));
		Panel->SetBrushColor(FLinearColor(0.025f, 0.02f, 0.015f, 0.96f));
		Place(Root, Panel, FVector2D::ZeroVector, FVector2D(340.0f, 105.0f),
			Center, CenterAlignment);
		Place(Root, MakeText(Tree, TEXT("MessageText"), TEXT("Player invited you to a party."), 10),
			FVector2D(0.0f, -15.0f), FVector2D(310.0f, 26.0f), Center, CenterAlignment);
		Place(Root, MakeMT2Button(Tree, TEXT("AcceptButton"), TEXT("Accept"),
			EMT2GeneratedButtonSize::Middle), FVector2D(-52.0f, 27.0f),
			FVector2D(94.0f, 22.0f), Center, CenterAlignment);
		Place(Root, MakeMT2Button(Tree, TEXT("DeclineButton"), TEXT("Decline"),
			EMT2GeneratedButtonSize::Middle), FVector2D(52.0f, 27.0f),
			FVector2D(94.0f, 22.0f), Center, CenterAlignment);
		return CompileAndSave(BP);
	}

	bool BuildChat()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2Chat"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("ChatRoot")); Tree->RootWidget = Root;

		constexpr float PanelWidth = 600.0f;
		constexpr float PanelLeft = 18.0f;
		constexpr float InputHeight = 20.0f;
		constexpr float InputBottom = 42.0f;
		constexpr float HistoryHeight = 140.0f;
		const FAnchors BottomLeft(0.0f, 1.0f);

		auto PlaceBottomLeft = [&](UWidget* Widget, FVector2D Pos, FVector2D Size)
		{
			UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Widget);
			Slot->SetAnchors(BottomLeft);
			Slot->SetAlignment(FVector2D(0.0f, 1.0f));
			Slot->SetPosition(Pos);
			Slot->SetSize(Size);
		};

		UCanvasPanel* HistoryPanel = Make<UCanvasPanel>(Tree, TEXT("HistoryPanel"));
		PlaceBottomLeft(HistoryPanel, FVector2D(PanelLeft, -(InputBottom + InputHeight)),
			FVector2D(PanelWidth, HistoryHeight));

		UBorder* HistoryBg = Make<UBorder>(Tree, TEXT("ChatHistoryBackground"));
		HistoryBg->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
		Place(HistoryPanel, HistoryBg, FVector2D(0.0f, 9.0f), FVector2D(PanelWidth, HistoryHeight - 9.0f));

		UImage* ChatBarLeft = Make<UImage>(Tree, TEXT("ChatBarLeft"));
		SetImageTexture(ChatBarLeft, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_chat_bar_left")));
		Place(HistoryPanel, ChatBarLeft, FVector2D::ZeroVector, FVector2D(18.0f, 12.0f));
		UImage* ChatBarMiddle = Make<UImage>(Tree, TEXT("ChatBarMiddle"));
		SetImageTexture(ChatBarMiddle, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_chat_bar_middle")), true, false);
		Place(HistoryPanel, ChatBarMiddle, FVector2D(18.0f, 0.0f), FVector2D(PanelWidth - 36.0f, 12.0f));
		UImage* ChatBarRight = Make<UImage>(Tree, TEXT("ChatBarRight"));
		SetImageTexture(ChatBarRight, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_chat_bar_right")));
		Place(HistoryPanel, ChatBarRight, FVector2D(PanelWidth - 18.0f, 0.0f), FVector2D(18.0f, 12.0f));

		UScrollBox* HistoryBox = Make<UScrollBox>(Tree, TEXT("HistoryBox"));
		HistoryBox->SetScrollBarVisibility(ESlateVisibility::Collapsed);
		Place(HistoryPanel, HistoryBox, FVector2D(8.0f, 17.0f), FVector2D(PanelWidth - 16.0f, HistoryHeight - 22.0f));

		// Separate reward feed required by UMT2ChatWidget. Its Canvas slot remains fully editable in
		// the generated Widget Blueprint after creation.
		UScrollBox* RewardHistoryBox = Make<UScrollBox>(Tree, TEXT("RewardHistoryBox"));
		RewardHistoryBox->SetScrollBarVisibility(ESlateVisibility::Collapsed);
		PlaceBottomLeft(RewardHistoryBox, FVector2D(PanelLeft, -218.0f), FVector2D(PanelWidth, 72.0f));

		UCanvasPanel* InputControls = Make<UCanvasPanel>(Tree, TEXT("InputControls"));
		PlaceBottomLeft(InputControls, FVector2D(PanelLeft, -InputBottom), FVector2D(PanelWidth, InputHeight));

		UBorder* ModeFrame = Make<UBorder>(Tree, TEXT("ModeFrame"));
		ModeFrame->SetBrushColor(FLinearColor::White);
		Place(InputControls, ModeFrame, FVector2D(0.0f, 1.0f), FVector2D(43.0f, 18.0f));
		UBorder* ModeBg = Make<UBorder>(Tree, TEXT("ModeBackground"));
		ModeBg->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.82f));
		Place(InputControls, ModeBg, FVector2D(1.0f, 2.0f), FVector2D(41.0f, 16.0f));
		UButton* ModeButton = MakeButton(Tree, TEXT("ModeButton"), TEXT("Normal"));
		FButtonStyle ModeStyle = ModeButton->GetStyle();
		FSlateBrush NoDrawBrush; NoDrawBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
		ModeStyle.SetNormal(NoDrawBrush); ModeStyle.SetHovered(NoDrawBrush); ModeStyle.SetPressed(NoDrawBrush);
		ModeButton->SetStyle(ModeStyle);
		Place(InputControls, ModeButton, FVector2D(1.0f, 2.0f), FVector2D(41.0f, 16.0f));

		UBorder* InputFrame = Make<UBorder>(Tree, TEXT("InputFrame"));
		InputFrame->SetBrushColor(FLinearColor::White);
		Place(InputControls, InputFrame, FVector2D(48.0f, 1.0f), FVector2D(486.0f, 18.0f));
		UBorder* InputBg = Make<UBorder>(Tree, TEXT("ChatInputBackground"));
		InputBg->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.82f));
		Place(InputControls, InputBg, FVector2D(49.0f, 2.0f), FVector2D(484.0f, 16.0f));

		UEditableTextBox* InputBox = Make<UEditableTextBox>(Tree, TEXT("InputBox"));
		InputBox->SetHintText(FText::GetEmpty());
		InputBox->SetForegroundColor(FLinearColor::White);
		FEditableTextBoxStyle InputStyle = InputBox->GetWidgetStyle();
		InputStyle.SetBackgroundImageNormal(NoDrawBrush);
		InputStyle.SetBackgroundImageHovered(NoDrawBrush);
		InputStyle.SetBackgroundImageFocused(NoDrawBrush);
		InputStyle.SetBackgroundImageReadOnly(NoDrawBrush);
		FSlateFontInfo InputFont = InputStyle.TextStyle.Font;
		InputFont.Size = 10;
		InputStyle.SetFont(InputFont);
		InputStyle.SetPadding(FMargin(2.0f, 0.0f));
		InputBox->SetWidgetStyle(InputStyle);
		Place(InputControls, InputBox, FVector2D(50.0f, 2.0f), FVector2D(482.0f, 16.0f));

		UTexture2D* Taskbar = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_TaskbarAtlas")));
		UButton* SendButton = Make<UMT2AtlasButton>(Tree, TEXT("SendButton"));
		SendButton->IsFocusable = false;
		SetButtonAtlas(SendButton, Taskbar, {62,159,82,177}, {82,159,102,177}, {102,159,122,177});
		Place(InputControls, SendButton, FVector2D(538.0f, 1.0f), FVector2D(20.0f, 18.0f));
		UButton* WhisperButton = Make<UMT2AtlasButton>(Tree, TEXT("WhisperButton"));
		WhisperButton->IsFocusable = false;
		SetButtonAtlas(WhisperButton, Taskbar, {122,159,142,177}, {142,159,162,177}, {162,159,182,177});
		Place(InputControls, WhisperButton, FVector2D(559.0f, 1.0f), FVector2D(20.0f, 18.0f));
		UButton* HistoryButton = Make<UMT2AtlasButton>(Tree, TEXT("HistoryButton"));
		HistoryButton->IsFocusable = false;
		SetButtonAtlas(HistoryButton, Taskbar, {456,127,476,145}, {476,127,496,145}, {42,159,62,177});
		Place(InputControls, HistoryButton, FVector2D(580.0f, 1.0f), FVector2D(20.0f, 18.0f));

		return CompileAndSave(BP);
	}

	bool BuildTargetInfo()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2TargetInfo"), UMT2TargetInfoWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("TargetInfoRoot")); Tree->RootWidget = Root;

		UBorder* Background = Make<UBorder>(Tree, TEXT("PanelBackground"));
		Background->SetBrushColor(FLinearColor(0.12f, 0.14f, 0.18f, 0.96f));
		Place(Root, Background, FVector2D(2,2), FVector2D(378,44));

		auto FrameImage = [&](const TCHAR* Name, const TCHAR* Path, FVector2D Position, FVector2D Size,
			bool bTileX = false, bool bTileY = false)
		{
			UImage* Image = Make<UImage>(Tree, Name);
			SetImageTexture(Image, Path, bTileX, bTileY);
			Place(Root, Image, Position, Size);
		};
		FrameImage(TEXT("FrameTop"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_top")), FVector2D(16,0), FVector2D(350,16), true);
		FrameImage(TEXT("FrameBottom"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_bottom")), FVector2D(16,32), FVector2D(350,16), true);
		FrameImage(TEXT("FrameLeft"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_left")), FVector2D(0,16), FVector2D(16,16), false, true);
		FrameImage(TEXT("FrameRight"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_line_right")), FVector2D(366,16), FVector2D(16,16), false, true);
		FrameImage(TEXT("FrameLT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_lefttop")), FVector2D(0,0), FVector2D(16));
		FrameImage(TEXT("FrameRT"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_righttop")), FVector2D(366,0), FVector2D(16));
		FrameImage(TEXT("FrameLB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_leftbottom")), FVector2D(0,32), FVector2D(16));
		FrameImage(TEXT("FrameRB"), UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_thinboard_corner_rightbottom")), FVector2D(366,32), FVector2D(16));

		UTextBlock* TargetName = MakeText(Tree, TEXT("TargetNameText"), TEXT("Target"), 10);
		TargetName->SetJustification(ETextJustify::Left);
		TargetName->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Place(Root, TargetName, FVector2D(22,15), FVector2D(190,20));

		UProgressBar* HealthBar = Make<UProgressBar>(Tree, TEXT("HealthBar"));
		HealthBar->SetPercent(1.0f);
		HealthBar->SetFillColorAndOpacity(FLinearColor(0.78f, 0.02f, 0.025f, 1.0f));
		Place(Root, HealthBar, FVector2D(211,19), FVector2D(137,11));

		UMT2AtlasButton* Close = Make<UMT2AtlasButton>(Tree, TEXT("CloseButton"));
		UTexture2D* Public = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
		SetButtonAtlas(Close, Public, FMT2AtlasRegion(25,425,40,440),
			FMT2AtlasRegion(40,425,55,440), FMT2AtlasRegion(55,425,70,440));
		Place(Root, Close, FVector2D(352,16), FVector2D(15));
		return CompileAndSave(BP);
	}

	bool BuildHUD()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!Begin(TEXT("MT2GameHUD"), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("HUDRoot")); Tree->RootWidget = Root;
		UWidget* Taskbar = MakeChild(Tree, TEXT("MT2TaskBar"), UMT2TaskbarWidget::StaticClass(), TEXT("TaskbarWidget")); if (!Taskbar) return false;
		// Full-width strip flush against the bottom, 42px tall. Anchored bottom + horizontally stretched,
		// aligned by its own bottom edge so Offset.Bottom is the actual height (42) instead of stretching
		// to fill the whole screen.
		UCanvasPanelSlot* TaskbarSlot = Root->AddChildToCanvas(Taskbar);
		TaskbarSlot->SetAnchors(FAnchors(0.0f, 1.0f, 1.0f, 1.0f));
		TaskbarSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		TaskbarSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 42.0f));
		UWidget* Inventory = MakeChild(Tree, TEXT("MT2Inventory"), UMT2InventoryWidget::StaticClass(), TEXT("InventoryWidget")); if (!Inventory) return false;
		Place(Root, Inventory, FVector2D(0,-37), FVector2D(176,565), FAnchors(1,1), FVector2D(1,1));
		UWidget* Character = MakeChild(Tree, TEXT("MT2CharacterWindow"), UMT2CharacterWindowWidget::StaticClass(), TEXT("CharacterWindowWidget")); if (!Character) return false;
		Place(Root, Character, FVector2D(216,6), FVector2D(256,360));
		UWidget* TargetInfo = MakeChild(Tree, TEXT("MT2TargetInfo"), UMT2TargetInfoWidget::StaticClass(), TEXT("TargetInfoWidget")); if (!TargetInfo) return false;
		Place(Root, TargetInfo, FVector2D(0,12), FVector2D(382,48), FAnchors(0.5f,0.0f), FVector2D(0.5f,0.0f));
		UWidget* Minimap = MakeChild(Tree, TEXT("MT2Minimap"), UMT2MinimapWidget::StaticClass(), TEXT("MinimapWidget")); if (!Minimap) return false;
		Place(Root, Minimap, FVector2D::ZeroVector, FVector2D(180,165), FAnchors(1,0), FVector2D(1,0));
		UWidget* FullMap = MakeChild(Tree, TEXT("MT2FullMap"), UMT2FullMapWidget::StaticClass(), TEXT("FullMapWidget")); if (!FullMap) return false;
		Place(Root, FullMap, FVector2D(-146,0), FVector2D(271,294), FAnchors(1,0), FVector2D(1,0));
		return CompileAndSave(BP);
	}

	bool AttachTradeToHUD()
	{
		UWidgetBlueprint* BP = LoadBP(TEXT("MT2GameHUD"));
		UWidgetTree* Tree = BP ? BP->WidgetTree : nullptr;
		UCanvasPanel* Root = Tree ? Cast<UCanvasPanel>(Tree->RootWidget) : nullptr;
		if (!BP || !Tree || !Root)
		{
			UE_LOG(LogTemp, Error, TEXT("MT2GameHUD must have an existing CanvasPanel root."));
			return false;
		}

		if (UWidget* Existing = Tree->FindWidget(TEXT("TradeWidget")))
		{
			if (!Existing->IsA(UMT2TradeWidget::StaticClass()))
			{
				UE_LOG(LogTemp, Error, TEXT("MT2GameHUD.TradeWidget exists but has the wrong class."));
				return false;
			}
			return CompileAndSave(BP);
		}

		UWidget* Trade = MakeChild(Tree, TEXT("MT2Trade"), UMT2TradeWidget::StaticClass(), TEXT("TradeWidget"));
		if (!Trade) return false;
		BP->WidgetVariableNameToGuidMap.Add(Trade->GetFName(), FGuid::NewGuid());
		Place(Root, Trade, FVector2D::ZeroVector, FVector2D(390, 190), FAnchors(0.5f), FVector2D(0.5f));
		return CompileAndSave(BP);
	}

	bool BuildGuild()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2Guild"), UMT2GuildWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("GuildRoot")); Tree->RootWidget = Root;
		constexpr float Width = 376.0f;
		constexpr float Height = 356.0f;

		UWidget* Board = MakeChild(Tree, TEXT("MT2Board"), UMT2BoardWidget::StaticClass(), TEXT("BoardWidget"));
		if (!Board) return false;
		Place(Root, Board, FVector2D::ZeroVector, FVector2D(Width, Height));
		UWidget* TitleBar = MakeChild(Tree, TEXT("MT2TitleBar"), UMT2TitleBarWidget::StaticClass(), TEXT("TitleBarWidget"));
		if (!TitleBar) return false;
		Place(Root, TitleBar, FVector2D(12.0f, 7.0f), FVector2D(Width - 24.0f, 23.0f));
		Place(Root, MakeText(Tree, TEXT("WindowTitleText"), TEXT("Guild"), 10), FVector2D(12.0f, 10.0f), FVector2D(Width - 24.0f, 17.0f));

		UCanvasPanel* NoGuild = Make<UCanvasPanel>(Tree, TEXT("NoGuildPanel"));
		Place(Root, NoGuild, FVector2D(8.0f, 30.0f), FVector2D(360.0f, 289.0f));
		Place(NoGuild, MakeText(Tree, TEXT("NoGuildTitle"), TEXT("Create Guild"), 11), FVector2D(40.0f, 66.0f), FVector2D(280.0f, 20.0f));
		Place(NoGuild, MakeText(Tree, TEXT("NoGuildHint"), TEXT("Enter a guild name"), 9), FVector2D(40.0f, 98.0f), FVector2D(280.0f, 18.0f));
		UEditableTextBox* NameInput = Make<UEditableTextBox>(Tree, TEXT("GuildNameInput"));
		NameInput->SetHintText(FText::FromString(TEXT("Guild name")));
		PlaceMT2EditLine(Tree, NoGuild, NameInput, FVector2D(70.0f, 124.0f), FVector2D(220.0f, 18.0f), FAnchors(0), FVector2D::ZeroVector);
		Place(NoGuild, MakeMT2Button(Tree, TEXT("CreateGuildButton"), TEXT("Create"), EMT2GeneratedButtonSize::Large),
			FVector2D(136.0f, 158.0f), FVector2D(88.0f, 21.0f));
		Place(NoGuild, MakeText(Tree, TEXT("CreateCostText"), TEXT("Cost: 200,000 Yang"), 9), FVector2D(90.0f, 192.0f), FVector2D(180.0f, 18.0f));

		UCanvasPanel* Guild = Make<UCanvasPanel>(Tree, TEXT("GuildPanel"));
		Place(Root, Guild, FVector2D(8.0f, 30.0f), FVector2D(360.0f, 289.0f));

		auto Page = [&](const TCHAR* Name) -> UCanvasPanel*
		{
			UCanvasPanel* Result = Make<UCanvasPanel>(Tree, Name);
			Place(Guild, Result, FVector2D::ZeroVector, FVector2D(360.0f, 289.0f));
			return Result;
		};
		auto Section = [&](UCanvasPanel* Parent, const TCHAR* Name, const FString& Label, FVector2D Pos, FVector2D Size)
		{
			UBorder* Panel = Make<UBorder>(Tree, Name);
			Panel->SetBrushColor(FLinearColor(0.035f, 0.03f, 0.025f, 0.78f));
			Place(Parent, Panel, Pos, Size);
			Place(Parent, MakeText(Tree, *FString::Printf(TEXT("%sTitle"), Name), Label, 9), Pos + FVector2D(4.0f, 2.0f), FVector2D(Size.X - 8.0f, 17.0f));
		};
		auto Value = [&](UCanvasPanel* Parent, const TCHAR* Name, const FString& Label, float X, float Y, float LabelWidth, float ValueWidth) -> UTextBlock*
		{
			UTextBlock* LabelText = MakeText(Tree, *FString::Printf(TEXT("%sLabel"), Name), Label, 9);
			LabelText->SetJustification(ETextJustify::Left);
			Place(Parent, LabelText, FVector2D(X, Y), FVector2D(LabelWidth, 17.0f));
			UBorder* Slot = Make<UBorder>(Tree, *FString::Printf(TEXT("%sSlot"), Name));
			Slot->SetBrushColor(FLinearColor(0.01f, 0.01f, 0.01f, 0.9f));
			Place(Parent, Slot, FVector2D(X + LabelWidth, Y), FVector2D(ValueWidth, 17.0f));
			UTextBlock* Result = MakeText(Tree, Name, TEXT("-"), 9);
			Place(Parent, Result, FVector2D(X + LabelWidth, Y), FVector2D(ValueWidth, 17.0f));
			return Result;
		};

		UCanvasPanel* Info = Page(TEXT("InfoPage"));
		Section(Info, TEXT("GuildInfoSection"), TEXT("Guild Information"), FVector2D(5.0f, 10.0f), FVector2D(167.0f, 274.0f));
		Section(Info, TEXT("GuildMarkSection"), TEXT("Guild Mark"), FVector2D(188.0f, 10.0f), FVector2D(167.0f, 69.0f));
		Section(Info, TEXT("EnemyGuildSection"), TEXT("Enemy Guilds"), FVector2D(188.0f, 85.0f), FVector2D(167.0f, 199.0f));
		Value(Info, TEXT("GuildNameText"), TEXT("Name"), 12.0f, 43.0f, 65.0f, 86.0f);
		Value(Info, TEXT("GuildMasterText"), TEXT("Master"), 12.0f, 69.0f, 65.0f, 86.0f);
		Value(Info, TEXT("GuildLevelText"), TEXT("Level"), 12.0f, 105.0f, 65.0f, 45.0f);
		Value(Info, TEXT("GuildExperienceText"), TEXT("Experience"), 12.0f, 131.0f, 65.0f, 86.0f);
		Value(Info, TEXT("MemberCountText"), TEXT("Members"), 12.0f, 157.0f, 65.0f, 86.0f);
		Value(Info, TEXT("AverageLevelText"), TEXT("Average level"), 12.0f, 183.0f, 95.0f, 44.0f);
		Value(Info, TEXT("GuildYangText"), TEXT("Guild Yang"), 12.0f, 219.0f, 65.0f, 86.0f);
		UBorder* Mark = Make<UBorder>(Tree, TEXT("GuildMarkSlot")); Mark->SetBrushColor(FLinearColor(0.02f,0.02f,0.02f,0.9f));
		Place(Info, Mark, FVector2D(195.0f, 37.0f), FVector2D(49.0f, 37.0f));
		UImage* GuildMarkImage = Make<UImage>(Tree, TEXT("GuildMarkImage"));
		Place(Info, GuildMarkImage, FVector2D(196.0f, 37.0f), FVector2D(48.0f, 36.0f));
		Place(Info, MakeMT2Button(Tree, TEXT("UploadMarkButton"), TEXT("Upload Mark"), EMT2GeneratedButtonSize::Large), FVector2D(260.0f, 34.0f), FVector2D(88.0f, 21.0f));
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Value(Info, *FString::Printf(TEXT("EnemyGuild%dText"), Index + 1), TEXT(""), 194.0f, 112.0f + Index * 26.0f, 0.0f, 118.0f);
			Place(Info, MakeMT2Button(Tree, *FString::Printf(TEXT("EnemyGuildCancel%d"), Index + 1), TEXT("Cancel"), EMT2GeneratedButtonSize::Small), FVector2D(310.0f, 110.0f + Index * 26.0f), FVector2D(43.0f, 21.0f));
		}

		UCanvasPanel* BoardPage = Page(TEXT("BoardPage"));
		Place(BoardPage, MakeText(Tree, TEXT("BoardIdHeader"), TEXT("Name"), 9), FVector2D(15.0f, 8.0f), FVector2D(90.0f, 18.0f));
		Place(BoardPage, MakeText(Tree, TEXT("BoardMessageHeader"), TEXT("Message"), 9), FVector2D(104.0f, 8.0f), FVector2D(220.0f, 18.0f));
		UScrollBox* NoticeList = Make<UScrollBox>(Tree, TEXT("NoticeList")); Place(BoardPage, NoticeList, FVector2D(15.0f, 28.0f), FVector2D(325.0f, 232.0f));
		UEditableTextBox* NoticeInput = Make<UEditableTextBox>(Tree, TEXT("NoticeInput"));
		PlaceMT2EditLine(Tree, BoardPage, NoticeInput, FVector2D(15.0f, 269.0f), FVector2D(315.0f, 18.0f), FAnchors(0), FVector2D::ZeroVector);
		Place(BoardPage, MakeMT2Button(Tree, TEXT("PostNoticeButton"), TEXT(">"), EMT2GeneratedButtonSize::Small), FVector2D(330.0f, 267.0f), FVector2D(25.0f, 21.0f));

		UCanvasPanel* Members = Page(TEXT("MemberPage"));
		const TCHAR* MemberHeaders[] = {TEXT("Name"), TEXT("Rank"), TEXT("Job"), TEXT("Level"), TEXT("Offer"), TEXT("Knight")};
		const float MemberX[] = {10, 108, 172, 210, 248, 296};
		const float MemberW[] = {98, 64, 38, 38, 48, 44};
		for (int32 Index = 0; Index < 6; ++Index) Place(Members, MakeText(Tree, *FString::Printf(TEXT("MemberHeader%d"), Index), MemberHeaders[Index], 8), FVector2D(MemberX[Index], 5.0f), FVector2D(MemberW[Index], 18.0f));
		UScrollBox* MemberScroll = Make<UScrollBox>(Tree, TEXT("MemberScroll")); Place(Members, MemberScroll, FVector2D(10.0f, 25.0f), FVector2D(335.0f, 260.0f));
		UVerticalBox* MemberList = Make<UVerticalBox>(Tree, TEXT("MemberList")); MemberScroll->AddChild(MemberList);

		UCanvasPanel* Ranks = Page(TEXT("RankPage"));
		const TCHAR* RankHeaders[] = {TEXT("No."), TEXT("Rank"), TEXT("Invite"), TEXT("Kick"), TEXT("Notice"), TEXT("Skill")};
		for (int32 Index = 0; Index < 6; ++Index) Place(Ranks, MakeText(Tree, *FString::Printf(TEXT("RankHeader%d"), Index), RankHeaders[Index], 8), FVector2D(8.0f + Index * 57.0f, 4.0f), FVector2D(55.0f, 18.0f));
		for (int32 Row = 0; Row < 15; ++Row)
		{
			const float Y = 24.0f + Row * 17.0f;
			Place(Ranks, MakeText(Tree, *FString::Printf(TEXT("RankNumber%d"), Row + 1), FString::FromInt(Row + 1), 8), FVector2D(9.0f, Y), FVector2D(40.0f, 16.0f));
			Place(Ranks, MakeText(Tree, *FString::Printf(TEXT("RankName%d"), Row + 1), Row == 0 ? TEXT("Master") : TEXT("Member"), 8), FVector2D(55.0f, Y), FVector2D(70.0f, 16.0f));
			for (int32 Flag = 0; Flag < 4; ++Flag)
			{
				UCheckBox* Check = Make<UCheckBox>(Tree, *FString::Printf(TEXT("Rank%dPermission%d"), Row + 1, Flag));
				Place(Ranks, Check, FVector2D(143.0f + Flag * 57.0f, Y), FVector2D(16.0f));
			}
		}

		UCanvasPanel* Skills = Page(TEXT("SkillPage"));
		Section(Skills, TEXT("PassiveSkillSection"), TEXT("Passive Skills"), FVector2D(20.0f, 20.0f), FVector2D(320.0f, 58.0f));
		Section(Skills, TEXT("ActiveSkillSection"), TEXT("Active Skills"), FVector2D(20.0f, 87.0f), FVector2D(320.0f, 58.0f));
		Section(Skills, TEXT("AffectSkillSection"), TEXT("Active Effects"), FVector2D(20.0f, 154.0f), FVector2D(320.0f, 91.0f));
		for (int32 Row = 0; Row < 4; ++Row)
		{
			const int32 Count = Row < 2 ? 9 : 9;
			const float Y = Row == 0 ? 43.0f : Row == 1 ? 110.0f : 177.0f + (Row - 2) * 32.0f;
			for (int32 Col = 0; Col < Count; ++Col)
			{
				UBorder* Slot = Make<UBorder>(Tree, *FString::Printf(TEXT("GuildSkillSlot%d_%d"), Row, Col));
				Slot->SetBrushColor(FLinearColor(0.015f, 0.015f, 0.015f, 0.92f));
				Place(Skills, Slot, FVector2D(36.0f + Col * 32.0f, Y), FVector2D(32.0f));
			}
		}
		Place(Skills, MakeText(Tree, TEXT("GuildPowerLabel"), TEXT("Dragon God Power"), 9), FVector2D(20.0f, 263.0f), FVector2D(130.0f, 18.0f));
		UProgressBar* Power = Make<UProgressBar>(Tree, TEXT("GuildPowerGauge")); Power->SetPercent(1.0f); Place(Skills, Power, FVector2D(145.0f, 264.0f), FVector2D(130.0f, 14.0f));

		UCanvasPanel* BaseInfo = Page(TEXT("BaseInfoPage"));
		Section(BaseInfo, TEXT("GuildBaseNameSection"), TEXT("Guild Land"), FVector2D(12.0f, 10.0f), FVector2D(336.0f, 42.0f));
		Place(BaseInfo, MakeText(Tree, TEXT("GuildBaseNameText"), TEXT("No guild land"), 10), FVector2D(65.0f, 33.0f), FVector2D(230.0f, 18.0f));
		Section(BaseInfo, TEXT("GuildResourcesSection"), TEXT("Resources"), FVector2D(12.0f, 64.0f), FVector2D(336.0f, 70.0f));
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Place(BaseInfo, MakeText(Tree, *FString::Printf(TEXT("ResourceName%d"), Index + 1), FString::Printf(TEXT("R%d"), Index + 1), 8), FVector2D(24.0f + Index * 52.0f, 88.0f), FVector2D(45.0f, 16.0f));
			Place(BaseInfo, MakeText(Tree, *FString::Printf(TEXT("ResourceValue%d"), Index + 1), TEXT("0"), 8), FVector2D(24.0f + Index * 52.0f, 106.0f), FVector2D(45.0f, 16.0f));
		}
		Section(BaseInfo, TEXT("GuildBuildingsSection"), TEXT("Buildings"), FVector2D(12.0f, 146.0f), FVector2D(336.0f, 130.0f));
		Place(BaseInfo, MakeText(Tree, TEXT("BuildingNameHeader"), TEXT("Building"), 8), FVector2D(22.0f, 171.0f), FVector2D(100.0f, 16.0f));
		Place(BaseInfo, MakeText(Tree, TEXT("BuildingGradeHeader"), TEXT("Grade"), 8), FVector2D(132.0f, 171.0f), FVector2D(60.0f, 16.0f));
		Place(BaseInfo, MakeText(Tree, TEXT("BuildingOperateHeader"), TEXT("Operate"), 8), FVector2D(270.0f, 171.0f), FVector2D(65.0f, 16.0f));

		Place(Guild, MakeMT2Button(Tree, TEXT("LeaveGuildButton"), TEXT("Leave"), EMT2GeneratedButtonSize::Large), FVector2D(174.0f, 263.0f), FVector2D(88.0f, 21.0f));
		Place(Guild, MakeMT2Button(Tree, TEXT("DisbandGuildButton"), TEXT("Disband"), EMT2GeneratedButtonSize::Large), FVector2D(267.0f, 263.0f), FVector2D(88.0f, 21.0f));

		UMT2AtlasImage* Tabs = Make<UMT2AtlasImage>(Tree, TEXT("GuildTabImage"));
		Tabs->SetAtlas(FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("locale_en_ui_guild_T_guild"))), FMT2AtlasRect(0, 0, 376, 37));
		Tabs->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Root, Tabs, FVector2D(0.0f, 319.0f), FVector2D(376.0f, 37.0f));
		const TCHAR* TabNames[] = {TEXT("InfoTabButton"), TEXT("BoardTabButton"), TEXT("MemberTabButton"), TEXT("RankTabButton"), TEXT("SkillTabButton"), TEXT("BaseInfoTabButton")};
		const float TabX[] = {6, 61, 130, 192, 254, 316};
		const float TabW[] = {53, 67, 60, 60, 60, 55};
		for (int32 Index = 0; Index < 6; ++Index)
		{
			UButton* Button = MakeButton(Tree, TabNames[Index]);
			FButtonStyle Style = Button->GetStyle();
			FSlateBrush Empty; Empty.DrawAs = ESlateBrushDrawType::NoDrawType;
			Style.SetNormal(Empty); Style.SetHovered(Empty); Style.SetPressed(Empty); Button->SetStyle(Style);
			Place(Root, Button, FVector2D(TabX[Index], 324.0f), FVector2D(TabW[Index], 27.0f));
		}
		return CompileAndSave(BP);
	}

	bool BuildGuildInviteDialog()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2GuildInviteDialog"), UMT2GuildInviteDialogWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("GuildInviteRoot")); Tree->RootWidget = Root;
		UWidget* Board = MakeChild(Tree, TEXT("MT2Board"), UMT2BoardWidget::StaticClass(), TEXT("BoardWidget"));
		if (!Board) return false;
		Place(Root, Board, FVector2D::ZeroVector, FVector2D(280.0f, 120.0f));
		Place(Root, MakeText(Tree, TEXT("DialogTitleText"), TEXT("Guild Invitation"), 10), FVector2D(12.0f, 10.0f), FVector2D(256.0f, 18.0f));
		UTextBlock* Message = MakeText(Tree, TEXT("MessageText"), TEXT("Player invited you to join Guild."), 9);
		Message->SetAutoWrapText(true);
		Place(Root, Message, FVector2D(22.0f, 37.0f), FVector2D(236.0f, 38.0f));
		Place(Root, MakeMT2Button(Tree, TEXT("AcceptButton"), TEXT("Accept"), EMT2GeneratedButtonSize::Large), FVector2D(46.0f, 84.0f), FVector2D(88.0f, 21.0f));
		Place(Root, MakeMT2Button(Tree, TEXT("DeclineButton"), TEXT("Decline"), EMT2GeneratedButtonSize::Large), FVector2D(146.0f, 84.0f), FVector2D(88.0f, 21.0f));
		return CompileAndSave(BP);
	}

	bool BuildMinimap()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2Minimap"), UMT2MinimapWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("MinimapRoot")); Tree->RootWidget = Root;
		UCanvasPanel* Open = Make<UCanvasPanel>(Tree, TEXT("OpenWindow"));
		Place(Root, Open, FVector2D(44,0), FVector2D(136,137));

		URetainerBox* Retainer = Make<URetainerBox>(Tree, TEXT("MapRetainer"));
		Retainer->SetEffectMaterial(BuildMinimapCircleMaterial());
		Retainer->SetTextureParameter(TEXT("Texture"));
		Retainer->SetRenderingPhase(0, 1);
		Place(Open, Retainer, FVector2D(4,5), FVector2D(128));
		UCanvasPanel* Clip = Make<UCanvasPanel>(Tree, TEXT("MapClip"));
		Clip->SetClipping(EWidgetClipping::ClipToBoundsAlways);
		Retainer->AddChild(Clip);
		UCanvasPanel* Map = Make<UCanvasPanel>(Tree, TEXT("MapCanvas"));
		Place(Clip, Map, FVector2D::ZeroVector, FVector2D(128));

		UTexture2D* Atlas = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("ymir_work_ui_T_minimap")));
		UMT2AtlasImage* Frame = Make<UMT2AtlasImage>(Tree, TEXT("MinimapFrame"));
		Frame->SetAtlas(Atlas, FMT2AtlasRect(0,0,136,137));
		Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Open, Frame, FVector2D::ZeroVector, FVector2D(136,137));
		UMT2AtlasImage* PlayerArrow = Make<UMT2AtlasImage>(Tree, TEXT("PlayerArrow"));
		PlayerArrow->SetAtlas(Atlas, FMT2AtlasRect(240,152,10,11));
		PlayerArrow->SetRenderTransformPivot(FVector2D(0.5f));
		PlayerArrow->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Open, PlayerArrow, FVector2D(63,64), FVector2D(10,11));

		auto AtlasButton = [Tree, Open, Atlas](const TCHAR* Name, FVector2D Position, const FMT2AtlasRegion& Normal,
			const FMT2AtlasRegion& Hovered, const FMT2AtlasRegion& Pressed, FVector2D Size)
		{
			UMT2AtlasButton* Button = Make<UMT2AtlasButton>(Tree, Name);
			SetButtonAtlas(Button, Atlas, Normal, Hovered, Pressed);
			Place(Open, Button, Position, Size);
		};
		AtlasButton(TEXT("ScaleUpButton"), FVector2D(101,116), FMT2AtlasRegion(226,120,241,135),
			FMT2AtlasRegion(240,137,255,152), FMT2AtlasRegion(241,120,256,135), FVector2D(15));
		AtlasButton(TEXT("ScaleDownButton"), FVector2D(115,103), FMT2AtlasRegion(181,120,196,135),
			FMT2AtlasRegion(211,120,226,135), FMT2AtlasRegion(196,120,211,135), FVector2D(15));
		AtlasButton(TEXT("HideButton"), FVector2D(111,6), FMT2AtlasRegion(150,209,169,229),
			FMT2AtlasRegion(188,209,207,229), FMT2AtlasRegion(169,209,188,229), FVector2D(19,20));
		AtlasButton(TEXT("FullMapButton"), FVector2D(12,12), FMT2AtlasRegion(136,120,151,135),
			FMT2AtlasRegion(166,120,181,135), FMT2AtlasRegion(151,120,166,135), FVector2D(15));

		UCanvasPanel* Closed = Make<UCanvasPanel>(Tree, TEXT("CloseWindow"));
		Place(Root, Closed, FVector2D(48,0), FVector2D(132,48));
		UMT2AtlasButton* Show = Make<UMT2AtlasButton>(Tree, TEXT("ShowButton"));
		SetButtonAtlas(Show, Atlas, FMT2AtlasRegion(0,177,32,209),
			FMT2AtlasRegion(0,177,32,209), FMT2AtlasRegion(0,177,32,209));
		Place(Closed, Show, FVector2D(100,4), FVector2D(32));
		UTextBlock* Location = MakeText(Tree, TEXT("LocationText"),
			TEXT("Unknown Map  CH 1\nX 0  Y 0  Z 0"), 9);
		Location->SetJustification(ETextJustify::Center);
		Location->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.85f, 0.45f)));
		Place(Root, Location, FVector2D(0,137), FVector2D(180,28));
		return CompileAndSave(BP);
	}

	bool BuildFullMap()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2FullMap"), UMT2FullMapWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, TEXT("FullMapRoot")); Tree->RootWidget = Root;
		UImage* Background = Make<UImage>(Tree, TEXT("Background"));
		SetImageTexture(Background, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_base")), true, true);
		Place(Root, Background, FVector2D::ZeroVector, FVector2D(271,294));
		UTextBlock* Title = MakeText(Tree, TEXT("MapNameText"), TEXT("Zone Map"), 10);
		Place(Root, Title, FVector2D(20,7), FVector2D(231,20));
		UMT2AtlasButton* Close = Make<UMT2AtlasButton>(Tree, TEXT("CloseButton"));
		UTexture2D* Public = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
		SetButtonAtlas(Close, Public, FMT2AtlasRegion(25,425,40,440),
			FMT2AtlasRegion(40,425,55,440), FMT2AtlasRegion(55,425,70,440));
		Place(Root, Close, FVector2D(249,7), FVector2D(15));
		UCanvasPanel* Clip = Make<UCanvasPanel>(Tree, TEXT("MapClip"));
		Clip->SetClipping(EWidgetClipping::ClipToBoundsAlways);
		Place(Root, Clip, FVector2D(10,34), FVector2D(251,250));
		UCanvasPanel* Map = Make<UCanvasPanel>(Tree, TEXT("MapCanvas"));
		Place(Clip, Map, FVector2D::ZeroVector, FVector2D(251,250));
		return CompileAndSave(BP);
	}

	UCanvasPanel* BuildGatewayRoot(UWidgetTree* Tree, const TCHAR* RootName, const TCHAR* BackgroundName,
		const TCHAR* BackgroundPath)
	{
		UCanvasPanel* Root = Make<UCanvasPanel>(Tree, RootName);
		Tree->RootWidget = Root;
		UImage* Background = Make<UImage>(Tree, BackgroundName);
		SetImageTexture(Background, BackgroundPath);
		Place(Root, Background, FVector2D::ZeroVector, FVector2D::ZeroVector, FAnchors(0, 0, 1, 1))->SetOffsets(FMargin(0));
		UBorder* Shade = Make<UBorder>(Tree, TEXT("ScreenShade"));
		Shade->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.38f));
		Place(Root, Shade, FVector2D::ZeroVector, FVector2D::ZeroVector, FAnchors(0, 0, 1, 1))->SetOffsets(FMargin(0));
		return Root;
	}

	bool BuildLogin()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2Login"), UMT2LoginWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = BuildGatewayRoot(Tree, TEXT("LoginRoot"), TEXT("LoginBackground"),
			UMT2PathSettings::Path(TEXT("ymir_work_ui_T_intrologin")));
		const FAnchors Center(0.5f);
		const FVector2D CenterAlign(0.5f);
		UImage* Panel = Make<UImage>(Tree, TEXT("LoginPanel"));
		SetImageTexture(Panel, UMT2PathSettings::Path(TEXT("locale_en_ui_login_T_login")));
		Place(Root, Panel, FVector2D::ZeroVector, FVector2D(312, 146), Center, CenterAlign);
		UEditableTextBox* Username = Make<UEditableTextBox>(Tree, TEXT("UsernameBox"));
		Username->SetHintText(FText::FromString(TEXT("Account name")));
		PlaceMT2EditLine(Tree, Root, Username, FVector2D(50, -35), FVector2D(180, 27), Center, CenterAlign, false);
		UEditableTextBox* Password = Make<UEditableTextBox>(Tree, TEXT("PasswordBox")); Password->SetIsPassword(true);
		Password->SetHintText(FText::FromString(TEXT("Password")));
		PlaceMT2EditLine(Tree, Root, Password, FVector2D(50, 5), FVector2D(180, 27), Center, CenterAlign, false);
		Place(Root, MakeMT2Button(Tree, TEXT("LoginButton"), TEXT("Login"), EMT2GeneratedButtonSize::Large),
			FVector2D(-68, 43), FVector2D(132, 32), Center, CenterAlign);
		Place(Root, MakeMT2Button(Tree, TEXT("RegisterButton"), TEXT("Register"), EMT2GeneratedButtonSize::Large),
			FVector2D(68, 43), FVector2D(132, 32), Center, CenterAlign);
		Place(Root, MakeText(Tree, TEXT("StatusText"), TEXT(""), 10), FVector2D(0, 98), FVector2D(420, 38), Center, CenterAlign);
		return CompileAndSave(BP);
	}

	bool BuildAccountRegistration()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2AccountRegistration"),
			UMT2AccountRegistrationWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = BuildGatewayRoot(Tree, TEXT("RegistrationRoot"), TEXT("RegistrationBackground"),
			UMT2PathSettings::Path(TEXT("ymir_work_ui_T_intrologin")));
		const FAnchors Center(0.5f);
		const FVector2D CenterAlign(0.5f);
		UBorder* Panel = Make<UBorder>(Tree, TEXT("RegistrationPanel"));
		Panel->SetBrushColor(FLinearColor(0.025f, 0.02f, 0.015f, 0.94f));
		Place(Root, Panel, FVector2D::ZeroVector, FVector2D(460, 390), Center, CenterAlign);
		Place(Root, MakeText(Tree, TEXT("RegistrationTitle"), TEXT("Create Account"), 24),
			FVector2D(0, -155), FVector2D(400, 42), Center, CenterAlign);

		auto AddTextBox = [Tree, Root, Center, CenterAlign](const TCHAR* LabelName, const TCHAR* Label,
			const TCHAR* BoxName, const TCHAR* Hint, float Y, bool bPassword)
		{
			Place(Root, MakeText(Tree, LabelName, Label, 11), FVector2D(-150, Y),
				FVector2D(100, 24), Center, CenterAlign);
			UEditableTextBox* Box = Make<UEditableTextBox>(Tree, BoxName);
			Box->SetHintText(FText::FromString(Hint));
			Box->SetIsPassword(bPassword);
			PlaceMT2EditLine(Tree, Root, Box, FVector2D(55, Y), FVector2D(270, 27), Center, CenterAlign);
		};
		AddTextBox(TEXT("UsernameLabel"), TEXT("Account"), TEXT("UsernameBox"),
			TEXT("3-32 characters"), -85, false);
		AddTextBox(TEXT("PasswordLabel"), TEXT("Password"), TEXT("PasswordBox"),
			TEXT("8-128 characters"), -40, true);
		AddTextBox(TEXT("ConfirmPasswordLabel"), TEXT("Confirm"), TEXT("ConfirmPasswordBox"),
			TEXT("Repeat password"), 5, true);
		Place(Root, MakeMT2Button(Tree, TEXT("CreateAccountButton"), TEXT("Create Account"), EMT2GeneratedButtonSize::XLarge),
			FVector2D(-95, 76), FVector2D(170, 36), Center, CenterAlign);
		Place(Root, MakeMT2Button(Tree, TEXT("BackButton"), TEXT("Back"), EMT2GeneratedButtonSize::XLarge),
			FVector2D(95, 76), FVector2D(170, 36), Center, CenterAlign);
		Place(Root, MakeText(Tree, TEXT("StatusText"), TEXT(""), 10),
			FVector2D(0, 135), FVector2D(400, 44), Center, CenterAlign);
		return CompileAndSave(BP);
	}

	bool BuildLoadingScreen()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2LoadingScreen"), UMT2LoadingScreenWidget::StaticClass(), BP, Tree))
		{
			return false;
		}
		UCanvasPanel* Root = BuildGatewayRoot(Tree, TEXT("LoadingRoot"), TEXT("LoadingBackground"),
			UMT2PathSettings::Path(TEXT("ymir_work_ui_T_introloading")));
		const FAnchors Center(0.5f);
		const FVector2D CenterAlign(0.5f);
		UProgressBar* Progress = Make<UProgressBar>(Tree, TEXT("LoadingProgress"));
		Progress->SetIsMarquee(true);
		Progress->SetFillColorAndOpacity(FLinearColor(0.72f, 0.55f, 0.18f, 1.0f));
		Place(Root, Progress, FVector2D(0, 235), FVector2D(560, 15), Center, CenterAlign);
		UThrobber* Throbber = Make<UThrobber>(Tree, TEXT("LoadingThrobber"));
		Place(Root, Throbber, FVector2D(-145, 195), FVector2D(28), Center, CenterAlign);
		Place(Root, MakeText(Tree, TEXT("StatusText"), TEXT("Loading..."), 13),
			FVector2D(20, 195), FVector2D(290, 30), Center, CenterAlign);
		return CompileAndSave(BP);
	}

	bool BuildCharacterSelect()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2CharacterSelect"), UMT2CharacterSelectWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = BuildGatewayRoot(Tree, TEXT("CharacterSelectRoot"), TEXT("CharacterSelectBackground"),
			UMT2PathSettings::Path(TEXT("ymir_work_ui_T_introselect")));
		const FAnchors Center(0.5f);
		const FVector2D CenterAlign(0.5f);
		UBorder* Panel = Make<UBorder>(Tree, TEXT("CharacterSelectPanel")); Panel->SetBrushColor(FLinearColor(0.025f, 0.02f, 0.015f, 0.9f));
		Place(Root, Panel, FVector2D::ZeroVector, FVector2D(500, 510), Center, CenterAlign);
		Place(Root, MakeText(Tree, TEXT("CharacterSelectTitle"), TEXT("Select Character"), 24), FVector2D(0, -210), FVector2D(440, 42), Center, CenterAlign);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const float Y = -125.0f + Index * 67.0f;
			Place(Root, MakeMT2Button(Tree, *FString::Printf(TEXT("SlotButton%d"), Index + 1), FString(),
				EMT2GeneratedButtonSize::XLarge), FVector2D(0, Y), FVector2D(400, 52), Center, CenterAlign);
			UTextBlock* Name = MakeText(Tree, *FString::Printf(TEXT("SlotName%d"), Index + 1), TEXT("- empty -"), 13);
			Name->SetVisibility(ESlateVisibility::HitTestInvisible);
			Place(Root, Name, FVector2D(0, Y), FVector2D(380, 42), Center, CenterAlign);
		}
		Place(Root, MakeMT2Button(Tree, TEXT("StartButton"), TEXT("Start"), EMT2GeneratedButtonSize::XLarge), FVector2D(-105, 173), FVector2D(180, 36), Center, CenterAlign);
		Place(Root, MakeMT2Button(Tree, TEXT("CreateButton"), TEXT("Create"), EMT2GeneratedButtonSize::XLarge), FVector2D(105, 173), FVector2D(180, 36), Center, CenterAlign);
		Place(Root, MakeText(Tree, TEXT("StatusText"), TEXT(""), 10), FVector2D(0, 220), FVector2D(440, 32), Center, CenterAlign);
		return CompileAndSave(BP);
	}

	bool BuildCharacterCreate()
	{
		UWidgetBlueprint* BP; UWidgetTree* Tree;
		if (!BeginOrCreate(TEXT("MT2CharacterCreate"), UMT2CharacterCreateWidget::StaticClass(), BP, Tree)) return false;
		UCanvasPanel* Root = BuildGatewayRoot(Tree, TEXT("CharacterCreateRoot"), TEXT("CharacterCreateBackground"),
			UMT2PathSettings::Path(TEXT("ymir_work_ui_T_introempire")));
		const FAnchors Center(0.5f);
		const FVector2D CenterAlign(0.5f);
		UBorder* Panel = Make<UBorder>(Tree, TEXT("CharacterCreatePanel")); Panel->SetBrushColor(FLinearColor(0.025f, 0.02f, 0.015f, 0.9f));
		Place(Root, Panel, FVector2D::ZeroVector, FVector2D(650, 500), Center, CenterAlign);
		Place(Root, MakeText(Tree, TEXT("CharacterCreateTitle"), TEXT("Create Character"), 24), FVector2D(0, -210), FVector2D(580, 42), Center, CenterAlign);
		UEditableTextBox* NameBox = Make<UEditableTextBox>(Tree, TEXT("NameBox")); NameBox->SetHintText(FText::FromString(TEXT("Character name")));
		PlaceMT2EditLine(Tree, Root, NameBox, FVector2D(0, -150), FVector2D(340, 28), Center, CenterAlign);
		const TCHAR* RaceNames[] = {TEXT("WarriorButton"), TEXT("AssassinButton"), TEXT("SuraButton"), TEXT("ShamanButton")};
		const TCHAR* RaceLabels[] = {TEXT("Warrior"), TEXT("Assassin"), TEXT("Sura"), TEXT("Shaman")};
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Place(Root, MakeMT2Button(Tree, RaceNames[Index], RaceLabels[Index], EMT2GeneratedButtonSize::Middle), FVector2D(-225.0f + Index * 150.0f, -85), FVector2D(135, 34), Center, CenterAlign);
		}
		Place(Root, MakeMT2Button(Tree, TEXT("SexButton"), TEXT("Change Sex"), EMT2GeneratedButtonSize::Large), FVector2D(-100, -30), FVector2D(180, 34), Center, CenterAlign);
		Place(Root, MakeMT2Button(Tree, TEXT("StyleButton"), TEXT("Change Style"), EMT2GeneratedButtonSize::Large), FVector2D(100, -30), FVector2D(180, 34), Center, CenterAlign);
		Place(Root, MakeMT2Button(Tree, TEXT("EmpireRedButton"), TEXT("Shinsoo"), EMT2GeneratedButtonSize::Large), FVector2D(-190, 35), FVector2D(150, 38), Center, CenterAlign);
		Place(Root, MakeMT2Button(Tree, TEXT("EmpireYellowButton"), TEXT("Chunjo"), EMT2GeneratedButtonSize::Large), FVector2D(0, 35), FVector2D(150, 38), Center, CenterAlign);
		Place(Root, MakeMT2Button(Tree, TEXT("EmpireBlueButton"), TEXT("Jinno"), EMT2GeneratedButtonSize::Large), FVector2D(190, 35), FVector2D(150, 38), Center, CenterAlign);
		Place(Root, MakeText(Tree, TEXT("SelectionText"), TEXT("Warrior | Male | Shinsoo"), 12), FVector2D(0, 92), FVector2D(560, 32), Center, CenterAlign);
		Place(Root, MakeMT2Button(Tree, TEXT("CreateButton"), TEXT("Create"), EMT2GeneratedButtonSize::XLarge), FVector2D(-105, 153), FVector2D(180, 36), Center, CenterAlign);
		Place(Root, MakeMT2Button(Tree, TEXT("BackButton"), TEXT("Back"), EMT2GeneratedButtonSize::XLarge), FVector2D(105, 153), FVector2D(180, 36), Center, CenterAlign);
		Place(Root, MakeText(Tree, TEXT("StatusText"), TEXT(""), 10), FVector2D(0, 205), FVector2D(560, 32), Center, CenterAlign);
		return CompileAndSave(BP);
	}
}

UMT2GenerateUIBlueprintsCommandlet::UMT2GenerateUIBlueprintsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMT2GenerateUIBlueprintsCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("PartyOnly")))
	{
		const bool bSuccess = BuildPartyMember() && BuildPartyPanel() && BuildPartyInviteDialog();
		UE_LOG(LogTemp, Display, TEXT("MT2 party UI Blueprint generation: %s"),
			bSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("PatchAggression")))
	{
		const bool bSuccess = PatchSystemMenuAggression();
		UE_LOG(LogTemp, Display, TEXT("MT2 system menu aggression patch: %s"),
			bSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("PatchVoiceToggleMode")))
	{
		const bool bSuccess = PatchSystemMenuVoiceToggleMode();
		UE_LOG(LogTemp, Display, TEXT("MT2 system menu voice-mode patch: %s"),
			bSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("PatchLanguage")))
	{
		const bool bSuccess = PatchSystemMenuLanguage();
		UE_LOG(LogTemp, Display, TEXT("MT2 system menu language patch: %s"),
			bSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("SystemMenuOnly")))
	{
		const bool bSystemMenuSuccess = BuildSystemMenu();
		UE_LOG(LogTemp, Display, TEXT("MT2 system menu UI Blueprint generation: %s"),
			bSystemMenuSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bSystemMenuSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("DropDialogOnly")))
	{
		const bool bDropDialogSuccess = BuildItemDropDialog();
		UE_LOG(LogTemp, Display, TEXT("MT2 item drop dialog generation: %s"),
			bDropDialogSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bDropDialogSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("NameplateOnly")))
	{
		const bool bNameplateSuccess = BuildNameplate();
		UE_LOG(LogTemp, Display, TEXT("MT2 nameplate UI Blueprint generation: %s"),
			bNameplateSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bNameplateSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("InventoryCharacterOnly")))
	{
		const bool bInventoryCharacterSuccess =
			BuildInventorySlot() && BuildInventoryGrid() && BuildCharacterWindow();
		UE_LOG(LogTemp, Display, TEXT("MT2 inventory and character UI Blueprint generation: %s"),
			bInventoryCharacterSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bInventoryCharacterSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("TargetInfoOnly")))
	{
		const bool bTargetInfoSuccess = BuildTargetInfo() && BuildHUD();
		UE_LOG(LogTemp, Display, TEXT("MT2 target info UI Blueprint generation: %s"),
			bTargetInfoSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bTargetInfoSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("ChatOnly")))
	{
		const bool bChatSuccess = BuildChat();
		UE_LOG(LogTemp, Display, TEXT("MT2 chat UI Blueprint generation: %s"),
			bChatSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bChatSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("MinimapOnly")))
	{
		const bool bMinimapSuccess = BuildMinimap() && BuildFullMap() && BuildHUD();
		UE_LOG(LogTemp, Display, TEXT("MT2 minimap UI Blueprint generation: %s"),
			bMinimapSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bMinimapSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("GatewayOnly")))
	{
		const bool bGatewaySuccess = BuildLogin() && BuildAccountRegistration() &&
			BuildCharacterSelect() && BuildCharacterCreate() && BuildLoadingScreen();
		UE_LOG(LogTemp, Display, TEXT("MT2 gateway UI Blueprint generation: %s"),
			bGatewaySuccess ? TEXT("OK") : TEXT("FAILED"));
		return bGatewaySuccess ? 0 : 1;
	}
	// -Messenger regenerates only the messenger window, leaving every other UI asset alone.
	if (FParse::Param(*Params, TEXT("Messenger")))
	{
		const bool bMessengerSuccess = BuildMessenger() && BuildWhisper() && BuildNotifications() && BuildFriendRequestDialog() && BuildFriendAddDialog();
		UE_LOG(LogTemp, Display, TEXT("MT2 messenger Blueprint generation: %s"),
			bMessengerSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bMessengerSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("Trade")))
	{
		const bool bSuccess = BuildTrade() && AttachTradeToHUD();
		UE_LOG(LogTemp, Display, TEXT("MT2 trade Blueprint generation: %s"), bSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bSuccess ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("Guild")))
	{
		const bool bSuccess = BuildGuild() && BuildGuildInviteDialog();
		UE_LOG(LogTemp, Display, TEXT("MT2 guild Blueprint generation: %s"), bSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bSuccess ? 0 : 1;
	}

	const bool bFocusedOnly = FParse::Param(*Params, TEXT("FocusedOnly"));
	if (bFocusedOnly)
	{
		const bool bFocusedSuccess = BuildInventorySlot() && BuildLogin() && BuildAccountRegistration() &&
			BuildCharacterSelect() && BuildCharacterCreate() && BuildLoadingScreen();
		UE_LOG(LogTemp, Display, TEXT("MT2 focused UI Blueprint generation: %s"), bFocusedSuccess ? TEXT("OK") : TEXT("FAILED"));
		return bFocusedSuccess ? 0 : 1;
	}

	const bool bSuccess =
		BuildBoard() &&
		BuildTitleBar() &&
		BuildInventorySlot() &&
		BuildQuickSlot() &&
		BuildCurrencyPanel() &&
		BuildResourceGauge() &&
		BuildExperienceGauge() &&
		BuildNameplate() &&
		BuildInventoryGrid() &&
		BuildEquipmentPanel() &&
		BuildQuickSlotBar() &&
		BuildInventory() &&
		BuildTaskBar() &&
		BuildCharacterWindow() &&
		BuildItemDropDialog() &&
		BuildSystemMenu() &&
		BuildRespawn() &&
		BuildChat() &&
		BuildTargetInfo() &&
		BuildMinimap() &&
		BuildFullMap() &&
		BuildHUD() &&
		BuildMessenger() &&
		BuildWhisper() &&
		BuildNotifications() &&
		BuildFriendRequestDialog() &&
		BuildFriendAddDialog() &&
		BuildGuild() &&
		BuildGuildInviteDialog() &&
		BuildLogin() &&
		BuildAccountRegistration() &&
		BuildCharacterSelect() &&
		BuildCharacterCreate() &&
		BuildLoadingScreen();

	UE_LOG(LogTemp, Display, TEXT("MT2 UI Blueprint generation: %s"), bSuccess ? TEXT("OK") : TEXT("FAILED"));
	return bSuccess ? 0 : 1;
}

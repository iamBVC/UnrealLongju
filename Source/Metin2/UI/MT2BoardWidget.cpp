/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2BoardWidget.h"
#include "Config/MT2PathSettings.h"

#include "Components/Image.h"
#include "UI/MT2UIStyle.h"

namespace
{
	void ApplyTexture(UImage* Image, const TCHAR* Path)
	{
		if (Image)
		{
			Image->SetBrush(FMT2UIStyle::TextureBrush(FMT2UIStyle::LoadTexture(Path)));
		}
	}
}

void UMT2BoardWidget::InitializeBoard(const FVector2D& InSize)
{
	SetDesiredSizeInViewport(InSize);
}

void UMT2BoardWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyVisuals();
}

void UMT2BoardWidget::ApplyVisuals()
{
	ApplyTexture(BoardBase, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_base")));
	ApplyTexture(BoardTop, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_line_top")));
	ApplyTexture(BoardBottom, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_line_bottom")));
	ApplyTexture(BoardLeft, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_line_left")));
	ApplyTexture(BoardRight, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_line_right")));
	ApplyTexture(BoardLT, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_corner_lefttop")));
	ApplyTexture(BoardRT, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_corner_righttop")));
	ApplyTexture(BoardLB, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_corner_leftbottom")));
	ApplyTexture(BoardRB, UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_board_corner_rightbottom")));
}

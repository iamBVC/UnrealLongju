/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2BoardWidget.h"

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
	ApplyTexture(BoardBase, TEXT("/Game/ymir_work/ui/pattern/T_board_base.T_board_base"));
	ApplyTexture(BoardTop, TEXT("/Game/ymir_work/ui/pattern/T_board_line_top.T_board_line_top"));
	ApplyTexture(BoardBottom, TEXT("/Game/ymir_work/ui/pattern/T_board_line_bottom.T_board_line_bottom"));
	ApplyTexture(BoardLeft, TEXT("/Game/ymir_work/ui/pattern/T_board_line_left.T_board_line_left"));
	ApplyTexture(BoardRight, TEXT("/Game/ymir_work/ui/pattern/T_board_line_right.T_board_line_right"));
	ApplyTexture(BoardLT, TEXT("/Game/ymir_work/ui/pattern/T_board_corner_lefttop.T_board_corner_lefttop"));
	ApplyTexture(BoardRT, TEXT("/Game/ymir_work/ui/pattern/T_board_corner_righttop.T_board_corner_righttop"));
	ApplyTexture(BoardLB, TEXT("/Game/ymir_work/ui/pattern/T_board_corner_leftbottom.T_board_corner_leftbottom"));
	ApplyTexture(BoardRB, TEXT("/Game/ymir_work/ui/pattern/T_board_corner_rightbottom.T_board_corner_rightbottom"));
}

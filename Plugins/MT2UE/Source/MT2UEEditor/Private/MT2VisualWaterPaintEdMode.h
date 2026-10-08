#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"
#include "ScopedTransaction.h"
#include "MT2VisualWaterPaintData.h"

class FMT2VisualWaterPaintEdMode : public FEdMode
{
public:
	static const FEditorModeID ModeId;
	virtual void Enter() override;
	virtual void Exit() override;
	virtual bool UsesToolkits() const override { return true; }
	virtual bool UsesTransformWidget() const override { return false; }
	virtual bool CanAutoSave() const override { return !Transaction.IsValid(); }
	virtual bool MouseMove(FEditorViewportClient*, FViewport*, int32, int32) override;
	virtual bool CapturedMouseMove(FEditorViewportClient*, FViewport*, int32, int32) override;
	virtual bool InputKey(FEditorViewportClient*, FViewport*, FKey, EInputEvent) override;
	virtual bool LostFocus(FEditorViewportClient*, FViewport*) override;
	virtual void Tick(FEditorViewportClient*, float) override;
	virtual void Render(const FSceneView*, FViewport*, FPrimitiveDrawInterface*) override;
	virtual void PostUndo() override { Refresh(); }
	void UseSelectedMap();
	void PickHeight();
	void Refresh();
	FText Status() const;
	double Radius = 400;
	float Height = 0;
	bool bErase = false;
	bool bWaterPlane = true;
private:
	void SelectTarget(AMT2MapPresentationActor*);
	void UpdateCursor(FEditorViewportClient*);
	void Paint();
	bool Flush();
	void FinishStroke();
	TWeakObjectPtr<AMT2MapPresentationActor> Target;
	FMT2VisualWaterGrid Grid;
	FString Error;
	FVector Cursor = FVector::ZeroVector;
	FVector2D LastPaint = FVector2D::ZeroVector;
	bool bCursorValid = false;
	bool bStrokeErase = false;
	float StrokeHeight = 0;
	double StrokeRadius = 0, LastFlushTime = 0;
	FIntPoint InitialGridSize = FIntPoint::ZeroValue;
	FIntRect PendingDirty = FIntRect(0, 0, 0, 0);
	TMap<int32, float> Before;
	TUniquePtr<FScopedTransaction> Transaction;
};

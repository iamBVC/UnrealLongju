#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"
#include "ScopedTransaction.h"

class AMT2MapPresentationActor;
class ALandscapeProxy;
class UMaterial;

class FMT2AreaPaintEdMode : public FEdMode
{
public:
	static const FEditorModeID ModeId;
	virtual void Enter() override;
	virtual void Exit() override;
	virtual bool UsesToolkits() const override { return true; }
	virtual bool UsesTransformWidget() const override { return false; }
	virtual bool IsSelectionAllowed(AActor*, bool) const override { return false; }
	virtual bool CanAutoSave() const override { return !Transaction.IsValid(); }
	virtual bool MouseMove(FEditorViewportClient*, FViewport*, int32, int32) override;
	virtual bool CapturedMouseMove(FEditorViewportClient*, FViewport*, int32, int32) override;
	virtual bool InputKey(FEditorViewportClient*, FViewport*, FKey, EInputEvent) override;
	virtual bool LostFocus(FEditorViewportClient*, FViewport*) override;
	virtual void Tick(FEditorViewportClient*, float) override;
	virtual void Render(const FSceneView*, FViewport*, FPrimitiveDrawInterface*) override;
	virtual void AddReferencedObjects(FReferenceCollector&) override;
	virtual void PostUndo() override { RefreshPreview(); }
	void RefreshPreview() { Preview.Reset(); bPreviewBuilt = false; bPreviewFlagsDirty = true; }
	FText Status() const;
	int32 Bit = 0;
	uint8 VisibleBits = 0xff;
	double Radius = 150;
	bool bErase = false;
	bool bShowOverlay = true;
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FMT2AreaPreviewMaterialTest;
#endif
	void UpdateCursor(FEditorViewportClient*, FViewport*);
	void Paint();
	void FinishStroke();
	void CreatePreviewMaterial();
	struct FPreviewCell { FIntRect Cells; FVector Corners[4]; uint8 Flags = 0; };
	TArray<FPreviewCell> Preview;
	FIntRect PreviewRect;
	int32 PreviewStep = 0;
	FIntPoint PreviewSize = FIntPoint::ZeroValue;
	uint32 PreviewTerrainHash = 0;
	bool bPreviewBuilt = false;
	bool bPreviewFlagsDirty = true;
	TWeakObjectPtr<AMT2MapPresentationActor> Target;
	TWeakObjectPtr<ALandscapeProxy> Landscape;
	TWeakObjectPtr<AMT2MapPresentationActor> StrokeTarget;
	FIntPoint StrokeSize;
	TUniquePtr<FScopedTransaction> Transaction;
	TMap<int32, uint8> Before;
	FVector Cursor = FVector::ZeroVector;
	FVector2D LastPaint = FVector2D::ZeroVector;
	bool bCursorValid = false;
	bool bStrokeErase = false;
	int32 StrokeBit = 0;
	TObjectPtr<UMaterial> PreviewMaterial = nullptr;
};

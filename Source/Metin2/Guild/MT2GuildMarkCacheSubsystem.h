/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "MT2GuildMarkCacheSubsystem.generated.h"

class UTexture2D;

UCLASS()
class METIN2_API UMT2GuildMarkCacheSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UTexture2D* GetMark(int32 GuildId, int64 Revision) const;
	UTexture2D* GetMark(int32 GuildId) const;
	void StoreMark(int32 GuildId, int64 Revision, const TArray<uint8>& PngData);
	bool NeedsRequest(int32 GuildId, int64 Revision);

private:
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UTexture2D>> Textures;
	TMap<int32, int64> Revisions;
	TMap<int32, int64> RequestedRevisions;
};

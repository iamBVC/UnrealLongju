/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Guild/MT2GuildMarkCacheSubsystem.h"

#include "Engine/Texture2D.h"
#include "ImageUtils.h"

UTexture2D* UMT2GuildMarkCacheSubsystem::GetMark(int32 GuildId, int64 Revision) const
{
	const int64* CachedRevision = Revisions.Find(GuildId);
	const TObjectPtr<UTexture2D>* Texture = Textures.Find(GuildId);
	return CachedRevision && *CachedRevision == Revision && Texture ? Texture->Get() : nullptr;
}

UTexture2D* UMT2GuildMarkCacheSubsystem::GetMark(int32 GuildId) const
{
	const TObjectPtr<UTexture2D>* Texture = Textures.Find(GuildId);
	return Texture ? Texture->Get() : nullptr;
}

void UMT2GuildMarkCacheSubsystem::StoreMark(int32 GuildId, int64 Revision, const TArray<uint8>& PngData)
{
	if (GuildId <= 0 || Revision <= 0 || PngData.IsEmpty()) return;
	if (UTexture2D* Texture = FImageUtils::ImportBufferAsTexture2D(PngData))
	{
		Texture->NeverStream = true;
		Texture->Filter = TF_Nearest;
		Texture->UpdateResource();
		Textures.Add(GuildId, Texture);
		Revisions.Add(GuildId, Revision);
		RequestedRevisions.Remove(GuildId);
	}
}

bool UMT2GuildMarkCacheSubsystem::NeedsRequest(int32 GuildId, int64 Revision)
{
	if (GuildId <= 0 || (Revision > 0 ? GetMark(GuildId, Revision) : GetMark(GuildId))) return false;
	if (const int64* Requested = RequestedRevisions.Find(GuildId); Requested && *Requested == Revision) return false;
	RequestedRevisions.Add(GuildId, Revision);
	return true;
}

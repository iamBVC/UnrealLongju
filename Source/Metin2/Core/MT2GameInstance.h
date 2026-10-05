/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MT2GameInstance.generated.h"

UCLASS(Blueprintable)
class METIN2_API UMT2GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void OnStart() override;
	virtual void Shutdown() override;

private:
	void HandleApplicationActivationChanged(bool bIsActive);

	// A cooked/Shipping client does NOT treat a bare "host:port" command-line argument as a connect
	// URL the way the editor's "-game" launch does - it just loads GameDefaultMap standalone, so the
	// login RPC runs locally where IsGateway() is false ("Gateway is unavailable or busy"). This
	// pulls the gateway address from the command line (explicit -connect=<addr>, else the first bare
	// host:port token) so the client connects on startup exactly like the editor client does.
	static FString ResolveStartupConnectAddress();

	FDelegateHandle ApplicationActivationHandle;
	float ForegroundFrameRateLimit = 0.0f;
	bool bBackgroundFrameRateLimitActive = false;
};

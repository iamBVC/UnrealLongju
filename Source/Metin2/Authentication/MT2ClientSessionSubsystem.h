/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Server/MT2ServerRuntimeTypes.h"
#include "MT2ClientSessionSubsystem.generated.h"

UENUM(BlueprintType)
enum class EMT2ClientSessionState : uint8
{
	Disconnected,
	ConnectingGateway,
	GatewayConnected,
	Authenticating,
	CharacterSelection,
	TravelingToMap,
	InWorld,
	Failed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2ClientSessionStateSignature, EMT2ClientSessionState, State, const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMT2ClientCharacterListSignature, const TArray<FMT2CharacterSummary>&, Characters);

UCLASS()
class METIN2_API UMT2ClientSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "MT2|Session")
	EMT2ClientSessionState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "MT2|Session")
	const TArray<FMT2CharacterSummary>& GetCharacters() const { return Characters; }

	UFUNCTION(BlueprintPure, Category = "MT2|Session")
	const FString& GetLastError() const { return LastError; }

	UPROPERTY(BlueprintAssignable, Category = "MT2|Session")
	FMT2ClientSessionStateSignature OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "MT2|Session")
	FMT2ClientCharacterListSignature OnCharactersChanged;

	// Endpoint of the gateway this client authenticated through; the in-game system menu's
	// Disconnect button travels back here to land on the login screen again.
	void SetGatewayAddress(const FString& Address) { GatewayAddress = Address; }
	const FString& GetGatewayAddress() const { return GatewayAddress; }

	void MarkConnectingGateway();
	void MarkGatewayConnected();
	void MarkAuthenticating();
	void MarkLoginResult(bool bSucceeded, const FString& Error, const TArray<FMT2CharacterSummary>& InCharacters);
	void SetCharacters(const TArray<FMT2CharacterSummary>& InCharacters);
	void MarkTravelingToMap();
	void MarkInWorld();
	void MarkFailure(const FString& Error);

private:
	void SetState(EMT2ClientSessionState NewState, const FString& Error = FString());
	void HandleNetworkFailure(
		UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType,
		const FString& ErrorString);

	UPROPERTY(Transient)
	EMT2ClientSessionState State = EMT2ClientSessionState::Disconnected;

	UPROPERTY(Transient)
	TArray<FMT2CharacterSummary> Characters;

	UPROPERTY(Transient)
	FString LastError;

	UPROPERTY(Transient)
	FString GatewayAddress;

	FDelegateHandle NetworkFailureHandle;
};

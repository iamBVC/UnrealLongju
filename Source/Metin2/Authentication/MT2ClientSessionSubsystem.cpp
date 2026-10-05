/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Authentication/MT2ClientSessionSubsystem.h"

#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2ClientSession, Log, All);

void UMT2ClientSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(
			this, &UMT2ClientSessionSubsystem::HandleNetworkFailure);
	}
}

void UMT2ClientSessionSubsystem::Deinitialize()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
	}
	Super::Deinitialize();
}

void UMT2ClientSessionSubsystem::MarkConnectingGateway()
{
	Characters.Reset();
	OnCharactersChanged.Broadcast(Characters);
	SetState(EMT2ClientSessionState::ConnectingGateway);
}

void UMT2ClientSessionSubsystem::MarkGatewayConnected()
{
	SetState(EMT2ClientSessionState::GatewayConnected);
}

void UMT2ClientSessionSubsystem::MarkAuthenticating()
{
	SetState(EMT2ClientSessionState::Authenticating);
}

void UMT2ClientSessionSubsystem::MarkLoginResult(
	bool bSucceeded, const FString& Error, const TArray<FMT2CharacterSummary>& InCharacters)
{
	if (!bSucceeded)
	{
		SetState(EMT2ClientSessionState::GatewayConnected, Error);
		return;
	}
	SetCharacters(InCharacters);
	SetState(EMT2ClientSessionState::CharacterSelection, Error);
}

void UMT2ClientSessionSubsystem::SetCharacters(const TArray<FMT2CharacterSummary>& InCharacters)
{
	Characters = InCharacters;
	OnCharactersChanged.Broadcast(Characters);
}

void UMT2ClientSessionSubsystem::MarkTravelingToMap()
{
	SetState(EMT2ClientSessionState::TravelingToMap);
}

void UMT2ClientSessionSubsystem::MarkInWorld()
{
	SetState(EMT2ClientSessionState::InWorld);
}

void UMT2ClientSessionSubsystem::MarkFailure(const FString& Error)
{
	SetState(EMT2ClientSessionState::Failed, Error);
}

void UMT2ClientSessionSubsystem::SetState(
	EMT2ClientSessionState NewState, const FString& Error)
{
	State = NewState;
	LastError = Error;
	OnStateChanged.Broadcast(State, LastError);
}

void UMT2ClientSessionSubsystem::HandleNetworkFailure(
	UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType,
	const FString& ErrorString)
{
	if (!World || World->GetNetMode() != NM_DedicatedServer)
	{
		UE_LOG(LogMT2ClientSession, Error,
			TEXT("[MapTravel] Client network failure type=%d world='%s' driver='%s': %s"),
			static_cast<int32>(FailureType),
			World ? *World->GetName() : TEXT("None"),
			NetDriver ? *NetDriver->GetName() : TEXT("None"),
			ErrorString.IsEmpty() ? TEXT("No error text") : *ErrorString);
		FString Error = ErrorString;
		if (FailureType == ENetworkFailure::OutdatedClient)
		{
			Error = TEXT("Your client version does not match the server. Restart the game through the patcher.");
		}
		else if (FailureType == ENetworkFailure::OutdatedServer)
		{
			Error = TEXT("The server version does not match your client. Try again later.");
		}
		MarkFailure(Error.IsEmpty() ? TEXT("Network connection failed.") : Error);
	}
}

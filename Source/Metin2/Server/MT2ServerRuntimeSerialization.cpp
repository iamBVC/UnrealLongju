/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Server/MT2ServerRuntimeSubsystem.h"

#include "Dom/JsonObject.h"

FString UMT2ServerRuntimeSubsystem::GetStringField(
	const TSharedPtr<FJsonObject>& Message, const TCHAR* Name)
{
	FString Value;
	if (Message) Message->TryGetStringField(Name, Value);
	return Value;
}

int32 UMT2ServerRuntimeSubsystem::GetIntField(
	const TSharedPtr<FJsonObject>& Message, const TCHAR* Name, int32 DefaultValue)
{
	double Value = DefaultValue;
	return Message && Message->TryGetNumberField(Name, Value)
		? static_cast<int32>(Value) : DefaultValue;
}

int64 UMT2ServerRuntimeSubsystem::GetInt64Field(
	const TSharedPtr<FJsonObject>& Message, const TCHAR* Name, int64 DefaultValue)
{
	FString StringValue;
	if (Message && Message->TryGetStringField(Name, StringValue))
	{
		return FCString::Atoi64(*StringValue);
	}
	double NumberValue = static_cast<double>(DefaultValue);
	return Message && Message->TryGetNumberField(Name, NumberValue)
		? static_cast<int64>(NumberValue) : DefaultValue;
}

void UMT2ServerRuntimeSubsystem::WriteServerDescriptor(
	const FMT2MapServerDescriptor& Server, const TSharedRef<FJsonObject>& Message)
{
	Message->SetStringField(TEXT("instance_id"), Server.InstanceId);
	Message->SetStringField(TEXT("map_id"), Server.MapId);
	Message->SetStringField(TEXT("map_path"), Server.MapPath);
	Message->SetNumberField(TEXT("channel"), Server.Channel);
	Message->SetStringField(TEXT("public_ip"), Server.PublicIp);
	Message->SetNumberField(TEXT("game_port"), Server.GamePort);
	Message->SetNumberField(TEXT("players"), Server.PlayerCount);
	Message->SetNumberField(TEXT("max_players"), Server.MaxPlayers);
	Message->SetNumberField(TEXT("server_state"), static_cast<int32>(Server.State));
	Message->SetStringField(TEXT("build_version"), Server.BuildVersion);
}

FMT2MapServerDescriptor UMT2ServerRuntimeSubsystem::ReadServerDescriptor(
	const TSharedPtr<FJsonObject>& Message)
{
	FMT2MapServerDescriptor Server;
	Server.InstanceId = GetStringField(Message, TEXT("instance_id"));
	Server.MapId = GetStringField(Message, TEXT("map_id"));
	Server.MapPath = GetStringField(Message, TEXT("map_path"));
	Server.Channel = GetIntField(Message, TEXT("channel"), 1);
	Server.PublicIp = GetStringField(Message, TEXT("public_ip"));
	Server.GamePort = GetIntField(Message, TEXT("game_port"), 11001);
	Server.PlayerCount = GetIntField(Message, TEXT("players"));
	Server.MaxPlayers = GetIntField(Message, TEXT("max_players"), 1000);
	Server.State = static_cast<EMT2MapServerState>(GetIntField(
		Message, TEXT("server_state"), static_cast<int32>(EMT2MapServerState::Starting)));
	Server.BuildVersion = GetStringField(Message, TEXT("build_version"));
	return Server;
}

TSharedRef<FJsonObject> UMT2ServerRuntimeSubsystem::WriteCharacterSummary(
	const FMT2CharacterSummary& Character)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("character_id"), Character.CharacterId);
	Object->SetStringField(TEXT("character_name"), Character.CharacterName);
	Object->SetNumberField(TEXT("level"), Character.Level);
	Object->SetNumberField(TEXT("race"), static_cast<int32>(Character.Appearance.Race));
	Object->SetNumberField(TEXT("sex"), static_cast<int32>(Character.Appearance.Sex));
	Object->SetNumberField(TEXT("style"), static_cast<int32>(Character.Appearance.Style));
	Object->SetNumberField(TEXT("empire"), static_cast<int32>(Character.Empire));
	Object->SetStringField(TEXT("map_id"), Character.MapId);
	Object->SetNumberField(TEXT("channel"), Character.Channel);
	Object->SetNumberField(TEXT("armor_vnum"), Character.EquippedArmorVnum);
	Object->SetNumberField(TEXT("weapon_vnum"), Character.EquippedWeaponVnum);
	Object->SetNumberField(TEXT("hair_vnum"), Character.EquippedHairVnum);
	return Object;
}

bool UMT2ServerRuntimeSubsystem::ReadCharacterSummary(
	const TSharedPtr<FJsonObject>& Object, FMT2CharacterSummary& OutCharacter)
{
	if (!Object.IsValid()) return false;
	OutCharacter.CharacterId = GetStringField(Object, TEXT("character_id"));
	OutCharacter.CharacterName = GetStringField(Object, TEXT("character_name"));
	OutCharacter.Level = GetIntField(Object, TEXT("level"), 1);
	OutCharacter.Appearance.Race = static_cast<EMT2CharacterRace>(GetIntField(Object, TEXT("race")));
	OutCharacter.Appearance.Sex = static_cast<EMT2CharacterSex>(GetIntField(Object, TEXT("sex")));
	OutCharacter.Appearance.Style = static_cast<EMT2CharacterStyle>(GetIntField(Object, TEXT("style")));
	OutCharacter.Empire = static_cast<EMT2Empire>(GetIntField(Object, TEXT("empire")));
	OutCharacter.MapId = GetStringField(Object, TEXT("map_id"));
	OutCharacter.Channel = GetIntField(Object, TEXT("channel"), 1);
	OutCharacter.EquippedArmorVnum = GetIntField(Object, TEXT("armor_vnum"));
	OutCharacter.EquippedWeaponVnum = GetIntField(Object, TEXT("weapon_vnum"));
	OutCharacter.EquippedHairVnum = GetIntField(Object, TEXT("hair_vnum"));
	return !OutCharacter.CharacterId.IsEmpty() && !OutCharacter.CharacterName.IsEmpty();
}

TSharedRef<FJsonObject> UMT2ServerRuntimeSubsystem::WritePartyMember(
	const FMT2PartyMemberData& Member)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("character_id"), Member.CharacterId);
	Object->SetNumberField(TEXT("player_id"), Member.PlayerId);
	Object->SetStringField(TEXT("character_name"), Member.CharacterName);
	Object->SetNumberField(TEXT("level"), Member.Level);
	Object->SetNumberField(TEXT("race"), static_cast<int32>(Member.Appearance.Race));
	Object->SetNumberField(TEXT("sex"), static_cast<int32>(Member.Appearance.Sex));
	Object->SetNumberField(TEXT("style"), static_cast<int32>(Member.Appearance.Style));
	Object->SetNumberField(TEXT("health"), Member.Health);
	Object->SetNumberField(TEXT("max_health"), Member.MaxHealth);
	Object->SetBoolField(TEXT("leader"), Member.bLeader);
	Object->SetBoolField(TEXT("has_location"), Member.bHasWorldLocation);
	Object->SetNumberField(TEXT("x"), Member.WorldLocation.X);
	Object->SetNumberField(TEXT("y"), Member.WorldLocation.Y);
	Object->SetNumberField(TEXT("z"), Member.WorldLocation.Z);
	Object->SetStringField(TEXT("map_id"), Member.MapId);
	Object->SetNumberField(TEXT("channel"), Member.Channel);
	return Object;
}

bool UMT2ServerRuntimeSubsystem::ReadPartyMember(
	const TSharedPtr<FJsonObject>& Object, FMT2PartyMemberData& OutMember)
{
	if (!Object.IsValid()) return false;
	OutMember.PlayerState = nullptr;
	OutMember.CharacterId = GetStringField(Object, TEXT("character_id"));
	OutMember.PlayerId = GetIntField(Object, TEXT("player_id"), INDEX_NONE);
	OutMember.CharacterName = GetStringField(Object, TEXT("character_name"));
	OutMember.Level = GetIntField(Object, TEXT("level"), 1);
	OutMember.Appearance.Race = static_cast<EMT2CharacterRace>(GetIntField(Object, TEXT("race")));
	OutMember.Appearance.Sex = static_cast<EMT2CharacterSex>(GetIntField(Object, TEXT("sex")));
	OutMember.Appearance.Style = static_cast<EMT2CharacterStyle>(GetIntField(Object, TEXT("style")));
	double Health = 0.0, MaxHealth = 1.0, X = 0.0, Y = 0.0, Z = 0.0;
	Object->TryGetNumberField(TEXT("health"), Health);
	Object->TryGetNumberField(TEXT("max_health"), MaxHealth);
	Object->TryGetNumberField(TEXT("x"), X);
	Object->TryGetNumberField(TEXT("y"), Y);
	Object->TryGetNumberField(TEXT("z"), Z);
	OutMember.Health = static_cast<float>(Health);
	OutMember.MaxHealth = FMath::Max(static_cast<float>(MaxHealth), 1.0f);
	OutMember.bLeader = Object->GetBoolField(TEXT("leader"));
	OutMember.bHasWorldLocation = Object->GetBoolField(TEXT("has_location"));
	OutMember.WorldLocation = FVector(X, Y, Z);
	OutMember.MapId = GetStringField(Object, TEXT("map_id"));
	OutMember.Channel = GetIntField(Object, TEXT("channel"), 1);
	return !OutMember.CharacterId.IsEmpty() && !OutMember.CharacterName.IsEmpty();
}

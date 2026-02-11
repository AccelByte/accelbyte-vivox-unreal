// Copyright (c) 2024 AccelByte Inc. All Rights Reserved.
// This is licensed software from AccelByte Inc, for limitations
// and restrictions contact your company contract manager.

#include "VivoxIntegrationSubsystem.h"

#include "AccelByteVivoxVoiceChat.h"
#include "OnlineSubsystemAccelByte.h"
#include "OnlineSubsystemAccelByteTypes.h"
#include "OnlineSubsystemUtils.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"

DEFINE_LOG_CATEGORY(LogVivoxIntegration);

#define UE_LOG_VIVOX_INTEGRATION(Verbosity, Format, ...) \
	UE_LOG(LogVivoxIntegration, Verbosity, TEXT("%s: ") TEXT(Format), *FString(__FUNCTION__), ##__VA_ARGS__)

void UVivoxIntegrationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Get Online Subsystem and make sure it's valid.
	FOnlineSubsystemAccelByte* Subsystem = static_cast<FOnlineSubsystemAccelByte*>(Online::GetSubsystem(GetWorld()));
	if (!ensure(Subsystem))
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "The online subsystem is invalid.");
		return;
	}

	// Get AccelByte Identity Interface.
	IdentityInterface = StaticCastSharedPtr<FOnlineIdentityAccelByte>(Subsystem->GetIdentityInterface());
	if (!ensure(IdentityInterface.IsValid()))
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Identity interface is not valid.");
		return;
	}

	// Get AccelByte Session Interface.
	SessionInterface = StaticCastSharedPtr<FOnlineSessionV2AccelByte>(Subsystem->GetSessionInterface());
	if (!ensure(SessionInterface.IsValid()))
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Session interface is not valid.");
		return;
	}

	// Bind to OSS delegates.
	IdentityInterface->AddOnLoginCompleteDelegate_Handle(0,
		FOnLoginCompleteDelegate::CreateUObject(this, &ThisClass::OnLoginComplete));
	SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnCreateSessionComplete));
	SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoinSessionComplete));
	SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::OnDestroySessionComplete));

	// Bind to Vivox delegates.
	FAccelByteVivoxVoiceChatPtr VoiceChat = FAccelByteVivoxVoiceChat::Get();
	if (VoiceChat.IsValid())
	{
		VoiceChat->OnLoginCompleted.AddUObject(this, &ThisClass::OnVivoxLoginCompleted);
		VoiceChat->OnChannelJoined.AddUObject(this, &ThisClass::OnVivoxChannelJoined);
	}
}

void UVivoxIntegrationSubsystem::Deinitialize()
{
	// Leave party channel if active.
	FAccelByteVivoxVoiceChatPtr VoiceChat = FAccelByteVivoxVoiceChat::Get();
	if (VoiceChat.IsValid())
	{
		if (!CurrentPartyChannelName.IsEmpty())
		{
			UE_LOG_VIVOX_INTEGRATION(Log, "Leaving channel: %s", *CurrentPartyChannelName);
			VoiceChat->LeaveChannel(CurrentPartyChannelName);
			CurrentPartyChannelName.Empty();
		}

		if (VoiceChat->IsLoggedIn())
		{
			VoiceChat->Logout();
		}

		// Clear Vivox delegate bindings.
		VoiceChat->OnLoginCompleted.RemoveAll(this);
		VoiceChat->OnChannelJoined.RemoveAll(this);
	}

	// Clear OSS delegate bindings.
	if (IdentityInterface.IsValid())
	{
		IdentityInterface->ClearOnLoginCompleteDelegates(0, this);
	}

	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegates(this);
		SessionInterface->ClearOnJoinSessionCompleteDelegates(this);
		SessionInterface->ClearOnDestroySessionCompleteDelegates(this);
	}

	LoggedInUserNum = INDEX_NONE;

	Super::Deinitialize();
}

void UVivoxIntegrationSubsystem::OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
	if (!bWasSuccessful)
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "OSS login failed, skipping Vivox login. Error: %s", *Error);
		return;
	}

	FAccelByteVivoxVoiceChatPtr VoiceChat = FAccelByteVivoxVoiceChat::Get();
	if (!VoiceChat.IsValid())
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Vivox voice chat instance is not available.");
		return;
	}

	// Get AccelByte user ID from the unique net ID.
	const FUniqueNetIdAccelByteUserPtr UserABId = StaticCastSharedRef<const FUniqueNetIdAccelByteUser>(UserId.AsShared());
	if (!UserABId.IsValid())
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Failed to cast UserId to AccelByte user ID.");
		return;
	}
	const FString AccelByteUserId = UserABId->GetAccelByteId();

	// Get API client for token-based Vivox auth.
	const AccelByte::FApiClientPtr ApiClient = IdentityInterface->GetApiClient(LocalUserNum);
	if (!ApiClient)
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Failed to get AccelByte API Client.");
		return;
	}

	LoggedInUserNum = LocalUserNum;
	UE_LOG_VIVOX_INTEGRATION(Log, "OSS login successful. Logging into Vivox for user: %s", *AccelByteUserId);
	VoiceChat->Login(ApiClient, AccelByteUserId);
}

void UVivoxIntegrationSubsystem::OnVivoxLoginCompleted(bool bSuccess)
{
	if (bSuccess)
	{
		UE_LOG_VIVOX_INTEGRATION(Log, "Vivox login successful.");
	}
	else
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Vivox login failed.");
	}
}

void UVivoxIntegrationSubsystem::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	// Only handle party sessions.
	if (!SessionName.IsEqual(NAME_PartySession))
	{
		return;
	}

	if (!bWasSuccessful)
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Party session creation failed, skipping Vivox channel join.");
		return;
	}

	JoinPartyChannelFromSession(SessionName);
}

void UVivoxIntegrationSubsystem::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	// Only handle party sessions.
	if (!SessionName.IsEqual(NAME_PartySession))
	{
		return;
	}

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Party session join failed, skipping Vivox channel join. Result: %d",
			static_cast<int32>(Result));
		return;
	}

	JoinPartyChannelFromSession(SessionName);
}

void UVivoxIntegrationSubsystem::JoinPartyChannelFromSession(FName SessionName)
{

	FAccelByteVivoxVoiceChatPtr VoiceChat = FAccelByteVivoxVoiceChat::Get();
	if (!VoiceChat.IsValid())
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Vivox voice chat instance is not available.");
		return;
	}

	// Get the session to extract the session ID.
	FNamedOnlineSession* Session = SessionInterface->GetNamedSession(SessionName);
	if (!Session || !Session->SessionInfo.IsValid())
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Failed to get party session info.");
		return;
	}

	const TSharedPtr<FOnlineSessionInfoAccelByteV2> SessionInfo = StaticCastSharedPtr<FOnlineSessionInfoAccelByteV2>(Session->SessionInfo);
	if (!SessionInfo.IsValid())
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Party session info is not AccelByte V2.");
		return;
	}

	const FString PartyChannelName = SessionInfo->GetSessionId().ToString();
	if (PartyChannelName.IsEmpty())
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Party session ID is empty, skipping Vivox channel join.");
		return;
	}

	if (CurrentPartyChannelName == PartyChannelName && VoiceChat->IsInChannel(PartyChannelName))
	{
		UE_LOG_VIVOX_INTEGRATION(Log, "Already in Vivox party channel: %s", *PartyChannelName);
		return;
	}

	CurrentPartyChannelName = PartyChannelName;
	UE_LOG_VIVOX_INTEGRATION(Log, "Joining Vivox channel: %s", *CurrentPartyChannelName);

	VoiceChat->JoinChannel(CurrentPartyChannelName);
}

void UVivoxIntegrationSubsystem::OnVivoxChannelJoined(const FString& ChannelName, bool bSuccess)
{
	if (bSuccess)
	{
		UE_LOG_VIVOX_INTEGRATION(Log, "Joined Vivox channel: %s", *ChannelName);
		FAccelByteVivoxVoiceChatPtr VoiceChat = FAccelByteVivoxVoiceChat::Get();
		if (VoiceChat.IsValid() && ChannelName == CurrentPartyChannelName)
		{
			VoiceChat->SetTransmissionChannel(ChannelName);
		}
	}
	else
	{
		UE_LOG_VIVOX_INTEGRATION(Warning, "Failed to join Vivox channel: %s", *ChannelName);
	}
}

void UVivoxIntegrationSubsystem::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	// Only handle party sessions.
	if (!SessionName.IsEqual(NAME_PartySession))
	{
		return;
	}

	if (CurrentPartyChannelName.IsEmpty())
	{
		return;
	}

	FAccelByteVivoxVoiceChatPtr VoiceChat = FAccelByteVivoxVoiceChat::Get();
	if (VoiceChat.IsValid())
	{
		UE_LOG_VIVOX_INTEGRATION(Log, "Leaving Vivox channel: %s", *CurrentPartyChannelName);
		VoiceChat->LeaveChannel(CurrentPartyChannelName);
	}

	CurrentPartyChannelName.Empty();
}

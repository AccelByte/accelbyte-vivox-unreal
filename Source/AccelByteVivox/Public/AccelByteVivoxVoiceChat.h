// Copyright (c) 2024 AccelByte Inc. All Rights Reserved.
// This is licensed software from AccelByte Inc, for limitations
// and restrictions contact your company contract manager.

#pragma once

#include "CoreMinimal.h"
#include "Core/AccelByteApiClient.h"

#if VIVOX_AVAILABLE
#include "VivoxCore.h"
#endif

DECLARE_LOG_CATEGORY_EXTERN(LogAccelByteVivox, Log, All);

// Delegates
DECLARE_MULTICAST_DELEGATE_OneParam(FOnVivoxLoginCompleted, bool /*bSuccess*/);
DECLARE_MULTICAST_DELEGATE(FOnVivoxLogoutCompleted);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnVivoxChannelJoined, const FString& /*ChannelName*/, bool /*bSuccess*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnVivoxChannelLeft, const FString& /*ChannelName*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnVivoxParticipantAdded, const FString& /*ChannelName*/, const FString& /*ParticipantId*/, const FString& /*DisplayName*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnVivoxParticipantRemoved, const FString& /*ChannelName*/, const FString& /*ParticipantId*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnVivoxParticipantTalkingChanged, const FString& /*ChannelName*/, const FString& /*ParticipantId*/, bool /*bIsTalking*/);

using FAccelByteVivoxVoiceChatPtr = TSharedPtr<class FAccelByteVivoxVoiceChat, ESPMode::ThreadSafe>;

class ACCELBYTEVIVOX_API FAccelByteVivoxVoiceChat : public TSharedFromThis<FAccelByteVivoxVoiceChat, ESPMode::ThreadSafe>
{
public:
	static FAccelByteVivoxVoiceChatPtr Get();

	FAccelByteVivoxVoiceChat();
	~FAccelByteVivoxVoiceChat();

	// Lifecycle
	void Initialize();
	void Uninitialize();

	// Login / Logout
	void Login(const AccelByte::FApiClientPtr& ApiClient, const FString& InUsername);
	void Logout();
	bool IsLoggedIn() const;

	// Channel management
	void JoinChannel(const FString& ChannelName);
	void LeaveChannel(const FString& ChannelName);
	void LeaveAllChannels();
	bool IsInChannel(const FString& ChannelName) const;

	// Transmission — controls which channel receives your microphone audio
	void SetTransmissionChannel(const FString& ChannelName);
	void SetTransmissionToAll();
	void SetTransmissionToNone();

	// Mute
	void SetLocalMute(bool bMuted);
	bool IsLocalMuted() const;
	void SetPlayerMute(const FString& ChannelName, const FString& PlayerId, bool bMuted);
	bool IsPlayerMuted(const FString& ChannelName, const FString& PlayerId) const;

	// Delegates
	FOnVivoxLoginCompleted OnLoginCompleted;
	FOnVivoxLogoutCompleted OnLogoutCompleted;
	FOnVivoxChannelJoined OnChannelJoined;
	FOnVivoxChannelLeft OnChannelLeft;
	FOnVivoxParticipantAdded OnParticipantAdded;
	FOnVivoxParticipantRemoved OnParticipantRemoved;
	FOnVivoxParticipantTalkingChanged OnParticipantTalkingChanged;

private:
	enum class EVivoxLoginState : uint8
	{
		NotLoggedIn,
		LoggingIn,
		LoggedIn
	};

	EVivoxLoginState CurrentLoginState = EVivoxLoginState::NotLoggedIn;
	FString Username;
	AccelByte::FApiClientPtr ApiClientPtr;
	bool bLocalMuted = false;

#if VIVOX_AVAILABLE
	IClient* VivoxVoiceClient = nullptr;
	ILoginSession* VivoxLoginSession = nullptr;
	AccountId VivoxAccountId;
	TMap<FString, IChannelSession*> ChannelSessions;

	// Internal helpers
	void HandleLoginTokenResponse(const FString& AccessToken, const FString& Uri);
	void HandleVivoxLoginCompleted(VivoxCoreError Error);
	void HandleLoginSessionStateChanged(LoginState State);

	void HandleJoinTokenResponse(const FString& ChannelName, const FString& AccessToken, const FString& Uri);
	void HandleChannelConnectCompleted(const FString& ChannelName, VivoxCoreError Error);
	void HandleChannelStateChanged(const FString& ChannelName, const IChannelConnectionState& State);
	void CleanUpChannelSession(const FString& ChannelName);

	// Participant event handlers
	void HandleParticipantAdded(const IParticipant& Participant);
	void HandleParticipantRemoved(const IParticipant& Participant);
	void HandleParticipantUpdated(const IParticipant& Participant);

	// Track talking state per participant to detect changes
	TMap<FString, TMap<FString, bool>> ParticipantTalkingState;

	// Delegate handles for cleanup
	FDelegateHandle LoginSessionStateChangedHandle;
	TMap<FString, FDelegateHandle> ChannelStateChangedHandles;
#endif
};

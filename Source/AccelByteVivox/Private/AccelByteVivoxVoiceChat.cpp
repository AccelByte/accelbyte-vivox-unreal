// Copyright (c) 2024 AccelByte Inc. All Rights Reserved.
// This is licensed software from AccelByte Inc, for limitations
// and restrictions contact your company contract manager.

#include "AccelByteVivoxVoiceChat.h"
#include "AccelByteVivoxSettings.h"

#if VIVOX_AVAILABLE
#include "VivoxCore.h"
#endif

#include "Api/AccelByteVivoxAuthApi.h"
#include "Models/AccelByteVivoxAuthModels.h"

DEFINE_LOG_CATEGORY(LogAccelByteVivox);

static FAccelByteVivoxVoiceChatPtr AccelByteVivoxInstance = nullptr;

FAccelByteVivoxVoiceChatPtr FAccelByteVivoxVoiceChat::Get()
{
	if (!AccelByteVivoxInstance.IsValid())
	{
		AccelByteVivoxInstance = MakeShared<FAccelByteVivoxVoiceChat, ESPMode::ThreadSafe>();
	}
	return AccelByteVivoxInstance;
}

FAccelByteVivoxVoiceChat::FAccelByteVivoxVoiceChat()
{
}

FAccelByteVivoxVoiceChat::~FAccelByteVivoxVoiceChat()
{
	Uninitialize();
}

void FAccelByteVivoxVoiceChat::Initialize()
{
#if VIVOX_AVAILABLE
	if (VivoxVoiceClient != nullptr)
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("Vivox already initialized"));
		return;
	}

	FVivoxCoreModule* VivoxModule = static_cast<FVivoxCoreModule*>(
		&FModuleManager::Get().LoadModuleChecked(TEXT("VivoxCore")));

	if (VivoxModule == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("Failed to load VivoxCore module"));
		return;
	}

	VivoxVoiceClient = &VivoxModule->VoiceClient();
	VivoxCoreError Error = VivoxVoiceClient->Initialize();
	if (Error != VxErrorSuccess)
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("Failed to initialize Vivox client, error: %d"), static_cast<int32>(Error));
		VivoxVoiceClient = nullptr;
		return;
	}

	UE_LOG(LogAccelByteVivox, Log, TEXT("Vivox initialized successfully"));
#else
	UE_LOG(LogAccelByteVivox, Log, TEXT("Vivox not available on this platform"));
#endif
}

void FAccelByteVivoxVoiceChat::Uninitialize()
{
#if VIVOX_AVAILABLE
	if (VivoxVoiceClient == nullptr)
	{
		return;
	}

	if (CurrentLoginState != EVivoxLoginState::NotLoggedIn)
	{
		LeaveAllChannels();
		Logout();
	}

	VivoxVoiceClient->Uninitialize();
	VivoxVoiceClient = nullptr;

	UE_LOG(LogAccelByteVivox, Log, TEXT("Vivox uninitialized"));
#endif
}

void FAccelByteVivoxVoiceChat::Login(const AccelByte::FApiClientPtr& ApiClient, const FString& InUsername)
{
#if VIVOX_AVAILABLE
	if (VivoxVoiceClient == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("Login failed: Vivox not initialized. Call Initialize() first"));
		OnLoginCompleted.Broadcast(false);
		return;
	}

	if (CurrentLoginState != EVivoxLoginState::NotLoggedIn)
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("Login failed: Already logged in or login in progress"));
		OnLoginCompleted.Broadcast(false);
		return;
	}

	if (!ApiClient.IsValid())
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("Login failed: Invalid ApiClient"));
		OnLoginCompleted.Broadcast(false);
		return;
	}

	ApiClientPtr = ApiClient;
	Username = InUsername;
	CurrentLoginState = EVivoxLoginState::LoggingIn;

	const UAccelByteVivoxSettings* Settings = UAccelByteVivoxSettings::Get();
	VivoxAccountId = AccountId(Settings->VivoxIssuer, Username, Settings->VivoxDomain);
	VivoxLoginSession = &VivoxVoiceClient->GetLoginSession(VivoxAccountId);

	// Request login token from AccelByte
	AccelByte::Api::VivoxAuth VivoxAuthApi = ApiClientPtr->GetApi<AccelByte::Api::VivoxAuth>();

	FAccelByteVivoxAuthServiceGenerateVivoxTokenRequest Request;
	Request.Type = EAccelByteVivoxAuthServiceGenerateVivoxTokenRequestType::login;
	Request.Username = Username;

	VivoxAuthApi.ServiceGenerateVivoxToken(
		Request,
		THandler<FAccelByteVivoxAuthServiceGenerateVivoxTokenResponse>::CreateLambda(
			[this](const FAccelByteVivoxAuthServiceGenerateVivoxTokenResponse& Response)
			{
				HandleLoginTokenResponse(Response.AccessToken, Response.Uri);
			}),
		FErrorHandler::CreateLambda([this](int32 ErrorCode, const FString& ErrorMessage)
		{
			UE_LOG(LogAccelByteVivox, Error, TEXT("Failed to get login token. Code: %d, Message: %s"), ErrorCode, *ErrorMessage);
			CurrentLoginState = EVivoxLoginState::NotLoggedIn;
			OnLoginCompleted.Broadcast(false);
		})
	);
#else
	UE_LOG(LogAccelByteVivox, Warning, TEXT("Login: Vivox not available on this platform"));
	OnLoginCompleted.Broadcast(false);
#endif
}

#if VIVOX_AVAILABLE
void FAccelByteVivoxVoiceChat::HandleLoginTokenResponse(const FString& AccessToken, const FString& Uri)
{
	if (VivoxLoginSession == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("Login session is null after token received"));
		CurrentLoginState = EVivoxLoginState::NotLoggedIn;
		OnLoginCompleted.Broadcast(false);
		return;
	}

	const UAccelByteVivoxSettings* Settings = UAccelByteVivoxSettings::Get();
	const FString LoginServerUri = Settings->VivoxServer;
	if (LoginServerUri.IsEmpty())
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("Vivox login failed: server URI missing. Set VivoxServer in AccelByteVivox settings."));
		CurrentLoginState = EVivoxLoginState::NotLoggedIn;
		OnLoginCompleted.Broadcast(false);
		return;
	}

	VivoxCoreError Error = VivoxLoginSession->BeginLogin(
		LoginServerUri,
		AccessToken,
		ILoginSession::FOnBeginLoginCompletedDelegate::CreateRaw(
			this, &FAccelByteVivoxVoiceChat::HandleVivoxLoginCompleted));

	if (Error != VxErrorSuccess)
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("BeginLogin failed with error: %d"), static_cast<int32>(Error));
		CurrentLoginState = EVivoxLoginState::NotLoggedIn;
		OnLoginCompleted.Broadcast(false);
	}
}

void FAccelByteVivoxVoiceChat::HandleVivoxLoginCompleted(VivoxCoreError Error)
{
	if (Error == VxErrorSuccess)
	{
		CurrentLoginState = EVivoxLoginState::LoggedIn;

		LoginSessionStateChangedHandle = VivoxLoginSession->EventStateChanged.AddRaw(
			this, &FAccelByteVivoxVoiceChat::HandleLoginSessionStateChanged);

		UE_LOG(LogAccelByteVivox, Log, TEXT("Vivox login successful for user: %s"), *Username);
		OnLoginCompleted.Broadcast(true);
	}
	else
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("Vivox login failed with error: %d"), static_cast<int32>(Error));
		CurrentLoginState = EVivoxLoginState::NotLoggedIn;
		OnLoginCompleted.Broadcast(false);
	}
}

void FAccelByteVivoxVoiceChat::HandleLoginSessionStateChanged(LoginState State)
{
	if (State == LoginState::LoggedOut)
	{
		UE_LOG(LogAccelByteVivox, Log, TEXT("Vivox login session logged out"));
		CurrentLoginState = EVivoxLoginState::NotLoggedIn;
		VivoxLoginSession = nullptr;
		OnLogoutCompleted.Broadcast();
	}
}
#endif

void FAccelByteVivoxVoiceChat::Logout()
{
#if VIVOX_AVAILABLE
	if (VivoxLoginSession == nullptr || CurrentLoginState == EVivoxLoginState::NotLoggedIn)
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("Logout: Not logged in"));
		return;
	}

	LeaveAllChannels();

	if (LoginSessionStateChangedHandle.IsValid())
	{
		VivoxLoginSession->EventStateChanged.Remove(LoginSessionStateChangedHandle);
		LoginSessionStateChangedHandle.Reset();
	}

	VivoxLoginSession->Logout();
	VivoxLoginSession = nullptr;
	CurrentLoginState = EVivoxLoginState::NotLoggedIn;
	ApiClientPtr.Reset();

	UE_LOG(LogAccelByteVivox, Log, TEXT("Vivox logged out"));
	OnLogoutCompleted.Broadcast();
#endif
}

bool FAccelByteVivoxVoiceChat::IsLoggedIn() const
{
	return CurrentLoginState == EVivoxLoginState::LoggedIn;
}

void FAccelByteVivoxVoiceChat::JoinChannel(const FString& ChannelName)
{
#if VIVOX_AVAILABLE
	if (CurrentLoginState != EVivoxLoginState::LoggedIn)
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("JoinChannel failed: Not logged in"));
		OnChannelJoined.Broadcast(ChannelName, false);
		return;
	}

	if (ChannelSessions.Contains(ChannelName))
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("JoinChannel: Already in channel %s"), *ChannelName);
		OnChannelJoined.Broadcast(ChannelName, true);
		return;
	}

	if (!ApiClientPtr.IsValid())
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("JoinChannel failed: ApiClient is invalid"));
		OnChannelJoined.Broadcast(ChannelName, false);
		return;
	}

	// Request join token from AccelByte
	AccelByte::Api::VivoxAuth VivoxAuthApi = ApiClientPtr->GetApi<AccelByte::Api::VivoxAuth>();

	FAccelByteVivoxAuthServiceGenerateVivoxTokenRequest Request;
	Request.Type = EAccelByteVivoxAuthServiceGenerateVivoxTokenRequestType::join;
	Request.Username = Username;
	Request.ChannelId = ChannelName;
	Request.ChannelType = EAccelByteVivoxAuthServiceGenerateVivoxTokenRequestChannelType::nonpositional;

	VivoxAuthApi.ServiceGenerateVivoxToken(
		Request,
		THandler<FAccelByteVivoxAuthServiceGenerateVivoxTokenResponse>::CreateLambda(
			[this, ChannelName](const FAccelByteVivoxAuthServiceGenerateVivoxTokenResponse& Response)
			{
				HandleJoinTokenResponse(ChannelName, Response.AccessToken, Response.Uri);
			}),
		FErrorHandler::CreateLambda([this, ChannelName](int32 ErrorCode, const FString& ErrorMessage)
		{
			UE_LOG(LogAccelByteVivox, Error, TEXT("Failed to get join token for channel %s. Code: %d, Message: %s"),
				*ChannelName, ErrorCode, *ErrorMessage);
			OnChannelJoined.Broadcast(ChannelName, false);
		})
	);
#else
	UE_LOG(LogAccelByteVivox, Warning, TEXT("JoinChannel: Vivox not available on this platform"));
	OnChannelJoined.Broadcast(ChannelName, false);
#endif
}

#if VIVOX_AVAILABLE
void FAccelByteVivoxVoiceChat::HandleJoinTokenResponse(const FString& ChannelName, const FString& AccessToken, const FString& Uri)
{
	if (VivoxLoginSession == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("Join channel failed: Login session is null"));
		OnChannelJoined.Broadcast(ChannelName, false);
		return;
	}

	if (Uri.IsEmpty())
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("Join channel failed: server URI missing in token response."));
		OnChannelJoined.Broadcast(ChannelName, false);
		return;
	}

	const UAccelByteVivoxSettings* Settings = UAccelByteVivoxSettings::Get();
	ChannelId VivoxChannelId(Settings->VivoxIssuer, ChannelName, Settings->VivoxDomain, ChannelType::NonPositional);

	IChannelSession& ChannelSession = VivoxLoginSession->GetChannelSession(VivoxChannelId);

	// Register participant event handlers
	ChannelSession.EventAfterParticipantAdded.AddRaw(
		this, &FAccelByteVivoxVoiceChat::HandleParticipantAdded);
	ChannelSession.EventBeforeParticipantRemoved.AddRaw(
		this, &FAccelByteVivoxVoiceChat::HandleParticipantRemoved);
	ChannelSession.EventAfterParticipantUpdated.AddRaw(
		this, &FAccelByteVivoxVoiceChat::HandleParticipantUpdated);

	ChannelSessions.Add(ChannelName, &ChannelSession);

	VivoxCoreError Error = ChannelSession.BeginConnect(
		true,  // audio
		false, // text
		false, // switchTransmission — caller controls via SetTransmissionChannel()
		AccessToken,
		IChannelSession::FOnBeginConnectCompletedDelegate::CreateLambda(
			[this, ChannelName](VivoxCoreError ConnectError)
			{
				HandleChannelConnectCompleted(ChannelName, ConnectError);
			}));

	if (Error != VxErrorSuccess)
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("BeginConnect failed for channel %s, error: %d"),
			*ChannelName, static_cast<int32>(Error));
		CleanUpChannelSession(ChannelName);
		OnChannelJoined.Broadcast(ChannelName, false);
	}
}

void FAccelByteVivoxVoiceChat::HandleChannelConnectCompleted(const FString& ChannelName, VivoxCoreError Error)
{
	if (Error == VxErrorSuccess)
	{
		IChannelSession** ChannelSessionPtr = ChannelSessions.Find(ChannelName);
		if (ChannelSessionPtr != nullptr && *ChannelSessionPtr != nullptr)
		{
			FDelegateHandle Handle = (*ChannelSessionPtr)->EventChannelStateChanged.AddLambda(
				[this, ChannelName](const IChannelConnectionState& State)
				{
					HandleChannelStateChanged(ChannelName, State);
				});
			ChannelStateChangedHandles.Add(ChannelName, Handle);
		}

		UE_LOG(LogAccelByteVivox, Log, TEXT("Joined channel: %s"), *ChannelName);
		OnChannelJoined.Broadcast(ChannelName, true);
	}
	else
	{
		UE_LOG(LogAccelByteVivox, Error, TEXT("Failed to join channel %s, error: %d"),
			*ChannelName, static_cast<int32>(Error));
		CleanUpChannelSession(ChannelName);
		OnChannelJoined.Broadcast(ChannelName, false);
	}
}

void FAccelByteVivoxVoiceChat::HandleChannelStateChanged(const FString& ChannelName, const IChannelConnectionState& State)
{
	if (State.State() == ConnectionState::Disconnected)
	{
		UE_LOG(LogAccelByteVivox, Log, TEXT("Channel %s disconnected"), *ChannelName);
		CleanUpChannelSession(ChannelName);
		OnChannelLeft.Broadcast(ChannelName);
	}
}

void FAccelByteVivoxVoiceChat::CleanUpChannelSession(const FString& ChannelName)
{
	FDelegateHandle* StateHandle = ChannelStateChangedHandles.Find(ChannelName);
	if (StateHandle != nullptr)
	{
		IChannelSession** ChannelSessionPtr = ChannelSessions.Find(ChannelName);
		if (ChannelSessionPtr != nullptr && *ChannelSessionPtr != nullptr)
		{
			(*ChannelSessionPtr)->EventChannelStateChanged.Remove(*StateHandle);
		}
		ChannelStateChangedHandles.Remove(ChannelName);
	}

	IChannelSession** ChannelSessionPtr = ChannelSessions.Find(ChannelName);
	if (ChannelSessionPtr != nullptr && *ChannelSessionPtr != nullptr && VivoxLoginSession != nullptr)
	{
		VivoxLoginSession->DeleteChannelSession((*ChannelSessionPtr)->Channel());
	}

	ChannelSessions.Remove(ChannelName);
	ParticipantTalkingState.Remove(ChannelName);
}
#endif

void FAccelByteVivoxVoiceChat::LeaveChannel(const FString& ChannelName)
{
#if VIVOX_AVAILABLE
	IChannelSession** ChannelSessionPtr = ChannelSessions.Find(ChannelName);
	if (ChannelSessionPtr == nullptr || *ChannelSessionPtr == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("LeaveChannel: Not in channel %s"), *ChannelName);
		return;
	}

	(*ChannelSessionPtr)->Disconnect();
	UE_LOG(LogAccelByteVivox, Log, TEXT("Leaving channel: %s"), *ChannelName);
	// Cleanup will happen in HandleChannelStateChanged when disconnect completes
#endif
}

void FAccelByteVivoxVoiceChat::LeaveAllChannels()
{
#if VIVOX_AVAILABLE
	TArray<FString> ChannelNames;
	ChannelSessions.GetKeys(ChannelNames);

	for (const FString& ChannelName : ChannelNames)
	{
		IChannelSession** ChannelSessionPtr = ChannelSessions.Find(ChannelName);
		if (ChannelSessionPtr != nullptr && *ChannelSessionPtr != nullptr)
		{
			(*ChannelSessionPtr)->Disconnect();
		}
	}

	// Force cleanup in case disconnect callbacks don't fire (e.g., during shutdown)
	ChannelSessions.Empty();
	ChannelStateChangedHandles.Empty();
	ParticipantTalkingState.Empty();
#endif
}

bool FAccelByteVivoxVoiceChat::IsInChannel(const FString& ChannelName) const
{
#if VIVOX_AVAILABLE
	return ChannelSessions.Contains(ChannelName);
#else
	return false;
#endif
}

void FAccelByteVivoxVoiceChat::SetTransmissionChannel(const FString& ChannelName)
{
#if VIVOX_AVAILABLE
	if (VivoxLoginSession == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("SetTransmissionChannel: Not logged in"));
		return;
	}

	IChannelSession** ChannelSessionPtr = ChannelSessions.Find(ChannelName);
	if (ChannelSessionPtr == nullptr || *ChannelSessionPtr == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("SetTransmissionChannel: Not in channel %s"), *ChannelName);
		return;
	}

	VivoxLoginSession->SetTransmissionMode(TransmissionMode::Single, (*ChannelSessionPtr)->Channel());
	UE_LOG(LogAccelByteVivox, Log, TEXT("Transmission set to channel: %s"), *ChannelName);
#endif
}

void FAccelByteVivoxVoiceChat::SetTransmissionToAll()
{
#if VIVOX_AVAILABLE
	if (VivoxLoginSession == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("SetTransmissionToAll: Not logged in"));
		return;
	}

	VivoxLoginSession->SetTransmissionMode(TransmissionMode::All);
	UE_LOG(LogAccelByteVivox, Log, TEXT("Transmission set to all channels"));
#endif
}

void FAccelByteVivoxVoiceChat::SetTransmissionToNone()
{
#if VIVOX_AVAILABLE
	if (VivoxLoginSession == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("SetTransmissionToNone: Not logged in"));
		return;
	}

	VivoxLoginSession->SetTransmissionMode(TransmissionMode::None);
	UE_LOG(LogAccelByteVivox, Log, TEXT("Transmission set to none"));
#endif
}

void FAccelByteVivoxVoiceChat::SetLocalMute(bool bMuted)
{
#if VIVOX_AVAILABLE
	if (VivoxVoiceClient == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("SetLocalMute: Vivox not initialized"));
		return;
	}

	bLocalMuted = bMuted;
	VivoxVoiceClient->AudioInputDevices().SetMuted(bMuted);
	UE_LOG(LogAccelByteVivox, Log, TEXT("Local mute set to: %s"), bMuted ? TEXT("true") : TEXT("false"));
#endif
}

bool FAccelByteVivoxVoiceChat::IsLocalMuted() const
{
	return bLocalMuted;
}

void FAccelByteVivoxVoiceChat::SetPlayerMute(const FString& ChannelName, const FString& PlayerId, bool bMuted)
{
#if VIVOX_AVAILABLE
	IChannelSession** ChannelSessionPtr = ChannelSessions.Find(ChannelName);
	if (ChannelSessionPtr == nullptr || *ChannelSessionPtr == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("SetPlayerMute: Not in channel %s"), *ChannelName);
		return;
	}

	IParticipant* Participant = (*ChannelSessionPtr)->Participants().FindRef(PlayerId);
	if (Participant == nullptr)
	{
		UE_LOG(LogAccelByteVivox, Warning, TEXT("SetPlayerMute: Participant %s not found in channel %s"),
			*PlayerId, *ChannelName);
		return;
	}

	Participant->BeginSetLocalMute(bMuted,
		IParticipant::FOnBeginSetLocalMuteCompletedDelegate::CreateLambda(
			[PlayerId, bMuted](VivoxCoreError Error)
			{
				if (Error == VxErrorSuccess)
				{
					UE_LOG(LogAccelByteVivox, Log, TEXT("Player %s mute set to %s"),
						*PlayerId, bMuted ? TEXT("true") : TEXT("false"));
				}
				else
				{
					UE_LOG(LogAccelByteVivox, Error, TEXT("Failed to set mute for player %s, error: %d"),
						*PlayerId, static_cast<int32>(Error));
				}
			}));
#endif
}

bool FAccelByteVivoxVoiceChat::IsPlayerMuted(const FString& ChannelName, const FString& PlayerId) const
{
#if VIVOX_AVAILABLE
	const IChannelSession* const* ChannelSessionPtr = ChannelSessions.Find(ChannelName);
	if (ChannelSessionPtr == nullptr || *ChannelSessionPtr == nullptr)
	{
		return false;
	}

	const IParticipant* Participant = (*ChannelSessionPtr)->Participants().FindRef(PlayerId);
	if (Participant == nullptr)
	{
		return false;
	}

	return Participant->LocalMute();
#else
	return false;
#endif
}

#if VIVOX_AVAILABLE
void FAccelByteVivoxVoiceChat::HandleParticipantAdded(const IParticipant& Participant)
{
	const FString ChannelName = Participant.ParentChannelSession().Channel().Name();
	const FString ParticipantId = Participant.Account().Name();
	const FString DisplayName = Participant.Account().DisplayName();

	if (!ParticipantTalkingState.Contains(ChannelName))
	{
		ParticipantTalkingState.Add(ChannelName, TMap<FString, bool>());
	}
	ParticipantTalkingState[ChannelName].Add(ParticipantId, false);

	UE_LOG(LogAccelByteVivox, Log, TEXT("Participant added: %s in channel %s"), *ParticipantId, *ChannelName);
	OnParticipantAdded.Broadcast(ChannelName, ParticipantId, DisplayName);
}

void FAccelByteVivoxVoiceChat::HandleParticipantRemoved(const IParticipant& Participant)
{
	const FString ChannelName = Participant.ParentChannelSession().Channel().Name();
	const FString ParticipantId = Participant.Account().Name();

	if (ParticipantTalkingState.Contains(ChannelName))
	{
		ParticipantTalkingState[ChannelName].Remove(ParticipantId);
	}

	UE_LOG(LogAccelByteVivox, Log, TEXT("Participant removed: %s from channel %s"), *ParticipantId, *ChannelName);
	OnParticipantRemoved.Broadcast(ChannelName, ParticipantId);
}

void FAccelByteVivoxVoiceChat::HandleParticipantUpdated(const IParticipant& Participant)
{
	const FString ChannelName = Participant.ParentChannelSession().Channel().Name();
	const FString ParticipantId = Participant.Account().Name();
	const bool bIsTalking = Participant.SpeechDetected();

	TMap<FString, bool>* ChannelParticipants = ParticipantTalkingState.Find(ChannelName);
	if (ChannelParticipants == nullptr)
	{
		return;
	}

	bool* PreviousTalkingState = ChannelParticipants->Find(ParticipantId);
	if (PreviousTalkingState == nullptr)
	{
		return;
	}

	if (*PreviousTalkingState != bIsTalking)
	{
		*PreviousTalkingState = bIsTalking;
		OnParticipantTalkingChanged.Broadcast(ChannelName, ParticipantId, bIsTalking);
	}
}
#endif

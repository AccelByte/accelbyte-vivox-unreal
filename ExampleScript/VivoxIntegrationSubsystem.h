// Copyright (c) 2024 AccelByte Inc. All Rights Reserved.
// This is licensed software from AccelByte Inc, for limitations
// and restrictions contact your company contract manager.

#pragma once

#include "CoreMinimal.h"
#include "OnlineIdentityInterfaceAccelByte.h"
#include "OnlineSessionInterfaceV2AccelByte.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "VivoxIntegrationSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVivoxIntegration, Log, All);

/**
 * Sample integration subsystem that automatically wires OSS events to Vivox:
 * - Login success -> Vivox login
 * - Party create success -> Join Vivox channel using party session ID
 * - Party destroy -> Leave Vivox channel
 */
UCLASS()
class ACCELBYTEWARS_API UVivoxIntegrationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	// OSS references
	FOnlineIdentityAccelBytePtr IdentityInterface;
	FOnlineSessionV2AccelBytePtr SessionInterface;

	// OSS event handlers
	void OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	// Vivox event handlers
	void OnVivoxLoginCompleted(bool bSuccess);
	void OnVivoxChannelJoined(const FString& ChannelName, bool bSuccess);

	// Party flow
	void JoinPartyChannelFromSession(FName SessionName);

	// State
	int32 LoggedInUserNum = INDEX_NONE;
	FString CurrentPartyChannelName;
};

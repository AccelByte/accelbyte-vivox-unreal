// Copyright (c) 2024 AccelByte Inc. All Rights Reserved.
// This is licensed software from AccelByte Inc, for limitations
// and restrictions contact your company contract manager.

#include "AccelByteVivoxModule.h"
#include "AccelByteVivoxVoiceChat.h"

IMPLEMENT_MODULE(FAccelByteVivoxModule, AccelByteVivox)

void FAccelByteVivoxModule::StartupModule()
{
	FAccelByteVivoxVoiceChatPtr VoiceChat = FAccelByteVivoxVoiceChat::Get();
	VoiceChat->Initialize();
}

void FAccelByteVivoxModule::ShutdownModule()
{
	FAccelByteVivoxVoiceChatPtr VoiceChat = FAccelByteVivoxVoiceChat::Get();
	if (VoiceChat.IsValid())
	{
		VoiceChat->Uninitialize();
	}
}

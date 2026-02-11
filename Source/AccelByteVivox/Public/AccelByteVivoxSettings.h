// Copyright (c) 2024 AccelByte Inc. All Rights Reserved.
// This is licensed software from AccelByte Inc, for limitations
// and restrictions contact your company contract manager.

#pragma once

#include "CoreMinimal.h"
#include "AccelByteVivoxSettings.generated.h"

UCLASS(Config = Engine)
class ACCELBYTEVIVOX_API UAccelByteVivoxSettings : public UObject
{
	GENERATED_BODY()

public:
	static const UAccelByteVivoxSettings* Get();

	UPROPERTY(EditAnywhere, Config, Category = "AccelByte Vivox")
	FString VivoxIssuer;

	UPROPERTY(EditAnywhere, Config, Category = "AccelByte Vivox")
	FString VivoxDomain;

	UPROPERTY(EditAnywhere, Config, Category = "AccelByte Vivox")
	FString VivoxServer;
};

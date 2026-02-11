// Copyright (c) 2024 AccelByte Inc. All Rights Reserved.
// This is licensed software from AccelByte Inc, for limitations
// and restrictions contact your company contract manager.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{

public class AccelByteVivox : ModuleRules
{
    public AccelByteVivox(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "AccelByteUe4Sdk",
            "AccelByteUe4SdkCustomization"
        });

        bool bVivoxAvailable = Target.Type != TargetType.Server && Target.Platform != UnrealTargetPlatform.Linux;

        if (bVivoxAvailable)
        {
            PublicDependencyModuleNames.Add("VivoxCore");
            PublicDefinitions.Add("VIVOX_AVAILABLE=1");
        }
        else
        {
            PublicDefinitions.Add("VIVOX_AVAILABLE=0");
        }
    }
}

}

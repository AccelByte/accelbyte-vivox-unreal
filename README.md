# AccelByteVivox Plugin

Vivox voice chat plugin for Unreal Engine with AccelByte token generation. Provides login, multi-channel join/leave, transmission control, and mute APIs.

## Dependencies

- **AccelByteUe4Sdk** — API client
- **AccelByteUe4SdkCustomization** — `VivoxAuth::ServiceGenerateVivoxToken()` for token generation
  - Generate via codegen CLI or download from the [release page](https://github.com/AccelByte/accelbyte-vivox-unreal/releases)
- **VivoxCore** — Vivox SDK (client platforms only, excluded on server/Linux)
  - Download from the official Vivox website

## Configuration

Add to `DefaultEngine.ini`:

```ini
[/Script/AccelByteVivox.AccelByteVivoxSettings]
VivoxIssuer=your-issuer
VivoxDomain=your-domain
VivoxServer=your-login-server-uri

[/Script/AccelByteUe4SdkCustomization.AccelByteCustomizationSettings]
VivoxAuthServerUrl=extend-vivox-url
```

## Platform Support

VivoxCore is only linked on non-server, non-Linux targets. The `VIVOX_AVAILABLE` preprocessor macro is defined as `1` or `0` accordingly. All Vivox SDK calls are guarded by `#if VIVOX_AVAILABLE`, so the plugin compiles cleanly on all platforms.

## Usage

### Setup

Enable the plugin in your `.uproject`:

```json
{
  "Name": "AccelByteVivox",
  "Enabled": true
}
```

Add `AccelByteVivox` to your module's `Build.cs` dependencies.

### API

Access the singleton via `FAccelByteVivoxVoiceChat::Get()`.

```cpp
#include "AccelByteVivoxVoiceChat.h"

auto VoiceChat = FAccelByteVivoxVoiceChat::Get();
```

#### Lifecycle

The module calls `Initialize()` on startup and `Uninitialize()` on shutdown automatically.

#### Login / Logout

```cpp
// Login — pass an AccelByte ApiClient and a username
VoiceChat->Login(ApiClient, Username);

// Listen for result
VoiceChat->OnLoginCompleted.AddLambda([](bool bSuccess)
{
    // handle login result
});

// Logout
VoiceChat->Logout();
```

After login succeeds, you can:

- Join a party or create one via OSS; use the session ID as the Vivox channel name.
- Call `JoinChannel` and wait for `OnChannelJoined` before calling `SetTransmissionChannel`.

#### Channel Management

Supports joining multiple channels simultaneously.

```cpp
VoiceChat->JoinChannel(TEXT("party-123"));
VoiceChat->JoinChannel(TEXT("team-456"));

VoiceChat->OnChannelJoined.AddLambda([](const FString& ChannelName, bool bSuccess)
{
    // handle join result
});

VoiceChat->LeaveChannel(TEXT("party-123"));
VoiceChat->LeaveAllChannels();
```

#### Transmission Control

When in multiple channels, controls which channel(s) receive your microphone audio. You can always hear all joined channels regardless of transmission mode.

```cpp
// Transmit to one channel only
VoiceChat->SetTransmissionChannel(TEXT("party-123"));

// Transmit to all joined channels
VoiceChat->SetTransmissionToAll();

// Stop transmitting (listen only)
VoiceChat->SetTransmissionToNone();
```

#### Mute

```cpp
// Mute/unmute own microphone
VoiceChat->SetLocalMute(true);
bool bMuted = VoiceChat->IsLocalMuted();

// Mute a specific participant (client-side only)
VoiceChat->SetPlayerMute(TEXT("party-123"), PlayerId, true);
bool bPlayerMuted = VoiceChat->IsPlayerMuted(TEXT("party-123"), PlayerId);
```

### Delegates

| Delegate | Parameters | Description |
|----------|-----------|-------------|
| `OnLoginCompleted` | `bool bSuccess` | Vivox login result |
| `OnLogoutCompleted` | — | Logged out |
| `OnChannelJoined` | `FString ChannelName, bool bSuccess` | Channel join result |
| `OnChannelLeft` | `FString ChannelName` | Channel disconnected |
| `OnParticipantAdded` | `FString ChannelName, FString ParticipantId, FString DisplayName` | Player joined channel |
| `OnParticipantRemoved` | `FString ChannelName, FString ParticipantId` | Player left channel |
| `OnParticipantTalkingChanged` | `FString ChannelName, FString ParticipantId, bool bIsTalking` | Player talking state changed |

## File Structure

```
AccelByteVivox/
├── AccelByteVivox.uplugin
├── README.md
└── Source/AccelByteVivox/
    ├── AccelByteVivox.Build.cs
    ├── Public/
    │   ├── AccelByteVivoxModule.h          — Module interface
    │   ├── AccelByteVivoxSettings.h        — Config (VivoxIssuer, VivoxDomain, VivoxServer)
    │   └── AccelByteVivoxVoiceChat.h       — Singleton voice chat API
    └── Private/
        ├── AccelByteVivoxModule.cpp
        ├── AccelByteVivoxSettings.cpp
        └── AccelByteVivoxVoiceChat.cpp
```

## Example Script

An OSS-driven integration sample is provided at `ExampleScript/VivoxIntegrationSubsystem.cpp`.

Flow summary:

- Bind OSS login delegates.
- On OSS login success, call `FAccelByteVivoxVoiceChat::Login` using the ApiClient and AccelByte user id.
- Bind OSS party create/join delegates.
- On party create or join success, fetch the party session id and call `JoinChannel`.
- On `OnChannelJoined`, call `SetTransmissionChannel` for the party channel.
- On party destroy, call `LeaveChannel`.

# SRC-Link (The OBS Studio Plugin)

[日本語はこちら](./README_ja.md)

[<img src="./src-link_usage_image.jpg" />](./src-link_usage_image.jpg)

## Concept

The basic concept of the [OBS Studio](https://obsproject.com/) plug-in SRT-Link (Secure Reliable Controllable Link) is to turn OBS into a powerful video 
transmitter and receiver.

OBS Studio offers a wide variety of inputs. There is an excellent game capture feature (unfortunately there is a little compatibility issue in OBS 31,
but I expect it will be fixed in time).
There are effects, overlays, and compositing capabilities. They can be enhanced with plug-ins. And since it is open source, OBS itself can be improved.

What it doesn't have is the ability to send out multiple streams in a reliable manner and manage connections, which SRC-Link provides.

SRC-Link is used with the web service [SRC-Link Control Panel](https://src-link.live/introduction).

**Key features**

- Video Transmitter/Receiver (Based on the SRT protocol)
- Connection management
- WebSocket Portal for OBS (obs-websocket compatible server)
- Operation Team (Share SRC-Link resources with your team)

**Functionalities**

- A host can receive multiple clean-feed (video/audio streams) from guests in parallel. The guest to receive can be selected from among the members.
- Guests can connect to receiver and enter standby state. The standby state allows transmission to begin at any time, and the host's commands initiate video and audio transmission.
- There is no need to send connection information (address, stream ID, password, etc.) to guests.
  Guests do not need to configure them in OBS Studio.
- The host can view the guest's standby status and video screenshots (updated every 5 to 30 seconds) on the SRC-Link Control Panel.
- Since a guest in standby state is literally just waiting without transmitting video or audio, there can theoretically be far more streams (whether 10, 100, or 1,000) than the host can receive.
  * However, there is a limit in the SRC-Link Control Panel plan.
- SRT is used as the transmission protocol for video and audio. Stream ID and passphrase are controlled by the SRC-Link Control Panel.
- **(Since 0.5.5)** Operation Teams allows to share receiver among your organization staffs to operate live streaming program jointly.
- **(Since 0.6.0)** Stream Recording functionality records cleanfeed in guest's local disk.
- **(Since 0.6.0)** External Connections allows SRC-Link connects to external SRT servers such as vMix, SRT MiniServer, Nimble Streamer etc..
- **(Since 0.7.0)** WebSocket Portal for OBS provides an obs-websocket compatible server and allows remote control of 
  OBS Studio over the Internet using obs-websocket clients. No open ports, tunneling or VPN is required.
- **(Since 0.7.2)** RTMP External Connection allows to broadcast external RTMP server each sources.
  
**Glossary**

- **HOST**: OBS instance (and SRC-Link user) receiving video and audio
- **GUEST**: OBS instance (and SRC-Link user) transmitting video and audio
- **RECEIVER**: Represents a group of inputs handled by the host. Usually a single OBS instance is assumed, but it is possible to receive in more than one. A receiver can contain multiple slots and multiple members.
- **SLOT**: One slot represents a set of inputs for one guest. This means that multiple sources (webcam, game capture, etc.) can be configured per slot. The guest sends all sources in parallel, so switching and compositing can be done by the host.
- **SOURCE**: Guests can send multiple sources in parallel. The number of sources is specified by the host, and the guest is free to select video and audio from its own OBS sources.
- **MEMBER**:  Registered guests who can connect to the receiver and standby.
- **INVITATION CODE**: To invite a guest to the receiver, the host generates an invitation code and sends it to the guest. The guest becomes a member by redeeming the code.
- **GUEST CODE**: To request a host to join, a guest sends his/her guest code to the host. The host uses the guest code to register as a member.
- **DOWNLINK**: Host-side input connections
- **UPLINK**: Guest-side output connection

## Requirements

[OBS Studio](https://obsproject.com/) >= 30.1.0 (Qt6, x64/ARM64/AppleSilicon)

(*) MacOS, Linux Up to 30.2.3

(*) Linux required install `qt6-websockets` separately.

Also required signing up to [SRC-Link Control Panel](https://src-link.live) 
Separate [paid subscription plans](https://src-link.live/subscriptions/plans) are available.

# Installation

Please download latest install package from [Release](https://github.com/OPENSPHERE-Inc/src-link/releases)

> NOTE: The windows installer copies `Qt6WebSockets.dll` and Qt6's `tls` plugins folder under your `obs-studio/bin/64bit` as required library.

# User manual

[More detailed user manual in Wiki section](https://github.com/OPENSPHERE-Inc/src-link/wiki)

## For Host

1. Sign up/login to the [SRC-Link Control Panel](https://src-link.live) in a web browser then setup the receiver. Normally, a sample is created after sign-up, so edit this.
   
   First add/remove as many slots as you need (the number of guests appearing on the stream at the same time).

   Then add/remove as many sources as you want to receive from one guest (e.g. 2 sources for a webcam and a game screen).

2. **Basically, the host doesn't need to register as a member with the receiver.**

   If you wish to register yourself as a member for testing purposes, you may do so. 
   You can also send and receive yourself, but the video may loop.
   Refer to the guest's instructions on how to stream the video.

3. Install the SRC-Link plugin in OBS Studio [Download here](https://github.com/OPENSPHERE-Inc/src-link/releases)

4. Launch OBS Studio and click “SRC-Link Settings” from the “Tools” menu or the “Login” button on the “SRC-Link” dock.

5. A web browser will open and an approval screen will appear. Click the “Accept” button.

   OBS Studio and SRC-Link Control Panel are now connected and ready to use.

6. Open “SRC-Link Settings” from the “Tools” menu in OBS Studio. Under “UDP Port Range for listen” set the range of UDP ports to be used. These ports must be accessible from the outside. If you are in a NAT router or FW environment, please open the ports.

   The number of ports required is equal to the total number of sources.

   In environments where opening ports is difficult, consider [installing a relay server](https://github.com/OPENSPHERE-Inc/src-link/wiki/08.-External-Relay-Server-Installation-Guide).

7. In OBS Studio, add “SRC-Link Downlink” to the source. In the source properties, select the receiver, slot, and source that this input will receive. These must not be the same combination of things in other sources (you will not get an error, but you will receive only one of them due to conflicts).

   The source properties also allow you to set the resolution and bitrate range, and these settings act as regulators for the guest-side settings.

   “Relay Server” should be checked if you want to use it. Cannot be checked if unavailable in your plan or no set up in the receiver.

8. Press OK to close the source properties and the source will go into receive standby (if set up correctly, the red warning indicator will disappear).

9. Open the [Host](https://src-link.live/receivers) menu of the SRC-Link Control Panel again and add members to the receiver for each person scheduled to appear. There are two ways to do this: send an invitation code to the guest or receive an guest code from the guest and enter it. Invitation codes are the format `SRCH-xxxx-xxxx-xxxx-xxxx` and guest codes are in the format
 `SRCG-xxxx-xxxx-xxxx-xxxx`.

   If you send an invitation code, wait for it to be accepted by the guest.

   At a good time before the show, tell the guest to be on standby.

   The member on standby will have the link icon activated and a screenshot will be displayed. The screenshot will refresh from time to time in about 5 to 30 seconds.

10. In the [Host](https://src-link.live/receivers) menu of the SRC-Link Control Panel, assign a performer from among the members to a slot. You can only receive streams from members assigned to slots.

11. If the member is in standby, the video will appear on the SRC-Link downlink in OBS Studio as soon as it is assigned to a slot.

12. When the slot is switched, the video on the OBS Studio side is also automatically switched.

## For Guest

1. Sign up/login to the [SRC-Link Control Panel](https://src-link.live) in your web browser.

2. Install the SRC-Link plugin in OBS Studio [Download here](https://github.com/OPENSPHERE-Inc/src-link/releases)

3. Launch OBS Studio and from the “Tools” menu, click “SRC-Link Settings” or the “Login” button in the “SRC-Link” dock.

4. A web browser will open and an approval screen will appear. Click the “Accept” button.

   OBS Studio and SRC-Link Control Panel are now connected and ready to use.

5. Join the host's receiver. There are two ways to join: send an guest code to the host to be added, or have the host generate an invitation code and enter it yourself.

   - For guest codes
     
     Open the [Guest code](https://src-link.live/accounts/access-codes) menu in the SRC-Link Control Panel and copy the default sample guest code already generated, or click “Create” to create a new one.

   - For Invitation Codes

     If the host knows your e-mail address and you receive an e-mail from the SRC-Link Control Panel (noreply@src-link.live), click on the activation URL listed to accept the invitation.

     If your host only sent you an invitation code, click the “Redeem Invitation Code” button in the [Guest](https://src-link.live/memberships) menu of the SRC-Link Control Panel and enter the invitation code.

6. Once you become a member of a receiver, the receiver will appear in the “SRC-Link” dock in OBS Studio (select in the pull-down if you have more than one participating)

   Selecting a receiver activates the “Uplink” and makes the source selectable. Assign the inputs you wish to send to each source. The number of sources and their contents are specified by the host.

   Although the host may specify the number of sources, the guest may set it to “none,” but please consult with the host on how to use the sources.

   Click the “gear” icon for each source to set the bit rate, video/audio encoder, and audio source.

7. By default, the SRC-Link uplink is interlocked with “Virtual Camera”; starting the “Virtual Camera” in OBS Studio will also put the SRC-Link uplink in standby.

   In addition to the “Virtual Camera”, you can select “Streaming”, “Recording”, “Streaming or Recording”, or “Always ON” for the interlocking.

   The standby state should be a state where there is no problem with the host viewing the video and audio at any time. If there is a problem, mute the source by removing the standby state or clicking the “eye” icon on the uplink.

8. If you have finished your performance and wish to leave the receiver, click the “Leave” button on the receiver in the SRC-Link Control Panel [guest](https://src-link.live/memberships) menu.

# Development

This plugin is developed under [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)

## Using Open Source Libraries

- [Qt6 + Qt WebSockets](https://www.qt.io/)
- [o2](https://github.com/pipacs/o2) (Qt based OAuth2 client)
- [json](https://github.com/nlohmann/json) (JSON for Modern C++)
- [Font Awesome](https://fontawesome.com) Free 6.7.0 by @fontawesome - https://fontawesome.com License - https://fontawesome.com/license/free Copyright 2024 Fonticons, Inc.
- [OBS Studio](https://obsproject.com/)

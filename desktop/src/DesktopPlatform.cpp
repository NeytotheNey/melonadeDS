// The desktop's part of the core's Platform layer, with PlatformBase.cpp (files, log, threads): what the app's PlatformAndroid.cpp does
// with the instance the core was made for (the userdata, a MelonInstance), so saves go through its SaveManagers as on the phone, and the
// stop and rumble go to the frontend's event messenger. Multiplayer, Wi-Fi, camera, mic and AAC are stubbed out for now
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "Platform.h"
#include "PlatformAndroid.h"
#include "SPI_Firmware.h"
#include "FrontendContext.h"
#include "MelonInstance.h"

using namespace melonDS;

namespace melonDS::Platform
{

static MelonDSAndroid::MelonInstance* Instance(void* userdata)
{
    return static_cast<MelonDSAndroid::MelonInstance*>(userdata);
}

void SignalStop(StopReason reason, void* userdata)
{
    Log(LogLevel::Info, "emulator stopped, reason %d\n", (int)reason);
    if (MelonDSAndroid::frontendContext.eventMessenger)
        MelonDSAndroid::frontendContext.eventMessenger->onEmulatorStop(reason);
}

FileHandle* OpenInternalFile(const std::string path, FileMode mode)
{
    std::filesystem::path fullPath = MelonDSAndroid::frontendContext.internalFilesDir;
    fullPath /= path;
    return OpenFile(fullPath.string(), mode);
}

// only the app's performance hints use it
pid_t GetThreadTid(Thread* thread)
{
    return 0;
}

void WriteNDSSave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata)
{
    if (auto instance = Instance(userdata))
        instance->requestNdsSaveWrite(savedata, savelen, writeoffset, writelen);
}

void WriteGBASave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata)
{
    if (auto instance = Instance(userdata))
        instance->requestGbaSaveWrite(savedata, savelen, writeoffset, writelen);
}

void SyncNDSSave(const u8* savedata, u32 savelen, void* userdata)
{
    if (auto instance = Instance(userdata))
        instance->syncNdsSaveFromState(savedata, savelen);
}

void SyncGBASave(const u8* savedata, u32 savelen, void* userdata)
{
    if (auto instance = Instance(userdata))
        instance->syncGbaSaveFromState(savedata, savelen);
}

// the firmware is generated each run (or a --bios dump that's only read), and the RTC follows the PC clock, as before the shared code
void WriteFirmware(const Firmware& firmware, u32 writeoffset, u32 writelen, void* userdata) {}
void WriteDateTime(int year, int month, int day, int hour, int minute, int second, void* userdata) {}

void MP_Begin(void* userdata) {}
void MP_End(void* userdata) {}
int MP_SendPacket(u8* data, int len, u64 timestamp, void* userdata) { return 0; }
int MP_RecvPacket(u8* data, u64* timestamp, void* userdata) { return 0; }
int MP_SendCmd(u8* data, int len, u64 timestamp, void* userdata) { return 0; }
int MP_SendReply(u8* data, int len, u64 timestamp, u16 aid, void* userdata) { return 0; }
int MP_SendAck(u8* data, int len, u64 timestamp, void* userdata) { return 0; }
int MP_RecvHostPacket(u8* data, u64* timestamp, void* userdata) { return 0; }
u16 MP_RecvReplies(u8* data, u64 timestamp, u16 aidmask, void* userdata) { return 0; }

// no Wi-Fi yet: the instance's Net has no driver on the desktop
int Net_SendPacket(u8* data, int len, void* userdata) { return 0; }
int Net_RecvPacket(u8* data, void* userdata) { return 0; }

void Camera_Start(int num, void* userdata) {}
void Camera_Stop(int num, void* userdata) {}
void Camera_CaptureFrame(int num, u32* frame, int width, int height, bool yuv, void* userdata)
{
    memset(frame, 0, width * height * (yuv ? 2 : 4));
}

void Mic_Start(void* userdata) {}
void Mic_Stop(void* userdata) {}
int Mic_ReadInput(s16* data, int maxlength, void* userdata) { return 0; }

AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder* dec) {}
bool AAC_Configure(AACDecoder* dec, int frequency, int channels) { return false; }
bool AAC_DecodeFrame(AACDecoder* dec, const void* input, int inputlen, void* output, int outputlen) { return false; }

bool Addon_KeyDown(KeyType type, void* userdata) { return false; }

void Addon_RumbleStart(u32 len, void* userdata)
{
    if (MelonDSAndroid::frontendContext.eventMessenger)
        MelonDSAndroid::frontendContext.eventMessenger->onRumbleStart((int)len);
}

void Addon_RumbleStop(void* userdata)
{
    if (MelonDSAndroid::frontendContext.eventMessenger)
        MelonDSAndroid::frontendContext.eventMessenger->onRumbleStop();
}

float Addon_MotionQuery(MotionQueryType type, void* userdata)
{
    auto instance = Instance(userdata);
    return instance ? instance->getMotionData(type) : 0;
}

}

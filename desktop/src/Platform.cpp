// The core's Platform layer for tools/jitverify, with PlatformBase.cpp: saves are written to disk here, and the rest (multiplayer,
// Wi-Fi, camera, mic, AAC) is stubbed out
#include <stdio.h>
#include <string.h>
#include <string>

#include "Platform.h"
#include "SPI_Firmware.h"
#include "Frontend.h"

using namespace melonDS;

namespace Frontend
{

State App;

static void WriteSave(SaveFile& save)
{
    if (save.Path.empty() || save.Data.empty())
        return;
    FILE* f = fopen(save.Path.c_str(), "wb");
    if (!f)
    {
        fprintf(stderr, "can't write save %s\n", save.Path.c_str());
        return;
    }
    fwrite(save.Data.data(), 1, save.Data.size(), f);
    fclose(f);
    save.Dirty = false;
}

void FlushSaves(bool force)
{
    u64 now = Platform::GetMSCount();
    for (SaveFile* save : {&App.NDSSave, &App.GBASave})
    {
        if (save->Dirty && (force || now - save->LastWriteMs >= 1000))
            WriteSave(*save);
    }
}

static void KeepSave(SaveFile& save, const u8* savedata, u32 savelen)
{
    save.Data.assign(savedata, savedata + savelen);
    save.Dirty = true;
    save.LastWriteMs = Platform::GetMSCount();
}

// a loaded savestate's save image: a pending in-game save goes to disk first, then the image is kept without writing it
static void SyncSave(SaveFile& save, const u8* savedata, u32 savelen)
{
    if (save.Dirty && !save.Path.empty())
    {
        WriteSave(save);
        // still dirty means the write failed: keep the pending data so a later flush writes it, not the state's image
        if (save.Dirty)
        {
            fprintf(stderr, "pending save to %s not written before a state load, keeping it\n", save.Path.c_str());
            return;
        }
    }
    save.Data.assign(savedata, savedata + savelen);
}

}

namespace melonDS::Platform
{

void SignalStop(StopReason reason, void* userdata)
{
    fprintf(stderr, "emulator stopped, reason %d\n", (int)reason);
    Frontend::App.StopRequested = true;
}

void WriteNDSSave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata)
{
    Frontend::KeepSave(Frontend::App.NDSSave, savedata, savelen);
}

void WriteGBASave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata)
{
    Frontend::KeepSave(Frontend::App.GBASave, savedata, savelen);
}

void SyncNDSSave(const u8* savedata, u32 savelen, void* userdata)
{
    Frontend::SyncSave(Frontend::App.NDSSave, savedata, savelen);
}

void SyncGBASave(const u8* savedata, u32 savelen, void* userdata)
{
    Frontend::SyncSave(Frontend::App.GBASave, savedata, savelen);
}

// the firmware is generated each run (or a dump that's only read), and the RTC follows the PC clock
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
void Addon_RumbleStart(u32 len, void* userdata) {}
void Addon_RumbleStop(void* userdata) {}
float Addon_MotionQuery(MotionQueryType type, void* userdata) { return 0; }

}

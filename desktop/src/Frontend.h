// What main.cpp and Platform.cpp share: the log and the save files the core writes through Platform
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "types.h"

namespace Frontend
{

// a save the core wrote to, written out a moment after its last change (like the app's SaveManager)
struct SaveFile
{
    std::string Path;
    std::vector<melonDS::u8> Data;
    bool Dirty = false;
    melonDS::u64 LastWriteMs = 0;
};

struct State
{
    SaveFile NDSSave;
    SaveFile GBASave;
    bool StopRequested = false;
};

extern State App;
extern FILE* LogFile;
extern bool VerboseLog;

// writes saves that haven't changed for a second, or all of them with force
void FlushSaves(bool force);

}

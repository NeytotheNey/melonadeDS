// Secrets kept in settings.json (the RetroAchievements Web API key): encrypted with Windows' DPAPI (CryptProtectData, for the Windows user
// who saved it, so the file alone can't be read on another account or PC) and written as base64. Any thread
#pragma once

#include <string>

namespace Frontend
{

// [plain] encrypted, as base64; empty when DPAPI fails
std::string ProtectSecret(const std::string& plain);
// the other way; false when [stored] isn't one (another user's, damaged, or empty)
bool UnprotectSecret(const std::string& stored, std::string& plain);

}

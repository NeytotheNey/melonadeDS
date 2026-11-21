#include "Secret.h"

#include <windows.h>
#include <wincrypt.h>

#include <vector>

namespace Frontend
{

namespace
{

// mixed into the encryption, so another program's DPAPI blob for the same user can't stand in for ours
const char kEntropy[] = "melonadeDS desktop: RetroAchievements Web API key";

DATA_BLOB Entropy()
{
    DATA_BLOB blob;
    blob.pbData = (BYTE*)kEntropy;
    blob.cbData = sizeof(kEntropy) - 1;
    return blob;
}

}

std::string ProtectSecret(const std::string& plain)
{
    DATA_BLOB in {(DWORD)plain.size(), (BYTE*)plain.data()}, out {}, entropy = Entropy();
    if (!CryptProtectData(&in, L"melonadeDS", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return "";
    DWORD chars = 0;
    std::string text;
    if (CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &chars))
    {
        text.resize(chars);
        if (CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, text.data(), &chars))
            text.resize(chars);
        else
            text.clear();
    }
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return text;
}

bool UnprotectSecret(const std::string& stored, std::string& plain)
{
    plain.clear();
    if (stored.empty())
        return false;
    DWORD bytes = 0;
    if (!CryptStringToBinaryA(stored.c_str(), (DWORD)stored.size(), CRYPT_STRING_BASE64, nullptr, &bytes, nullptr, nullptr))
        return false;
    std::vector<BYTE> blob(bytes);
    if (!CryptStringToBinaryA(stored.c_str(), (DWORD)stored.size(), CRYPT_STRING_BASE64, blob.data(), &bytes, nullptr, nullptr))
        return false;
    DATA_BLOB in {bytes, blob.data()}, out {}, entropy = Entropy();
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;
    plain.assign((const char*)out.pbData, out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
}

}

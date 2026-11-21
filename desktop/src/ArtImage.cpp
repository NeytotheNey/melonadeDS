#include "ArtImage.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <mutex>

// only the decoders art comes in, no HDR or the file-name API (paths go through _wfopen here)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_MAX_DIMENSIONS 4096
#include "stb/stb_image.h"

namespace Frontend
{

namespace
{

// GUID_ContainerFormatWebp, which older SDK headers lack
const GUID kContainerWebp = {0xe094b0e2, 0x67f2, 0x45b3, {0xb0, 0xea, 0x11, 0x53, 0x37, 0xca, 0x7c, 0xf3}};

bool IsWebp(const uint8_t* data, size_t size)
{
    return size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0;
}

bool WithinCaps(int64_t w, int64_t h)
{
    return w > 0 && h > 0 && w <= kMaxArtSide && h <= kMaxArtSide && w * h <= kMaxArtPixels;
}

// COM for the calling thread while it's in scope (the threads here are workers; one already in COM keeps its own)
struct ComScope
{
    ComScope() { hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
    HRESULT hr;
};

template <typename T> struct Com
{
    T* P = nullptr;
    ~Com()
    {
        if (P)
            P->Release();
    }
    T** operator&() { return &P; }
    T* operator->() { return P; }
};

// a WebP through WIC: its size (checked against the caps) and, with [rgba], its pixels. False when it can't be read
bool DecodeWebp(const uint8_t* data, size_t size, int& w, int& h, std::vector<uint8_t>* rgba)
{
    ComScope com;
    Com<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return false;
    Com<IWICStream> stream;
    Com<IWICBitmapDecoder> decoder;
    Com<IWICBitmapFrameDecode> frame;
    UINT fw = 0, fh = 0;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory((BYTE*)data, (DWORD)size))
        || FAILED(factory->CreateDecoder(kContainerWebp, nullptr, &decoder))
        || FAILED(decoder->Initialize(stream.P, WICDecodeMetadataCacheOnDemand)) || FAILED(decoder->GetFrame(0, &frame))
        || FAILED(frame->GetSize(&fw, &fh)) || !WithinCaps(fw, fh))
        return false;
    w = (int)fw;
    h = (int)fh;
    if (!rgba)
        return true;
    Com<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter))
        || FAILED(converter->Initialize(frame.P, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return false;
    rgba->resize((size_t)fw * fh * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, fw * 4, (UINT)rgba->size(), rgba->data()));
}

bool ReadWhole(const std::filesystem::path& file, std::vector<uint8_t>& data)
{
    FILE* f = _wfopen(file.c_str(), L"rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0 && size < (64l << 20))
    {
        data.resize((size_t)size);
        if (fread(data.data(), 1, data.size(), f) != data.size())
            data.clear();
    }
    fclose(f);
    return !data.empty();
}

}

bool WebpSupported()
{
    static std::once_flag once;
    static bool supported = false;
    std::call_once(once, []
    {
        ComScope com;
        Com<IWICImagingFactory> factory;
        Com<IWICBitmapDecoder> decoder;
        supported = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
            && SUCCEEDED(factory->CreateDecoder(kContainerWebp, nullptr, &decoder));
        printf("art: %s\n", supported ? "WebP images through Windows' WIC decoder" : "this Windows has no WebP decoder: WebP art is skipped");
        fflush(stdout);
    });
    return supported;
}

ImageCheck CheckImage(const uint8_t* data, size_t size)
{
    if (!data || size == 0 || size > (size_t)INT32_MAX)
        return ImageCheck::NotAnImage;
    if (IsWebp(data, size))
    {
        if (!WebpSupported())
            return ImageCheck::Unsupported;
        int w = 0, h = 0;
        return DecodeWebp(data, size, w, h, nullptr) ? ImageCheck::Ok : ImageCheck::NotAnImage;
    }
    int w = 0, h = 0, n = 0;
    return stbi_info_from_memory(data, (int)size, &w, &h, &n) && WithinCaps(w, h) ? ImageCheck::Ok : ImageCheck::NotAnImage;
}

bool LoadArt(const std::filesystem::path& file, int maxSide, bool topScreenOnly, ArtPixels& out)
{
    out = {};
    std::vector<uint8_t> data;
    if (!ReadWhole(file, data))
        return false;
    int w = 0, h = 0, n = 0;
    std::vector<uint8_t> webp;
    stbi_uc* decoded = nullptr;
    const uint8_t* pixels = nullptr;
    if (IsWebp(data.data(), data.size()))
    {
        if (!WebpSupported() || !DecodeWebp(data.data(), data.size(), w, h, &webp))
            return false;
        pixels = webp.data();
    }
    else
    {
        // the size first: nothing over the caps gets decoded
        if (!stbi_info_from_memory(data.data(), (int)data.size(), &w, &h, &n) || !WithinCaps(w, h))
            return false;
        decoded = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &n, 4);
        if (!decoded)
            return false;
        pixels = decoded;
    }
    // both screens stacked (taller than 1.4 times the width): the top one
    int srcH = topScreenOnly && h * 10 >= w * 14 ? h / 2 : h;
    float scale = std::min(1.f, (float)maxSide / (float)std::max(w, srcH));
    int dw = std::max(1, (int)(w * scale + 0.5f)), dh = std::max(1, (int)(srcH * scale + 0.5f));
    out.Width = dw;
    out.Height = dh;
    out.Rgba.resize((size_t)dw * dh * 4);
    if (dw == w && dh == srcH)
        std::copy(pixels, pixels + (size_t)w * srcH * 4, out.Rgba.begin());
    else
    {
        // each destination pixel the average of the source pixels it covers (premultiplied, so transparent edges don't darken)
        for (int y = 0; y < dh; y++)
        {
            int y0 = y * srcH / dh, y1 = std::max(y0 + 1, (y + 1) * srcH / dh);
            for (int x = 0; x < dw; x++)
            {
                int x0 = x * w / dw, x1 = std::max(x0 + 1, (x + 1) * w / dw);
                uint64_t r = 0, g = 0, b = 0, a = 0;
                for (int sy = y0; sy < y1; sy++)
                {
                    const uint8_t* p = pixels + ((size_t)sy * w + x0) * 4;
                    for (int sx = x0; sx < x1; sx++, p += 4)
                    {
                        r += p[0] * p[3];
                        g += p[1] * p[3];
                        b += p[2] * p[3];
                        a += p[3];
                    }
                }
                uint8_t* d = &out.Rgba[((size_t)y * dw + x) * 4];
                uint64_t count = (uint64_t)(x1 - x0) * (y1 - y0);
                d[0] = a ? (uint8_t)(r / a) : 0;
                d[1] = a ? (uint8_t)(g / a) : 0;
                d[2] = a ? (uint8_t)(b / a) : 0;
                d[3] = (uint8_t)(a / count);
            }
        }
    }
    if (decoded)
        stbi_image_free(decoded);
    return true;
}

}

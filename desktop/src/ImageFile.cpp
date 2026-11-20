#include "ImageFile.h"

#include <windows.h>
#include <wincodec.h>

#include <cstdio>
#include <system_error>

using namespace melonDS;

namespace Frontend
{

namespace
{

// a COM pointer that releases itself
template <typename T> struct Com
{
    T* Ptr = nullptr;
    ~Com() { if (Ptr) Ptr->Release(); }
    T** operator&() { return &Ptr; }
    T* operator->() const { return Ptr; }
    explicit operator bool() const { return Ptr != nullptr; }
};

bool MakeFactory(Com<IWICImagingFactory>& factory)
{
    // the UI thread's COM is set up in main; a thread without it gets it here (and keeps it)
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&factory);
    if (hr == CO_E_NOTINITIALIZED)
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&factory);
    }
    return SUCCEEDED(hr) && factory;
}

}

bool WritePng(const std::filesystem::path& path, const std::vector<u8>& rgba, int width, int height)
{
    if (width <= 0 || height <= 0 || rgba.size() < (size_t)width * height * 4)
        return false;
    Com<IWICImagingFactory> factory;
    if (!MakeFactory(factory))
        return false;

    // written next to it first, so a failed write never leaves half a file in its place
    std::filesystem::path temp = path;
    temp += L".tmp";
    bool ok = false;
    {
        Com<IWICStream> stream;
        Com<IWICBitmapEncoder> encoder;
        Com<IWICBitmapFrameEncode> frame;
        Com<IPropertyBag2> props;
        // the PNG encoder takes BGRA everywhere (RGBA only on newer versions)
        std::vector<u8> bgra(rgba.begin(), rgba.begin() + (size_t)width * height * 4);
        for (size_t i = 0; i < bgra.size(); i += 4)
            std::swap(bgra[i], bgra[i + 2]);
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        ok = SUCCEEDED(factory->CreateStream(&stream))
            && SUCCEEDED(stream->InitializeFromFilename(temp.c_str(), GENERIC_WRITE))
            && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
            && SUCCEEDED(encoder->Initialize(stream.Ptr, WICBitmapEncoderNoCache))
            && SUCCEEDED(encoder->CreateNewFrame(&frame, &props))
            && SUCCEEDED(frame->Initialize(props.Ptr))
            && SUCCEEDED(frame->SetSize((UINT)width, (UINT)height))
            && SUCCEEDED(frame->SetPixelFormat(&format))
            && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)
            && SUCCEEDED(frame->WritePixels((UINT)height, (UINT)width * 4, (UINT)bgra.size(), bgra.data()))
            && SUCCEEDED(frame->Commit())
            && SUCCEEDED(encoder->Commit());
    }
    std::error_code ec;
    if (ok)
        std::filesystem::rename(temp, path, ec);
    if (!ok || ec)
    {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

bool ReadImage(const std::filesystem::path& path, std::vector<u8>& rgba, int& width, int& height)
{
    Com<IWICImagingFactory> factory;
    if (!MakeFactory(factory))
        return false;
    Com<IWICBitmapDecoder> decoder;
    Com<IWICBitmapFrameDecode> frame;
    Com<IWICFormatConverter> converter;
    UINT w = 0, h = 0;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))
        || FAILED(decoder->GetFrame(0, &frame))
        || FAILED(factory->CreateFormatConverter(&converter))
        || FAILED(converter->Initialize(frame.Ptr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))
        || FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0 || w > 8192 || h > 8192)
        return false;
    rgba.resize((size_t)w * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)rgba.size(), rgba.data())))
        return false;
    width = (int)w;
    height = (int)h;
    return true;
}

}

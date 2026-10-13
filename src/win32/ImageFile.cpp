#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "triggerlab/ImageFile.hpp"

namespace triggerlab {
namespace {
struct ComScope {
    HRESULT status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~ComScope() {if(SUCCEEDED(status)) CoUninitialize();}
};
}
ImageFileResult loadImageColor(const std::filesystem::path& path) {
    using Microsoft::WRL::ComPtr;
    auto fail=[](const wchar_t* error) {return ImageFileResult{{},error};};
    try {
        std::error_code error;
        if(!std::filesystem::is_regular_file(path,error) || error) return fail(L"Не удалось открыть файл изображения");
        const auto size=std::filesystem::file_size(path,error);
        if(error || !size || size>64ULL*1024*1024) return fail(L"Изображение должно быть непустым и не больше 64 МБ");
        ComScope com;if(FAILED(com.status) && com.status!=RPC_E_CHANGED_MODE) return fail(L"Не удалось запустить декодер изображения");
        ComPtr<IWICImagingFactory> factory;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf())))) return fail(L"Декодер изображения недоступен");
        ComPtr<IWICBitmapDecoder> decoder;
        if(FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,decoder.GetAddressOf()))) return fail(L"Файл повреждён или формат не поддерживается");
        GUID format{};
        if(FAILED(decoder->GetContainerFormat(&format)) ||
           (format!=GUID_ContainerFormatPng && format!=GUID_ContainerFormatJpeg && format!=GUID_ContainerFormatBmp && format!=GUID_ContainerFormatGif && format!=GUID_ContainerFormatTiff)) return fail(L"Поддерживаются PNG, JPEG, BMP, GIF и TIFF");
        ComPtr<IWICBitmapFrameDecode> frame;
        if(FAILED(decoder->GetFrame(0,frame.GetAddressOf()))) return fail(L"Не удалось прочитать первый кадр изображения");
        UINT width=0,height=0;
        if(FAILED(frame->GetSize(&width,&height)) || !width || !height || width>4096 || height>4096) return fail(L"Размер изображения: от 1 до 4096 пикселей по каждой стороне");
        ComPtr<IWICFormatConverter> converter;
        if(FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
           FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return fail(L"Не удалось преобразовать изображение в RGB");
        Frame pixels(static_cast<int>(width),static_cast<int>(height));
        if(FAILED(converter->CopyPixels(nullptr,width*4,width*height*4,reinterpret_cast<BYTE*>(pixels.data())))) return fail(L"Не удалось прочитать пиксели изображения");
        auto selected=dominantImageColor(pixels);
        if(!selected) return fail(L"В изображении нет пикселей с непрозрачностью не менее 50%");
        return {selected,{}};
    } catch(const std::exception&) {return fail(L"Ошибка чтения изображения");}
}
}

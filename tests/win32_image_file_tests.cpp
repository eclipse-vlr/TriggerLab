#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "triggerlab/ImageFile.hpp"
#include "triggerlab/TriggerEngine.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
void check(bool value,const char* message) {if(!value) {std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    using namespace triggerlab;using Microsoft::WRL::ComPtr;
    const auto dir=std::filesystem::temp_directory_path()/(L"triggerlab-image-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    const auto bmp=dir/L"образец.bmp";
    BITMAPFILEHEADER header{};header.bfType=0x4D42;header.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+12;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=3;info.biHeight=1;info.biPlanes=1;info.biBitCount=24;info.biSizeImage=12;
    const unsigned char pixels[]={19,67,231,19,67,231,255,0,0,0,0,0};
    {std::ofstream out(bmp,std::ios::binary);out.write(reinterpret_cast<const char*>(&header),sizeof(header));out.write(reinterpret_cast<const char*>(&info),sizeof(info));out.write(reinterpret_cast<const char*>(pixels),sizeof(pixels));check(bool(out),"fixture written");}
    auto loaded=loadImageColor(bmp);
    check(loaded.selected && loaded.selected->color.red==231 && loaded.selected->color.blue==19 && loaded.selected->coverage==2./3,"real BMP decoding, Unicode path, dominant BGRA color");
    check(!loadImageColor(dir/L"missing.png").selected,"missing file rejected");
    check(!loadImageColor(dir).selected,"directory rejected");
    const auto broken=dir/L"broken.png";{std::ofstream out(broken);out<<"not a picture";}
    check(!loadImageColor(broken).selected,"corrupt file rejected");
    // Encode an actual transparent PNG with WIC to verify the decoder keeps alpha.
    check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialized");
    {
        ComPtr<IWICImagingFactory> factory;
        check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf()))),"WIC factory");
        ComPtr<IWICStream> stream;check(SUCCEEDED(factory->CreateStream(stream.GetAddressOf())),"PNG stream");
        const auto png=dir/L"transparent.png";
        check(SUCCEEDED(stream->InitializeFromFilename(png.c_str(),GENERIC_WRITE)),"PNG file opened");
        ComPtr<IWICBitmapEncoder> encoder;check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf())),"PNG encoder");
        check(SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)),"PNG encoder initialized");
        ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> options;
        check(SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(),options.GetAddressOf())) && SUCCEEDED(frame->Initialize(options.Get())) && SUCCEEDED(frame->SetSize(1,1)),"PNG frame");
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
        check(SUCCEEDED(frame->SetPixelFormat(&format)) && format==GUID_WICPixelFormat32bppBGRA,"PNG BGRA alpha");
        BYTE transparent[]={0,0,255,0};
        check(SUCCEEDED(frame->WritePixels(1,4,4,transparent)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()),"transparent PNG encoded");
    }
    CoUninitialize();
    loaded=loadImageColor(dir/L"transparent.png");
    check(!loaded.selected && !loaded.error.empty(),"fully transparent PNG rejected rather than selecting hidden red");
    std::filesystem::resize_file(broken,64ULL*1024*1024+1);
    check(!loadImageColor(broken).selected,"oversized encoded file rejected");
    std::filesystem::remove_all(dir);
    std::cout<<"PASS: WIC image file loading and errors\n";
}

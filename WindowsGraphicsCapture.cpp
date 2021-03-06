#include "pch.h"
#include "ScreenCaptureTest.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <Windows.Graphics.Capture.Interop.h>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::System;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Graphics::Capture;

class GraphicsCapture
{
public:
    using Callback = std::function<void(ID3D11Texture2D*, int w, int h)>;

    GraphicsCapture();
    ~GraphicsCapture();
    bool start(HWND hwnd, bool free_threaded, const Callback& callback);
    bool start(HMONITOR hmon, bool free_threaded, const Callback& callback);
    void stop();

private:
    template<class CreateCaptureItem>
    bool startImpl(bool free_threaded, const Callback& callback, const CreateCaptureItem& cci);

    void onFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const& args);

private:
    com_ptr<ID3D11Device> m_device;
    com_ptr<ID3D11DeviceContext> m_context;

    IDirect3DDevice m_device_rt{ nullptr };
    Direct3D11CaptureFramePool m_frame_pool{ nullptr };
    GraphicsCaptureItem m_capture_item{ nullptr };
    GraphicsCaptureSession m_capture_session{ nullptr };
    Direct3D11CaptureFramePool::FrameArrived_revoker m_frame_arrived;

    Callback m_callback;
};

GraphicsCapture::GraphicsCapture()
{
    // create device
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    flags |= D3D11_CREATE_DEVICE_DEBUG;

    ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, m_device.put(), nullptr, nullptr);
    m_device->GetImmediateContext(m_context.put());

    auto dxgi = m_device.as<IDXGIDevice>();
    com_ptr<::IInspectable> device_rt;
    ::CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), device_rt.put());
    m_device_rt = device_rt.as<IDirect3DDevice>();
}

GraphicsCapture::~GraphicsCapture()
{
    stop();
}

void GraphicsCapture::stop()
{
    m_frame_arrived.revoke();
    m_capture_session = nullptr;
    if (m_frame_pool) {
        m_frame_pool.Close();
        m_frame_pool = nullptr;
    }
    m_capture_item = nullptr;
    m_callback = {};
}

template<class CreateCaptureItem>
bool GraphicsCapture::startImpl(bool free_threaded, const Callback& callback, const CreateCaptureItem& cci)
{
    stop();
    m_callback = callback;

    sctProfile("GraphicsCapture::start");
    // create capture item
    auto factory = get_activation_factory<GraphicsCaptureItem>();
    auto interop = factory.as<IGraphicsCaptureItemInterop>();
    cci(interop);

    if (m_capture_item) {
        // create frame pool
        auto size = m_capture_item.Size();
        if (free_threaded)
            m_frame_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(m_device_rt, DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
        else
            m_frame_pool = Direct3D11CaptureFramePool::Create(m_device_rt, DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
        m_frame_arrived = m_frame_pool.FrameArrived(auto_revoke, { this, &GraphicsCapture::onFrameArrived });

        // capture start
        m_capture_session = m_frame_pool.CreateCaptureSession(m_capture_item);
        m_capture_session.StartCapture();
        return true;
    }
    else {
        return false;
    }
}

bool GraphicsCapture::start(HWND hwnd, bool free_threaded, const Callback& callback)
{
    return startImpl(free_threaded, callback, [&](auto interop) {
        interop->CreateForWindow(hwnd, guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), put_abi(m_capture_item));
        });

}

bool GraphicsCapture::start(HMONITOR hmon, bool free_threaded, const Callback& callback)
{
    return startImpl(free_threaded, callback, [&](auto interop) {
        interop->CreateForMonitor(hmon, guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), put_abi(m_capture_item));
        });
}

void GraphicsCapture::onFrameArrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const& args)
{
    sctProfile("GraphicsCapture::onFrameArrived");
    auto frame = sender.TryGetNextFrame();
    auto size = frame.ContentSize();

    com_ptr<ID3D11Texture2D> surface;
    frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>()->GetInterface(guid_of<ID3D11Texture2D>(), surface.put_void());
    m_callback(surface.get(), size.Width, size.Height);
}


void TestGraphicsCapture()
{
    HMONITOR target = ::MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

    GraphicsCapture capture;

    // single threaded
    {
        bool arrived = false;

        // called from DispatchMessage()
        auto callback = [&](ID3D11Texture2D* surface, int w, int h) {
            ReadTexture(surface, w, h, [&](void* data, int stride) {
                SaveAsPNG("GraphicsCapture.png", w, h, stride, data);
                });
            arrived = true;
        };

        if (capture.start(target, false, callback)) {
            MSG msg;
            while (!arrived) {
                ::GetMessage(&msg, nullptr, 0, 0);
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
            }

            capture.stop();
        }
    }

    // free threaded
    {
        std::mutex mutex;
        std::condition_variable cond;

        // called from capture thread
        auto callback = [&](ID3D11Texture2D* surface, int w, int h) {
            ReadTexture(surface, w, h, [&](void* data, int stride) {
                SaveAsPNG("GraphicsCapture_FreeThreaded.png", w, h, stride, data);
                });
            cond.notify_one();
        };

        std::unique_lock<std::mutex> lock(mutex);
        if (capture.start(target, true, callback)) {
            cond.wait(lock);

            // stop() must be called from the thread created GraphicsCapture
            capture.stop();
        }
    }
}

#include "GraphicsProbe.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#endif

namespace plynth::app {

// Creates a Direct3D 11 device on the first graphics card and reads its name.
plynth::GraphicsProbe probeGraphics()
{
    plynth::GraphicsProbe probe;
#ifdef Q_OS_WIN
    using Microsoft::WRL::ComPtr;
    probe.api = "Direct3D 11";
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        probe.problem = "The graphics system of Windows could not be started.";
        return probe;
    }
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapters1(0, &adapter))) {
        probe.problem = "No graphics card was found.";
        return probe;
    }
    DXGI_ADAPTER_DESC1 description{};
    adapter->GetDesc1(&description);
    probe.device = QString::fromWCharArray(description.Description).toStdString();

    const D3D_FEATURE_LEVEL wanted[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL got{};
    const HRESULT created = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, wanted, 2,
                                              D3D11_SDK_VERSION, &device, &got, nullptr);
    if (FAILED(created)) {
        probe.problem = "The graphics card or its driver cannot run the 3D preview.";
        return probe;
    }
    probe.ok = true;
#else
    probe.problem = "The 3D preview is only checked on Windows.";
#endif
    return probe;
}

// The probe made once, the first time something asks.
const plynth::GraphicsProbe& appGraphics()
{
    static const plynth::GraphicsProbe probe = probeGraphics();
    return probe;
}

}

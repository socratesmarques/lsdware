#pragma once
#include "lsd/core.hpp"
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>
namespace lsd {
class Overlay {
  public:
    Overlay();
    ~Overlay();
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;
    bool pump();
    void align(HWND target, bool menu);
    void begin();
    void end();
    Vec2 size() const {
        return {static_cast<float>(width_), static_cast<float>(height_)};
    }
    HWND window() const {
        return window_;
    }
    bool foreground(HWND target) const {
        return GetForegroundWindow() == target;
    }

  private:
    void target();
    void resize(unsigned w, unsigned h);
    HWND window_{};
    unsigned width_{1280}, height_{800};
    bool interactive_{false};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> composition_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> composition_target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
};
} // namespace lsd

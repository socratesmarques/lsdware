#include "lsd/overlay_win.hpp"
#include <algorithm>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <stdexcept>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace lsd {
namespace {
void check(HRESULT result, const char* action) {
    if (FAILED(result))
        throw std::runtime_error(std::string(action) + " HRESULT " +
                                 std::to_string(static_cast<unsigned long>(result)));
}
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
        return TRUE;
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_SYSCOMMAND && (wparam & 0xFFF0) == SC_KEYMENU)
        return 0;
    if (message == WM_NCHITTEST && (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TRANSPARENT))
        return HTTRANSPARENT;
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace
Overlay::Overlay() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = procedure;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LSDWareOverlay";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc))
        throw std::runtime_error("RegisterClassEx failed");
    window_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        wc.lpszClassName, L"LSDWare training", WS_POPUP, 80, 80, static_cast<int>(width_),
        static_cast<int>(height_), nullptr, nullptr, wc.hInstance, nullptr);
    if (!window_)
        throw std::runtime_error("CreateWindowEx failed");
    try {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL level{};
        auto result =
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                              levels, 1, D3D11_SDK_VERSION, &device_, &level, &context_);
        if (FAILED(result))
            check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    levels, 1, D3D11_SDK_VERSION, &device_, &level, &context_),
                  "D3D11CreateDevice");
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        check(device_.As(&dxgi), "Query IDXGIDevice");
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        check(dxgi->GetAdapter(&adapter), "GetAdapter");
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        check(adapter->GetParent(IID_PPV_ARGS(&factory)), "GetFactory");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width_;
        desc.Height = height_;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        check(factory->CreateSwapChainForComposition(device_.Get(), &desc, nullptr, &swap_),
              "CreateSwapChainForComposition");
        check(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&composition_)), "DCompositionCreateDevice");
        check(composition_->CreateTargetForHwnd(window_, TRUE, &composition_target_), "Composition target");
        check(composition_->CreateVisual(&visual_), "Composition visual");
        check(visual_->SetContent(swap_.Get()), "Composition content");
        check(composition_target_->SetRoot(visual_.Get()), "Composition root");
        check(composition_->Commit(), "Composition commit");
        target();
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui::GetStyle().WindowRounding = 8;
        if (!ImGui_ImplWin32_Init(window_))
            throw std::runtime_error("ImGui Win32 initialization failed");
        if (!ImGui_ImplDX11_Init(device_.Get(), context_.Get()))
            throw std::runtime_error("ImGui DX11 initialization failed");
        ShowWindow(window_, SW_SHOWNOACTIVATE);
    } catch (...) {
        if (ImGui::GetCurrentContext()) {
            if (ImGui::GetIO().BackendRendererUserData)
                ImGui_ImplDX11_Shutdown();
            if (ImGui::GetIO().BackendPlatformUserData)
                ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        DestroyWindow(window_);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        throw;
    }
}
Overlay::~Overlay() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyWindow(window_);
    UnregisterClassW(L"LSDWareOverlay", GetModuleHandleW(nullptr));
}
void Overlay::target() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    check(swap_->GetBuffer(0, IID_PPV_ARGS(&texture)), "GetBuffer");
    check(device_->CreateRenderTargetView(texture.Get(), nullptr, &render_target_), "CreateRenderTargetView");
}
void Overlay::resize(unsigned w, unsigned h) {
    if (!w || !h || (w == width_ && h == height_))
        return;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    render_target_.Reset();
    check(swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
    width_ = w;
    height_ = h;
    target();
}
bool Overlay::pump() {
    MSG m{};
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT)
            return false;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return true;
}
void Overlay::align(HWND game, bool menu) {
    if (menu != interactive_) {
        auto style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
        style = menu ? style & ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)
                     : style | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        SetWindowLongPtrW(window_, GWL_EXSTYLE, style);
        SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (menu) {
            ShowWindow(window_, SW_SHOW);
            SetForegroundWindow(window_);
        } else if (game)
            SetForegroundWindow(game);
        interactive_ = menu;
    }
    if (game && IsWindow(game) && !IsIconic(game)) {
        RECT rect{};
        GetClientRect(game, &rect);
        POINT origin{0, 0};
        ClientToScreen(game, &origin);
        const auto w = static_cast<unsigned>(std::max(1L, rect.right)),
                   h = static_cast<unsigned>(std::max(1L, rect.bottom));
        resize(w, h);
        SetWindowPos(window_, HWND_TOPMOST, origin.x, origin.y, static_cast<int>(w), static_cast<int>(h),
                     SWP_NOACTIVATE);
    }
    const bool show = menu || (!game) || (!IsIconic(game) && foreground(game));
    ShowWindow(window_, show ? SW_SHOWNOACTIVATE : SW_HIDE);
}
void Overlay::begin() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}
void Overlay::end() {
    ImGui::Render();
    const float clear[4] = {0, 0, 0, 0};
    context_->OMSetRenderTargets(1, render_target_.GetAddressOf(), nullptr);
    context_->ClearRenderTargetView(render_target_.Get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    check(swap_->Present(1, 0), "Present");
}
} // namespace lsd

#include <windows.h>
#include <d3d11.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "OpenCVCam.h"

#pragma comment(lib, "d3d11.lib")

// RAII guard to ensure every PushStyleColor is popped even on early exits
struct ImGuiStyleColorGuard
{
    ImGuiStyleColorGuard(ImGuiCol idx, const ImVec4& col)
    {
        ImGui::PushStyleColor(idx, col);
        pushed = 1;
    }
    ~ImGuiStyleColorGuard()
    {
        if (pushed)
            ImGui::PopStyleColor();
    }
    // disable copy
    ImGuiStyleColorGuard(const ImGuiStyleColorGuard&) = delete;
    ImGuiStyleColorGuard& operator=(const ImGuiStyleColorGuard&) = delete;
private:
    int pushed = 0;
};

// ------------------------------------------------------------
// DirectX 11 globals
// ------------------------------------------------------------

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// ------------------------------------------------------------
// Forward declarations
// ------------------------------------------------------------

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();

LRESULT WINAPI WndProc(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);

// ------------------------------------------------------------
// Application state
// ------------------------------------------------------------

static bool g_running = true;
static bool g_darkMode = true;

static int g_selectedCameraIndex = 0;
static bool g_openmodal = false;
static float g_bgColor[4] = { 0.08f, 0.08f, 0.08f, 1.0f };

static bool g_recorderWindowOpen = false;
static std::string g_videoFileName = "output.avi";

static int g_width = 640;
static int g_height = 480;

// ------------------------------------------------------------
// WinMain
// ------------------------------------------------------------

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    LPSTR,
    int nCmdShow
)
{
    // --------------------------------------------------------
    // Register window class
    // --------------------------------------------------------

    WNDCLASSEXW wc =
    {
        sizeof(WNDCLASSEXW),
        CS_CLASSDC,
        WndProc,
        0L,
        0L,
        hInstance,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        L"WebCamObscura",
        nullptr
    };

    ::RegisterClassExW(&wc);

    // --------------------------------------------------------
    // Create window
    // --------------------------------------------------------

    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName,
        L"WebCamObscura",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1100,
        700,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );

    if (!hwnd)
    {
        ::UnregisterClassW(
            wc.lpszClassName,
            wc.hInstance
        );

        return 1;
    }

    // --------------------------------------------------------
    // Initialize DirectX 11
    // --------------------------------------------------------

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();

        ::DestroyWindow(hwnd);

        ::UnregisterClassW(
            wc.lpszClassName,
            wc.hInstance
        );

        return 1;
    }

    ::ShowWindow(hwnd, nCmdShow);
    ::UpdateWindow(hwnd);

    // --------------------------------------------------------
    // Initialize Dear ImGui
    // --------------------------------------------------------

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // --------------------------------------------------------
    // Dear ImGui style
    // --------------------------------------------------------

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 6.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.GrabRounding = 4.0f;

    // --------------------------------------------------------
    // Initialize platform/renderer backends
    // --------------------------------------------------------

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX11_Init(
        g_pd3dDevice,
        g_pd3dDeviceContext
    );

    // Create persistent camera instance (moved outside the main loop)
    OpenCVCam cam;

    // --------------------------------------------------------
    // Main loop
    // --------------------------------------------------------

    MSG msg{};

    while (g_running)
    {
        // ----------------------------------------------------
        // Windows messages
        // ----------------------------------------------------

        while (::PeekMessage(
            &msg,
            nullptr,
            0U,
            0U,
            PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
                g_running = false;
        }

        if (!g_running)
            break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();

        ImGui::NewFrame();

        // ====================================================
        // Main application window
        // ====================================================

        ImGui::SetNextWindowPos(
            ImVec2(60, 60),
            ImGuiCond_FirstUseEver
        );

        ImGui::SetNextWindowSize(
            ImVec2(400, 400),
            ImGuiCond_FirstUseEver
        );

        //ImVec4 bgCol = ImVec4(g_bgColor[0], g_bgColor[1], g_bgColor[2], g_bgColor[3]);
        //ImGuiStyleColorGuard guard(ImGuiCol_WindowBg, bgCol);

        //ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(g_bgColor[0], g_bgColor[1], g_bgColor[2], g_bgColor[3]));

        ImGui::Begin(
            "Start WebCamObscura",
            nullptr,
            ImGuiWindowFlags_MenuBar
        );

        // ----------------------------------------------------
        // Menu bar
        // ----------------------------------------------------

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit"))
                {
                    if (MessageBoxA(hwnd, "Are you sure you want to close the application?", "Confirm Close", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        g_running = false;
                        ::DestroyWindow(hwnd);
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
				ImGui::MenuItem("Video Recorder", nullptr, &g_recorderWindowOpen);
                ImGui::Separator();
                ImGui::MenuItem("Dark Mode", nullptr, &g_darkMode);
                if (ImGui::MenuItem("Change BG Color"))
					g_openmodal = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About"))
                {
                    MessageBoxA(
                        hwnd,
                        "WebCamObscura\n\n"
                        "A Camera and Video Capture Software\n\n"
                        "Made with Dear ImGui with Win32 and DirectX 11 and OpenCV.\n\n"
                        "Created by: Jarred",
                        "About WebCamObscura",
                        MB_OK | MB_ICONINFORMATION
                    );
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
        
        // ----------------------------------------------------
        // Change style
        // ----------------------------------------------------

        if (g_darkMode)
            ImGui::StyleColorsDark();
        else
            ImGui::StyleColorsLight();

        // ----------------------------------------------------
        // Header
        // ----------------------------------------------------

        ImGui::Spacing();

        ImGui::Text(
            "Welcome to WebCamObscura"
        );

        ImGui::Separator();

        ImGui::Spacing();

        // ----------------------------------------------------
        // Camera Selection
        // ----------------------------------------------------

        ImGui::Text("Select Camera:");

        ImGui::Indent();

        ImGui::DragInt(
            "Camera Index",
            &g_selectedCameraIndex,
            1.0f,
            0,
            10
        );

        ImGui::Unindent();

        ImGui::Spacing();

        ImGui::Text("Resolution: ");
        ImGui::Indent();

        ImGui::DragInt(
            "Width",
            &g_width,
            1.0f,
            320,
            1920
        );

        //ImGui::SameLine();

        ImGui::DragInt(
            "Height",
            &g_height,
            1.0f,
            320,
            1920
        );

        ImGui::Unindent();
        ImGui::Spacing();

        // ----------------------------------------------------
        // Start/Stop Button
        // ----------------------------------------------------

        if (ImGui::Button(
            "Start Viewing",
            ImVec2(150, 40)
        ))
        {
			if (cam.isCameraIdExist(g_selectedCameraIndex) == false)
			{
				MessageBoxA(
					hwnd,
					"The selected camera index does not exist.\n"
                    "Please select a valid camera index or insert/reinsert the video capture device.",
					"Error",
					MB_OK | MB_ICONERROR
				);
				continue;
			}

            try {
                cam.setCameraId(g_selectedCameraIndex);
                cam.setResolution(g_width, g_height);
                cam.start();
            }
            catch (const std::exception& e) {
                MessageBoxA(
                    hwnd,
                    e.what(),
                    "Error",
                    MB_OK | MB_ICONERROR
                );
            }
        }

        ImGui::SameLine();

        if (ImGui::Button(
            "Stop Viewing",
            ImVec2(150, 40)
        ))
        {
            cam.stop();
        }

        ImGui::Spacing();

        // ----------------------------------------------------
        // Status panel
        // ----------------------------------------------------

        ImGui::BeginChild(
            "StatusPanel",
            ImVec2(0, 120),
            true
        );

        ImGui::Text("Status and Information");

        ImGui::Separator();

        ImGui::Text(
            "Camera Index: %d",
            g_selectedCameraIndex
        );

        ImGui::Text(
            "Resolution: %dx%d",
            g_width,
            g_height
        );

        if (cam.isOpened())
        {
            ImGui::Text(
                "Camera is Opened: Yes"
            );
        }
        else
        {
            ImGui::Text(
                "Camera is Opened: No"
            );
        }

        ImGui::EndChild();

        // ----------------------------------------------------
        // Footer
        // ----------------------------------------------------

        ImGui::Spacing();

        ImGui::Separator();

        ImGui::TextDisabled(
            "Created by: Jarred | Please put a star on my GitHub Repo!"
        );

        ImGui::End();

        // ====================================================
        // Record window
        // ====================================================

        if (g_recorderWindowOpen)
        {
            ImGui::Begin("Video Recorder", &g_recorderWindowOpen);
            
            ImGui::Text("Video File Name:");
			ImGui::Indent();
			ImGui::InputText("File Name", (char *)g_videoFileName.c_str(), g_videoFileName.length() + 1);
			ImGui::SameLine();
			if (ImGui::Button("Browse"))
			{
				OPENFILENAMEA ofn;
				CHAR szFile[260] = { 0 };
				ZeroMemory(&ofn, sizeof(ofn));
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = hwnd;
				ofn.lpstrFile = szFile;
				ofn.nMaxFile = sizeof(szFile);
				ofn.lpstrFilter = "AVI Files\0*.avi\0All Files\0*.*\0";
				ofn.nFilterIndex = 1;
				ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
				if (GetSaveFileNameA(&ofn) == TRUE)
				{
					g_videoFileName = std::string(szFile);
				}
			}
            ImGui::Unindent();

			ImGui::Spacing();

			if (ImGui::Button("Start Recording"))
			{
				try {
					cam.startRecording(g_videoFileName);
				}
				catch (const std::exception& e) {
					MessageBoxA(
						hwnd,
						e.what(),
						"Error",
						MB_OK | MB_ICONERROR
					);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Stop Recording"))
			{
				cam.stopRecording();
			}

            ImGui::Spacing();

			ImGui::Separator();

            ImGui::Text("Recording Status: %s", cam.getVideoStatus().c_str());

            ImGui::End();
        }

        // ====================================================
        // Change BG Color window
        // ====================================================

		if (g_openmodal)
		{
			ImGui::OpenPopup("Change BG Color");
			g_openmodal = false;
		}

        ImGui::SetNextWindowSize(ImVec2(300, 430), ImGuiCond_FirstUseEver);
        if (ImGui::BeginPopupModal("Change BG Color", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Select Background Color");
            ImGui::Separator();
            ImGui::ColorPicker4("##picker", g_bgColor, ImGuiColorEditFlags_AlphaBar);
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

		//ImGui::PopStyleColor();

        // ====================================================
        // Rendering
        // ====================================================

        ImGui::Render();

        //const float clearColor[4] =
        //{
        //    0.08f,
        //    0.08f,
        //    0.08f,
        //    1.0f
        //};

        g_pd3dDeviceContext->OMSetRenderTargets(
            1,
            &g_mainRenderTargetView,
            nullptr
        );

        //g_pd3dDeviceContext->ClearRenderTargetView(
        //    g_mainRenderTargetView,
        //    clearColor
        //);

        g_pd3dDeviceContext->ClearRenderTargetView(
            g_mainRenderTargetView,
            g_bgColor
        );

        ImGui_ImplDX11_RenderDrawData(
            ImGui::GetDrawData()
        );

        g_pSwapChain->Present(
            1,
            0
        );
    }

    // --------------------------------------------------------
    // Shutdown Dear ImGui
    // --------------------------------------------------------

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();

    ImGui::DestroyContext();

    // --------------------------------------------------------
    // Cleanup DirectX
    // --------------------------------------------------------

    CleanupDeviceD3D();

    ::DestroyWindow(hwnd);

    ::UnregisterClassW(
        wc.lpszClassName,
        wc.hInstance
    );

    return 0;
}

// ------------------------------------------------------------
// Create DirectX 11 device
// ------------------------------------------------------------

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};

    sd.BufferCount = 2;

    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;

    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;

    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    sd.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;

    sd.OutputWindow = hWnd;

    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;

    sd.Windowed = TRUE;

    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT createDeviceFlags = 0;

#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;

    const D3D_FEATURE_LEVEL featureLevelArray[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray,
        ARRAYSIZE(featureLevelArray),
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext
    );

    if (result != S_OK)
        return false;

    CreateRenderTarget();

    return true;
}

// ------------------------------------------------------------
// Create render target
// ------------------------------------------------------------

void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;

    HRESULT result =
        g_pSwapChain->GetBuffer(
            0,
            IID_PPV_ARGS(&backBuffer)
        );

    if (SUCCEEDED(result))
    {
        g_pd3dDevice->CreateRenderTargetView(
            backBuffer,
            nullptr,
            &g_mainRenderTargetView
        );

        backBuffer->Release();
    }
}

// ------------------------------------------------------------
// Cleanup render target
// ------------------------------------------------------------

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

// ------------------------------------------------------------
// Cleanup DirectX
// ------------------------------------------------------------

void CleanupDeviceD3D()
{
    CleanupRenderTarget();

    if (g_pSwapChain)
    {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }

    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }

    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

// ------------------------------------------------------------
// Window procedure
// ------------------------------------------------------------

extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);

LRESULT WINAPI WndProc(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
)
{
    // Let Dear ImGui process input first.
    if (ImGui_ImplWin32_WndProcHandler(
        hWnd,
        msg,
        wParam,
        lParam))
    {
        return true;
    }

    switch (msg)
    {
        case WM_SIZE:
        {
            if (g_pd3dDevice != nullptr &&
                wParam != SIZE_MINIMIZED)
            {
                CleanupRenderTarget();

                g_pSwapChain->ResizeBuffers(
                    0,
                    static_cast<UINT>(LOWORD(lParam)),
                    static_cast<UINT>(HIWORD(lParam)),
                    DXGI_FORMAT_UNKNOWN,
                    0
                );

                CreateRenderTarget();
            }

            return 0;
        }

        case WM_SYSCOMMAND:
        {
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;

            break;
        }

		case WM_CLOSE:
		{
            if (MessageBoxA(hWnd, "Are you sure you want to close the application?", "Confirm Close", MB_YESNO | MB_ICONQUESTION) == IDYES) {
				g_running = false;
				::DestroyWindow(hWnd);
            }
			return 0;
		}

        case WM_DESTROY:
        {
            g_running = false;

            ::PostQuitMessage(0);

            return 0;
        }
    }

    return ::DefWindowProcW(
        hWnd,
        msg,
        wParam,
        lParam
    );
}
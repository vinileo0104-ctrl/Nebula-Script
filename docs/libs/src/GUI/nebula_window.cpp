#include <windows.h>

#define EXPORT __declspec(dllexport)

// Procedimento da janela para lidar com os eventos do sistema operativo
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

extern "C" {
    // 1. Cria e exibe a janela nativa
    EXPORT HWND window_create(const char* title, int width, int height) {
        HINSTANCE hInstance = NULL;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&WindowProc),
            &hInstance
        );

        const char* className = "NebulaWindowClass";

        WNDCLASSA wc = {};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

        ATOM atom = RegisterClassA(&wc);
        if (atom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            MessageBoxA(NULL, "Falha ao registar a classe da janela!", "Erro FFI", MB_OK);
            return NULL;
        }

        HWND hwnd = CreateWindowExA(
            0, className, title,
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, width, height,
            NULL, NULL, hInstance, NULL
        );

        if (hwnd == NULL) {
            DWORD err = GetLastError();
            std::string errMsg = "CreateWindowEx falhou! Erro Win32: " + std::to_string(err);
            MessageBoxA(NULL, errMsg.c_str(), "Erro FFI", MB_OK);
        }

        return hwnd;
    }
    // 2. Processa a fila de eventos da janela (retorna false se o utilizador fechar a janela)
    EXPORT bool window_poll_events() {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return false;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return true;
    }
}
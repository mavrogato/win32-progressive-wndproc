
#include <iostream>
#include <complex>
#include <numbers>
#include <coroutine>

#include <windows.h>
#include <windowsx.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

inline namespace aux
{
    template <size_t...> struct seq{};
    template <size_t N, size_t... I> struct gen_seq : gen_seq<N-1, N-1, I...>{};
    template <size_t... I> struct gen_seq<0, I...> : seq<I...>{};
    template <class Ch, class Tuple, size_t... I>
    void print(std::basic_ostream<Ch>& output, Tuple const& t, seq<I...>) noexcept {
        using swallow = int[];
        (void) swallow{0, (void(output << (I==0? "" : " ") << std::get<I>(t)), 0)...};
    }
    template <class Ch, class... Args>
    auto& operator<<(std::basic_ostream<Ch>& output, std::tuple<Args...> const& t) noexcept {
        output.put('(');
        print(output, t, gen_seq<sizeof... (Args)>());
        output.put(')');
        return output;
    }
} // ::aux

template <class T>
struct progressive {
    struct promise_type {
        T result;
        void unhandled_exception() { throw; }
        auto get_return_object() noexcept { return progressive{*this}; }
        auto initial_suspend() noexcept { return std::suspend_always{}; }
        auto final_suspend() noexcept { return std::suspend_always{}; }
        auto yield_value(T value) noexcept {
            this->result = value;
            return std::suspend_always{};
        }
        auto return_void() noexcept { }
    };
    T result() const noexcept {
        this->handle.resume();
        return this->handle.promise().result;
    }

private:
    explicit progressive(promise_type& p) noexcept
        : handle{std::coroutine_handle<promise_type>::from_promise(p)}
    {
    }
    std::coroutine_handle<promise_type> handle;
};

int main() {
    thread_local std::tuple<HWND, UINT, WPARAM, LPARAM> params;
    thread_local auto& [hwnd, message, wParam, lParam] = params;
    auto segment = []() noexcept -> progressive<bool> {
        for (;;) {
            int i = 0;
            while (message != WM_LBUTTONDOWN) co_yield false;
            SetTimer(hwnd, 1, 16, nullptr);
            SetCapture(hwnd);
            auto pt0 = std::complex<int>(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            co_yield true;
            while (message != WM_LBUTTONUP) {
                if (message == WM_TIMER) {
                    std::complex<int> pt1;
                    GetCursorPos(reinterpret_cast<POINT*>(&pt1));
                    ScreenToClient(hwnd, reinterpret_cast<POINT*>(&pt1));
                    HDC hdc = GetDC(hwnd);
                    for (int j = 0; j < std::abs(pt1-pt0)/32; ++j) {
                        double d = std::fmod(std::numbers::phi * (++i), 1.0);
                        auto pt = std::complex{
                            static_cast<int>((1-d) * pt0.real() + d * pt1.real()),
                            static_cast<int>((1-d) * pt0.imag() + d * pt1.imag()),
                        };
                        SetPixel(hdc, pt.real(), pt.imag(), 0xffffff);
                    }
                    ReleaseDC(hwnd, hdc);
                    co_yield true;
                    continue;
                }
                else {
                    co_yield false;
                    continue;
                }
            }
            auto pt1 = std::complex<int>(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            std::cout << pt0 << '-' << pt1 << std::endl;
            ReleaseCapture();
            KillTimer(hwnd, 1);
            co_yield true;
        }
    }();
    auto proc = [&segment]() noexcept -> progressive<LRESULT> {
        for (;;) {
            if (message == WM_DESTROY) {
                break;
            }
            else {
                if (segment.result()) {
                    co_yield 0;
                }
                else {
                    co_yield std::apply(DefWindowProc, params);
                }
            }
        }
        PostQuitMessage(0);
        co_yield 0;
    }();
    struct wndclass : WNDCLASS {
        ATOM atom;
        operator ATOM() const noexcept { return this->atom; }
        wndclass(auto... args) noexcept : WNDCLASS(args...), atom(RegisterClass(this))
        {
        }
        ~wndclass() noexcept {
            if (this->atom) {
                if (!UnregisterClass(this->lpszClassName, this->hInstance)) {
                    std::cerr << "UnregisterClass failed: " << GetLastError() << std::endl;
                }
            }
        }
    } wc = {
        CS_HREDRAW | CS_VREDRAW,
        [](auto... args) noexcept -> LRESULT {
            params = std::tuple{args...};
            if (auto proc =
                reinterpret_cast<progressive<LRESULT>*>(GetClassLongPtr(hwnd, 0)))
            {
                return proc->result();
            }
            return DefWindowProc(args...);
        },
        static_cast<DWORD>(sizeof (void*)),
        0,
        GetModuleHandle(nullptr),
        LoadIcon(nullptr, IDI_APPLICATION),
        LoadCursor(nullptr, IDC_CROSS),
        static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)),
        nullptr,
        TEXT("066a831d-8430-4ed5-8c13-6d4abc8b1c5a"),
    };
    if (!wc) {
        std::cerr << "RegisterClass failed: " << GetLastError() << std::endl;
        return 1;
    }
    hwnd = CreateWindowEx(WS_EX_CLIENTEDGE,
                          wc.lpszClassName,
                          wc.lpszClassName,
                          WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT,
                          CW_USEDEFAULT, CW_USEDEFAULT,
                          nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        std::cerr << "CreateWindowEx failed: " << GetLastError() << std::endl;
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetLastError(0);
    SetClassLongPtr(hwnd, 0, reinterpret_cast<LONG_PTR>(&proc));
    if (auto error = GetLastError()) {
        std::cerr << "SetWindowLongPtr failed: " << error << std::endl;
        return 1;
    }
    for (;;) {
        MSG msg;
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return static_cast<int>(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            WaitMessage();
        }
    }
}

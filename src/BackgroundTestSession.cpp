#include "BackgroundTestSession.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <QOpenGLWidget>
#include <QPaintEvent>
#include <QApplication>
#include <QTimer>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
bool isolated = false;
bool automated(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--background-test" || arg == "--background-timeout-ms" || arg == "--capture-ui" ||
            (arg.size() > 13 && arg.compare(arg.size() - 11, 11, "-regression") == 0))
            return true;
    }
    return false;
}
#ifdef _WIN32
struct Handle
{
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Desktop
{
    HDESK value = nullptr;
    ~Desktop() { if (value) CloseDesktop(value); }
};
std::wstring objectName(HANDLE object)
{
    DWORD bytes = 0;
    GetUserObjectInformationW(object, UOI_NAME, nullptr, 0, &bytes);
    std::vector<wchar_t> name(bytes / sizeof(wchar_t) + 1);
    if (!bytes || !GetUserObjectInformationW(object, UOI_NAME, name.data(), bytes, &bytes))
        return {};
    return name.data();
}
bool inheritStandardHandle(DWORD id, Handle &copy)
{
    HANDLE source = GetStdHandle(id);
    if (!source || source == INVALID_HANDLE_VALUE)
    {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        copy.value = CreateFileW(L"NUL", id == STD_INPUT_HANDLE ? GENERIC_READ : GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        return copy.value != INVALID_HANDLE_VALUE;
    }
    return DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &copy.value,
                           0, TRUE, DUPLICATE_SAME_ACCESS) != FALSE;
}
int failed(const char *operation)
{
    std::cerr << "Background test isolation failed: " << operation << " (Win32 " << GetLastError()
              << "). Refusing to run on the foreground desktop.\n";
    return 125;
}
struct WindowAudit { DWORD process; unsigned windows = 0; };
BOOL CALLBACK countWindows(HWND window, LPARAM data)
{
    auto &audit = *reinterpret_cast<WindowAudit *>(data);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == audit.process)
        ++audit.windows;
    return TRUE;
}
bool auditWindows(HDESK desktop, WindowAudit &audit)
{
    SetLastError(ERROR_SUCCESS);
    const BOOL result = EnumDesktopWindows(desktop, countWindows, reinterpret_cast<LPARAM>(&audit));
    // A not-yet-populated desktop can enumerate zero windows without an API error.
    return result || GetLastError() == ERROR_SUCCESS;
}
#endif
}

bool BackgroundTestSession::active() { return isolated; }

void BackgroundTestSession::startFramePump(QWidget *root)
{
    if (!isolated)
        return;
    auto timer = new QTimer(root);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(16);
    QObject::connect(timer, &QTimer::timeout, root, [root] {
        if (!root->isVisible())
            return;
        // An inactive desktop has no compositor expose events. Initialize/render only logically
        // visible GL widgets; cold-home tests must retain their lazy initialization behavior.
        for (auto gl : root->findChildren<QOpenGLWidget *>())
        {
            if (!gl->isVisible() || gl->size().isEmpty())
                continue;
            if (!gl->context())
                gl->grabFramebuffer();
            if (gl->context() && gl->isValid())
            {
                QPaintEvent event(gl->rect());
                QApplication::sendEvent(gl, &event);
            }
        }
    });
    timer->start();
}

int BackgroundTestSession::launchIfNeeded(int argc, char **argv)
{
    if (!automated(argc, argv))
        return -1;
    unsigned long timeoutMs = 10 * 60 * 1000;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--background-timeout-ms") == 0)
        {
            char *end = nullptr;
            timeoutMs = i + 1 < argc ? std::strtoul(argv[++i], &end, 10) : 0;
            if (!end || *end || timeoutMs < 1 || timeoutMs > 3600000)
            {
                std::cerr << "Invalid --background-timeout-ms (expected 1..3600000).\n";
                return 2;
            }
        }
#ifdef _WIN32
    // STARTUPINFO assigns the desktop to every GUI thread in the child, including Qt workers.
    // Changing only the calling thread's desktop would leave later threads on the input desktop.
    const std::wstring current = objectName(GetThreadDesktop(GetCurrentThreadId()));
    Desktop input;
    input.value = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_ENUMERATE);
    if (!input.value)
        return failed("OpenInputDesktop");
    const auto inputName = objectName(input.value);
    if (current.empty() || inputName.empty())
        return failed("query desktop names");
    if (current.find(L"learnQT-test-") == 0 && current != inputName)
    {
        isolated = true;
        std::cerr << "learnQT-background-v1: isolated desktop verified, pid=" << GetCurrentProcessId() << "\n";
        return -1;
    }

    Desktop desktop;
    const std::wstring name = L"learnQT-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64());
    // Deliberately omit DESKTOP_SWITCHDESKTOP. No code switches the user's input desktop.
    desktop.value = CreateDesktopW(name.c_str(), nullptr, nullptr, 0,
        DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_ENUMERATE |
        DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr);
    if (!desktop.value)
        return failed("CreateDesktop");
    Handle job;
    job.value = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                              &limits, sizeof(limits)))
        return failed("configure child cleanup");

    Handle in, out, err;
    if (!inheritStandardHandle(STD_INPUT_HANDLE, in) || !inheritStandardHandle(STD_OUTPUT_HANDLE, out) ||
        !inheritStandardHandle(STD_ERROR_HANDLE, err))
        return failed("redirect child streams");
    std::vector<wchar_t> executable(32768);
    if (!GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size())))
        return failed("get executable path");
    const auto station = objectName(GetProcessWindowStation());
    if (station.empty())
        return failed("query window station");
    std::wstring desktopPath = station + L"\\" + name;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.lpDesktop = &desktopPath[0];
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = in.value; startup.hStdOutput = out.value; startup.hStdError = err.value;
    std::wstring command = GetCommandLineW();
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable.data(), &command[0], nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | BELOW_NORMAL_PRIORITY_CLASS,
                        nullptr, nullptr, &startup, &child))
        return failed("launch isolated child");
    Handle process, thread;
    process.value = child.hProcess; thread.value = child.hThread;
    if (!AssignProcessToJobObject(job.value, process.value))
    {
        const DWORD error = GetLastError();
        TerminateProcess(process.value, 125);
        SetLastError(error);
        return failed("attach child cleanup");
    }
    if (ResumeThread(thread.value) == DWORD(-1))
        return failed("resume isolated child");
    // The job kills the entire test process tree on timeout or if CTest terminates this supervisor.
    const ULONGLONG started = GetTickCount64();
    unsigned checks = 0, maximumPrivateWindows = 0;
    DWORD result = WAIT_TIMEOUT;
    while ((result = WaitForSingleObject(process.value, 100)) == WAIT_TIMEOUT)
    {
        WindowAudit inputAudit{child.dwProcessId}, privateAudit{child.dwProcessId};
        if (!auditWindows(input.value, inputAudit) || !auditWindows(desktop.value, privateAudit))
            return failed("audit test windows");
        ++checks;
        maximumPrivateWindows = (std::max)(maximumPrivateWindows, privateAudit.windows);
        if (inputAudit.windows)
        {
            std::cerr << "Background test created a window on the input desktop; terminating.\n";
            return 125;
        }
        if (GetTickCount64() - started >= timeoutMs)
        {
            std::cerr << "Background test timed out; terminating its isolated process tree.\n";
            return 124;
        }
    }
    if (result != WAIT_OBJECT_0)
        return failed("wait for child");
    DWORD exitCode = 125;
    if (!GetExitCodeProcess(process.value, &exitCode))
        return failed("get child result");
    std::cerr << "Background audit: inputDesktopWindows=0 privateWindowsPeak=" << maximumPrivateWindows
              << " checks=" << checks << " exitCode=" << exitCode << "\n";
    // Windows exception statuses have the high bit set. Do not let their
    // signed conversion collide with launchIfNeeded's negative "run here"
    // result and cause the supervisor to start an app on the input desktop.
    return exitCode <= 0x7fffffffu ? int(exitCode) : 125;
#else
    std::cerr << "Background UI testing needs a verified isolated display on this platform.\n";
    return 125;
#endif
}

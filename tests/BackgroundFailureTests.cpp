#include "BackgroundTestSession.h"
#include <iostream>
#ifdef _WIN32
#include <intrin.h>
#endif

int main(int argc, char **argv)
{
    const int result = BackgroundTestSession::launchIfNeeded(argc, argv);
    if (result >= 0)
    {
        if (result != 125)
        {
            std::cerr << "Expected isolated crash to map to failure 125, got " << result << '\n';
            return 1;
        }
        std::cout << "Isolated crash returned to supervisor without foreground fallthrough.\n";
        return 0;
    }
    if (!BackgroundTestSession::active())
    {
        std::cerr << "Crash status fell through to execution on the input desktop.\n";
        return 2;
    }
#ifdef _WIN32
    // Reproduce the high-bit status observed in the NVIDIA driver failure.
    __fastfail(7);
#endif
    return 3;
}

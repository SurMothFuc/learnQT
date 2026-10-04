#pragma once
class QWidget;

// Called before QApplication: returns -1 to continue, or a nonnegative exit code.
// Windows exception exit statuses are logged verbatim and mapped to failure 125.
// Screenshot/UI-regression invocations default to a private, non-input Windows desktop.
namespace BackgroundTestSession
{
int launchIfNeeded(int argc, char **argv);
bool active();
void startFramePump(QWidget *root);
}

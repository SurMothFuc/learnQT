#pragma once
class QWidget;

// Called before QApplication: returns -1 to continue, or the isolated child's exit code.
// Screenshot/UI-regression invocations default to a private, non-input Windows desktop.
namespace BackgroundTestSession
{
int launchIfNeeded(int argc, char **argv);
bool active();
void startFramePump(QWidget *root);
}

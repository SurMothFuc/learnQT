#include "learnQT.h"
#include <QtWidgets/QApplication>
#include <QTextCodec>
int main(int argc, char *argv[])
{
    QSurfaceFormat fmt;
    fmt.setSwapInterval(0);                 // 0 = 关闭 VSync
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication a(argc, argv);
    learnQT w;
    w.show();
    return a.exec();
}

#include "learnQT.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    learnQT w;
    w.show();
    return a.exec();
}

#pragma once
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

// Small, dependency-free vector icons, rasterized at multiple device scales.
namespace WorkbenchStyle
{
inline QIcon icon(const QString &name)
{
    static QHash<QString, QIcon> cache;
    const auto cached = cache.constFind(name);
    if (cached != cache.constEnd())
        return cached.value();
    QIcon result;
    for (int scale : {1, 2, 3})
    {
        QPixmap pix(24 * scale, 24 * scale);
        pix.setDevicePixelRatio(scale);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#d6dbea"), 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        auto line = [&](qreal x, qreal y, qreal a, qreal b) {
            p.drawLine(QPointF(x, y), QPointF(a, b));
        };
        auto path = [&](std::initializer_list<QPointF> points) {
            QPainterPath shape;
            bool first = true;
            for (auto point : points)
            {
                if (first)
                    shape.moveTo(point);
                else
                    shape.lineTo(point);
                first = false;
            }
            p.drawPath(shape);
        };
        if (name == "home")
        {
            path({{3, 11}, {12, 3}, {21, 11}});
            path({{6, 10}, {6, 21}, {18, 21}, {18, 10}});
            path({{10, 21}, {10, 14}, {14, 14}, {14, 21}});
        }
        else if (name == "camera")
        {
            path({{3, 8}, {7, 8}, {9, 5}, {15, 5}, {17, 8}, {21, 8}, {21, 20}, {3, 20}, {3, 8}});
            p.drawEllipse(QPointF(12, 14), 4, 4);
        }
        else if (name == "scene" || name == "object")
        {
            path({{12, 3}, {21, 8}, {21, 17}, {12, 22}, {3, 17}, {3, 8}, {12, 3}});
            path({{3, 8}, {12, 13}, {21, 8}});
            line(12, 13, 12, 22);
        }
        else if (name == "open" || name == "assets")
        {
            path({{3, 19}, {3, 5}, {10, 5}, {12, 8}, {21, 8}, {21, 19}, {3, 19}});
            line(3, 11, 21, 11);
        }
        else if (name == "save")
        {
            path({{4, 3}, {18, 3}, {21, 6}, {21, 21}, {3, 21}, {3, 3}, {4, 3}});
            p.drawRect(QRectF(7, 3, 9, 6));
            p.drawRect(QRectF(7, 14, 10, 7));
        }
        else if (name == "new" || name == "import")
        {
            path({{14, 3}, {5, 3}, {5, 21}, {19, 21}, {19, 8}, {14, 3}, {14, 8}, {19, 8}});
            if (name == "import")
            {
                line(8, 14, 16, 14);
                path({{13, 11}, {16, 14}, {13, 17}});
            }
        }
        else if (name == "undo" || name == "redo")
        {
            if (name == "redo")
            {
                p.translate(24, 0);
                p.scale(-1, 1);
            }
            path({{8, 5}, {3, 10}, {8, 15}});
            QPainterPath curve;
            curve.moveTo(3, 10);
            curve.cubicTo(17, 7, 22, 12, 20, 19);
            p.drawPath(curve);
        }
        else if (name == "select")
        {
            path({{5, 3}, {19, 13}, {12, 14}, {9, 21}, {5, 3}});
        }
        else if (name == "move")
        {
            line(12, 3, 12, 21);
            line(3, 12, 21, 12);
            path({{9, 6}, {12, 3}, {15, 6}});
            path({{9, 18}, {12, 21}, {15, 18}});
            path({{6, 9}, {3, 12}, {6, 15}});
            path({{18, 9}, {21, 12}, {18, 15}});
        }
        else if (name == "rotate")
        {
            p.drawArc(QRectF(4, 4, 16, 16), 40 * 16, 300 * 16);
            path({{15, 3}, {19, 5}, {17, 9}});
        }
        else if (name == "scale" || name == "focus")
        {
            path({{9, 3}, {3, 3}, {3, 9}});
            path({{15, 3}, {21, 3}, {21, 9}});
            path({{3, 15}, {3, 21}, {9, 21}});
            path({{15, 21}, {21, 21}, {21, 15}});
            if (name == "scale")
            {
                line(8, 16, 17, 7);
                path({{11, 7}, {17, 7}, {17, 13}});
            }
        }
        else if (name == "play")
        {
            path({{7, 3}, {21, 12}, {7, 21}, {7, 3}});
        }
        else if (name == "pause")
        {
            line(8, 5, 8, 19);
            line(16, 5, 16, 19);
        }
        else if (name == "stop")
        {
            p.drawRoundedRect(QRectF(5, 5, 14, 14), 2, 2);
        }
        else if (name == "material")
        {
            p.drawEllipse(QRectF(3, 3, 18, 18));
            p.drawArc(QRectF(6, 6, 12, 12), 210 * 16, 190 * 16);
        }
        else if (name == "light")
        {
            p.drawEllipse(QRectF(7, 5, 10, 10));
            line(9, 18, 15, 18);
            line(10, 21, 14, 21);
            line(12, 1, 12, 2);
            line(2, 10, 4, 10);
            line(20, 10, 22, 10);
            line(4, 3, 6, 5);
            line(18, 5, 20, 3);
        }
        else if (name == "environment")
        {
            p.drawEllipse(QRectF(3, 3, 18, 18));
            p.drawEllipse(QRectF(8, 3, 8, 18));
            line(3, 12, 21, 12);
        }
        else if (name == "settings")
        {
            for (int i = 0; i < 3; ++i)
            {
                int y = 6 + i * 6, x = i == 1 ? 15 : 8;
                line(3, y, x - 2, y);
                line(x + 2, y, 21, y);
                p.drawEllipse(QPointF(x, y), 2, 2);
            }
        }
        else if (name == "chart")
        {
            path({{3, 3}, {3, 21}, {22, 21}});
            path({{6, 16}, {10, 10}, {14, 14}, {20, 5}});
        }
        else if (name == "search")
        {
            p.drawEllipse(QRectF(3, 3, 12, 12));
            line(14, 14, 21, 21);
        }
        else if (name == "snap")
        {
            path({{5, 4},
                  {5, 14},
                  {8, 18},
                  {16, 18},
                  {19, 14},
                  {19, 4},
                  {15, 4},
                  {15, 13},
                  {9, 13},
                  {9, 4},
                  {5, 4}});
        }
        else if (name == "close")
        {
            line(7, 7, 17, 17);
            line(17, 7, 7, 17);
        }
        else if (name == "dock")
        {
            p.drawRect(QRectF(4, 7, 13, 13));
            path({{8, 4}, {21, 4}, {21, 16}});
        }
        else
        {
            p.drawRoundedRect(QRectF(4, 4, 16, 16), 3, 3);
            line(8, 9, 16, 9);
            line(8, 14, 16, 14);
        }
        p.end();
        result.addPixmap(pix);
    }
    cache.insert(name, result);
    return result;
}
inline QString stylesheet()
{
    return QStringLiteral(R"QSS(
QMainWindow,QDialog { background:#10161e; color:#d6dbea; }
QWidget { color:#c9cfdd; }
QMainWindow::separator { background:#10161e; width:6px; height:6px; }
QMainWindow::separator:hover { background:#246bfa; }
QMenuBar { background:#161f2a; padding:4px 10px; border-bottom:1px solid #303544; }
QMenuBar::item { padding:5px 12px; background:transparent; }
QMenuBar::item:selected,QMenu::item:selected { background:#343950; border-radius:4px; }
QMenu { background:#242834; border:1px solid #3b4153; padding:6px; }
QMenu::item { padding:7px 28px 7px 12px; }
QToolBar { background:#192430; border:0; border-bottom:1px solid #303544; spacing:4px; padding:7px; }
QToolBar::separator { background:#383e4e; width:1px; margin:10px 8px; }
QToolButton { border:1px solid transparent; border-radius:7px; padding:7px 10px; }
QToolButton:hover { background:#303647; border-color:#434b62; }
QToolButton:checked { background:#17498d; border-color:#3986ff; color:white; }
QToolButton#renderPrimary { background:#246bfa; border:1px solid #4b8cff; padding:10px 18px; color:white; font-weight:600; }
QToolButton#renderPrimary:hover { background:#3d7fff; }
QToolButton#renderPrimary:disabled { background:#303446; border-color:#3a4050; color:#747c91; }
QToolBar#navigationRail { background:#161f2a; border-right:1px solid #303544; padding:8px 5px; spacing:7px; }
QToolBar#navigationRail QToolButton { padding:4px 6px; min-width:40px; font-size:12px; }
QWidget#dockTitle { background:#252a36; border:1px solid #343a49; border-top-left-radius:7px; border-top-right-radius:7px; }
QWidget#dockTitle QLabel { background:transparent; font-weight:600; }
QWidget#dockTitle QToolButton { padding:2px; border:0; }
QDockWidget { border:1px solid #343a49; }
QTreeView,QPlainTextEdit { background:#17202b; border:0; outline:0; }
QTreeView::item { height:28px; border:0; }
QTreeView::item:selected { background:#183d73; color:#f0f1ff; }
QTreeView::item:hover { background:#2b3041; }
QHeaderView::section { background:#252a36; color:#8f99af; border:0; padding:6px 4px; }
QScrollArea { background:#17202b; border:0; }
QScrollArea > QWidget > QWidget { background:#17202b; }
QTabWidget::pane { background:#17202b; border:1px solid #343a49; border-radius:6px; }
QTabBar::tab { background:#1b2734; color:#8f99af; padding:9px 12px; margin-right:3px; border-top-left-radius:6px; border-top-right-radius:6px; border-bottom:2px solid transparent; }
QTabBar::tab:selected { background:#2d3346; color:#eceeff; border-bottom:2px solid #2d78ff; }
QTabBar::tab:hover { color:#eceeff; background:#2a3040; }
QLineEdit,QAbstractSpinBox,QComboBox { background:#161a23; border:1px solid #363e50; border-radius:5px; padding:5px 7px; color:#e0e5f0; selection-background-color:#245aae; }
QLineEdit:focus,QAbstractSpinBox:focus,QComboBox:focus { border-color:#3986ff; }
QComboBox::drop-down { border:0; width:20px; }
QComboBox QAbstractItemView { background:#252a36; selection-background-color:#454b76; }
QPushButton { background:#303647; border:1px solid #454d62; border-radius:5px; padding:7px 10px; }
QPushButton:hover { background:#3c445c; border-color:#6b7394; }
QPushButton:pressed { background:#17498d; }
QWidget:disabled { color:#687185; }
QGroupBox { background:#1b2734; border:1px solid #323949; border-radius:6px; margin-top:18px; padding:12px 5px 5px; }
QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 5px; color:#adb8d1; font-weight:600; }
QCheckBox { spacing:7px; padding:3px 0; }
QScrollBar:vertical { background:#17202b; width:9px; margin:2px; }
QScrollBar::handle:vertical { background:#454d62; min-height:30px; border-radius:3px; }
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:transparent; }
QStatusBar { background:#191d26; border-top:1px solid #303544; padding:3px 8px; }
QStatusBar::item { border:0; }
QProgressBar { border:0; border-radius:3px; background:#2b3141; color:#c9cfdd; text-align:center; max-height:8px; }
QProgressBar::chunk { background:#2d78ff; border-radius:3px; }
QLabel#muted { color:#8b96ac; }
QWidget#viewportChrome { background:#1b2734; }
QWidget#viewportChrome QLabel { background:transparent; padding:3px 8px; }
QWidget#emptySurface { background:qradialgradient(cx:0.5, cy:0.45, radius:0.8, fx:0.5, fy:0.45, stop:0 #272d3e, stop:1 #131720); }
QFrame#emptyCard { background:#1b2734; border:1px solid #373f53; border-radius:14px; }
QFrame#emptyCard QLabel { background:transparent; border:0; }
QLabel#emptyTitle { font-size:22px; font-weight:600; color:#eef0ff; }
QListView { background:#141e29; border:1px solid #2c3b4d; border-radius:6px; outline:0; }
QListView::item { padding:5px; border:1px solid transparent; border-radius:5px; }
QListView::item:selected { background:#173f78; border-color:#2d78ff; color:white; }
QListView::item:hover { background:#233549; }
QTableWidget { background:#15202b; alternate-background-color:#1b2939; gridline-color:#2b3b50; border:1px solid #2b3b50; }
QLabel#pageHeading { font-size:22px; color:#eef4ff; font-weight:600; padding:6px; }
QLabel#sectionHeading { font-size:15px; color:#e6eeff; font-weight:600; padding:3px; }
QFrame#welcomeHero { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #122b4c,stop:0.55 #192c43,stop:1 #10202d); border:1px solid #345171; border-radius:10px; }
QLabel#heroHeading { font-size:29px; color:#f0f6ff; font-weight:600; background:transparent; }
QLabel#heroSubtitle { color:#aec5df; font-size:14px; background:transparent; }
QPushButton[homeCard="true"] { text-align:left; padding:14px; background:#1a2d44; border:1px solid #315170; border-radius:9px; font-size:13px; }
QPushButton[homeCard="true"]:hover { background:#203e66; border-color:#357cf5; }
QSplitter::handle { background:#10161e; width:5px; height:5px; }
QSplitter::handle:hover { background:#246bfa; }
)QSS");
}
} // namespace WorkbenchStyle

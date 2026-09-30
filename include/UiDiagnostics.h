#ifndef UI_DIAGNOSTICS_H
#define UI_DIAGNOSTICS_H

#include <QElapsedTimer>

struct UiSlotTimer;

// changed() 同步槽的接入点编号，用于把耗时归到具体面板。
enum UiSlot
{
    UiSlotWindow = 0,
    UiSlotEmptySurface,
    UiSlotSceneTree,
    UiSlotObjectInspector,
    UiSlotLightInspector,
    UiSlotWorkspacePages,
    UiSlotGlWidget,
    UiSlotEnvironment,
    UiSlotCount
};

// 每个 changed() 槽函数入口放一个，作用域结束时把耗时累加到对应接入点。
struct UiSlotTimer
{
    QElapsedTimer clock;
    int slot;
    explicit UiSlotTimer(int s) : slot(s)
    {
        clock.start();
    }
    ~UiSlotTimer();
};

// UI 线程的相机提交耗时分解。相机交互期间每个鼠标移动都会走一次
// EditorController::submit，这条路径上的任何重活都会直接表现为视口卡顿。
struct UiDiagnostics
{
    int submits = 0;
    int skipped = 0;
    // 文档比较（next.root == document.root）的累计毫秒数。
    double compareMs = 0;
    // 文档校验 validate() 的累计毫秒数。
    double validateMs = 0;
    // undo.push（含 EditorCommand 内部 before/after 拷贝）的累计毫秒数。
    double pushMs = 0;
    // emit changed() 中所有同步槽函数的累计毫秒数。
    double signalMs = 0;
    // 整次 submit（比较 + 校验 + undo.push 及其全部同步槽）的累计与单次最大值。
    double submitMs = 0, maxSubmitCallMs = 0;
    // undo.push 段（含其内部的 apply 与同步槽）的单次最大值。
    double maxSubmitMs = 0;
    // 按接入点累计/峰值的 changed() 槽函数耗时：定位到底是谁在相机交互里同步阻塞 UI。
    double slotMs[UiSlotCount] = {};
    double maxSlotMs[UiSlotCount] = {};
    // 场景树规模与其 data() 回调次数：data() 每次都要查一次节点索引。
    int treeItems = 0;
    quint64 treeDataCalls = 0;
    static const char *slotName(int i)
    {
        static const char *names[UiSlotCount] = {"window",   "emptySurface", "sceneTree",
                                                 "objectInspector", "lightInspector",
                                                 "workspacePages",  "glWidget",     "environment"};
        return i >= 0 && i < UiSlotCount ? names[i] : "?";
    }

    static UiDiagnostics &instance()
    {
        static UiDiagnostics d;
        return d;
    }

    static UiDiagnostics snapshot()
    {
        return instance();
    }

    void reset()
    {
        submits = 0;
        skipped = 0;
        compareMs = validateMs = pushMs = signalMs = 0;
        submitMs = maxSubmitCallMs = 0;
        maxSubmitMs = 0;
        treeItems = 0;
        treeDataCalls = 0;
        for (double &ms : slotMs)
            ms = 0;
        for (double &ms : maxSlotMs)
            ms = 0;
    }
};

#endif // UI_DIAGNOSTICS_H

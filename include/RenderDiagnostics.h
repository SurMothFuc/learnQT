#ifndef RENDER_DIAGNOSTICS_H
#define RENDER_DIAGNOSTICS_H

// 渲染循环诊断计数：把「一帧到底花在哪一段」暴露给渲染线程和回归剖析入口。
// 只保存最近一个统计窗口的累加值，不参与渲染正确性判断。
struct RenderLoopDiagnostics
{
    int frames = 0;
    // 完成 GPU 边界等待的次数（一个循环节拍）；与 frames 的差值说明有多少节拍没有产出新画面。
    int ticks = 0;
    // 等待上一批 GPU fence 的毫秒数（包含超时轮询的重试）。
    double boundaryWaitMs = 0;
    // 帧率整形 sleep 的耗时；锁定光栅化时它固定垫在每帧前面。
    double cadenceSleepMs = 0;
    // 循环尾部（提交 GPU 边界 + 发布统计）的耗时。
    double tailMs = 0;
    // 循环体其余部分的耗时：整轮减去边界等待，用于确认时间花在循环内还是循环外。
    double loopMs = 0;
    // 光栅化交互预览 pass 的 CPU 提交耗时；GPU 执行时间由 Renderer::stats.rasterMs 提供。
    double rasterSubmitMs = 0;
    // GPU 拾取 pass：CPU 提交耗时与窗口内累计 GPU 耗时分开统计。
    double pickSubmitMs = 0;
    double pickGpuMs = 0;
    double pickMaxMs = 0;
    // 拾取 pass 的实际重绘次数（版本或尺寸变化才算一次）。
    int pickPasses = 0;
    // 把可用帧复制进显示槽的 CPU 提交耗时。
    double presentMs = 0;
    // 从统计窗口开始到发布统计的整轮耗时，用于对账 frame 数与每帧总时间。
    double windowMs = 0;

    void reset()
    {
        frames = 0;
        ticks = 0;
        boundaryWaitMs = 0;
        cadenceSleepMs = 0;
        tailMs = 0;
        loopMs = 0;
        rasterSubmitMs = 0;
        pickSubmitMs = 0;
        pickGpuMs = 0;
        pickMaxMs = 0;
        pickPasses = 0;
        presentMs = 0;
        windowMs = 0;
    }
};

#endif // RENDER_DIAGNOSTICS_H

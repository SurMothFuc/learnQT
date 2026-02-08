#include "RenderParams.h"

RenderParams& RenderParams::instance() {
    static RenderParams inst;
    return inst;
}

RenderParams::RenderParams(QObject* parent)
    : QObject(parent) {
}

RenderParams::Stats RenderParams::stats() const {
    return {m_reads.load(), m_writes.load()};
}

#include "SceneAssets.h"
#include <assimp/IOStream.hpp>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <limits>

QString SceneAssets::resolve(const QString& request, bool record) {
    QString name=request; name.replace('\\','/'); name=QDir::cleanPath(name);
    const QDir base(QFileInfo(modelPath).absolutePath());
    const QString absolute=QDir::cleanPath(base.absoluteFilePath(name));
    const QString relative=base.relativeFilePath(absolute);
    QString actual;
    if (absolute==modelPath) actual=modelPath;
    else if (dependencies.contains(name)) actual=dependencies[name].toString();
    else if (dependencies.contains(relative)) actual=dependencies[relative].toString();
    else actual=absolute;
    if(!QFileInfo::exists(actual) && actual.contains('%')) actual=QUrl::fromPercentEncoding(actual.toUtf8());
    const QFileInfo info(actual);
    if (!info.isFile()) return {};
    actual=info.canonicalFilePath();
    if (!strictRoot.isEmpty()) {
        const QString rel=QDir(strictRoot).relativeFilePath(actual);
        if (QDir::isAbsolutePath(rel) || rel==".." || rel.startsWith("../")) return {};
    }
    if (record && absolute!=modelPath && !dependencies.contains(name) && !dependencies.contains(relative)) {
        dependencies[relative]=actual;
        // An authored absolute texture can reside next to the model. Preserve
        // its literal alias too; only the value is a physical fallback location.
        if (QDir::isAbsolutePath(name)) dependencies[name]=actual;
    }
    return actual;
}
namespace {
class Stream final : public Assimp::IOStream {
public:
    QFile file;
    explicit Stream(const QString& path):file(path) { file.open(QIODevice::ReadOnly); }
    size_t Read(void* buffer,size_t size,size_t count) override {
        if (!size || count>size_t(std::numeric_limits<qint64>::max())/size) return 0;
        const qint64 n=file.read(static_cast<char*>(buffer),qint64(size*count));
        return n>0 ? size_t(n)/size : 0;
    }
    size_t Write(const void*,size_t,size_t) override { return 0; }
    aiReturn Seek(size_t offset,aiOrigin origin) override {
        const qint64 base=origin==aiOrigin_SET ? 0 : (origin==aiOrigin_CUR ? file.pos() : file.size());
        return file.seek(base+static_cast<qint64>(offset)) ? aiReturn_SUCCESS : aiReturn_FAILURE;
    }
    size_t Tell() const override { return size_t(file.pos()); }
    size_t FileSize() const override { return size_t(file.size()); }
    void Flush() override {}
};
class IO final : public Assimp::IOSystem {
    SceneAssets& assets;
public:
    explicit IO(SceneAssets& a):assets(a) {}
    bool Exists(const char* name) const override { return !assets.resolve(QString::fromUtf8(name)).isEmpty(); }
    char getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char* name,const char* mode="rb") override {
        if (mode[0]!='r') return nullptr;
        const auto p=assets.resolve(QString::fromUtf8(name),true);
        if (p.isEmpty()) return nullptr;
        auto s=new Stream(p); if (!s->file.isOpen()) { delete s; return nullptr; } return s;
    }
    void Close(Assimp::IOStream* s) override { delete s; }
};
}
Assimp::IOSystem* SceneAssets::createIO() { return new IO(*this); }

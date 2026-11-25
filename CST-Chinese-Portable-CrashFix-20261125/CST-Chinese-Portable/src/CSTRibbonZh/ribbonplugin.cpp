#include "ribbonplugin.h"
#include "catalog.h"
#include "nativeui.h"
#include "surface.h"
#include "webui.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QAbstractItemDelegate>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <QtCore/QEvent>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QTranslator>
#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>
#include <algorithm>
#include <deque>

namespace {

// Keep the original ribbon terms for compatibility with the first release.
const QHash<QString, QString> pages = {
    {"file", "文件"}, {"home", "首页"}, {"modeling", "建模"}, {"modelling", "建模"},
    {"simulation", "仿真"}, {"post-processing", "后处理"}, {"view", "视图"},
    {"schematic", "原理图"}, {"home 3d", "三维首页"}, {"home schematic", "原理图首页"},
    {"mesh", "网格"}, {"macros", "宏"}, {"macro editor", "宏编辑器"},
    {"1d plot", "一维绘图"}, {"2d plot", "二维绘图"}, {"2d/3d plot", "二维/三维绘图"},
    {"farfield plot", "远场图"}, {"farfield cuts", "远场切面"}, {"cables", "线缆"},
    {"assembly modeling", "装配建模"}, {"edit pcb", "PCB 编辑"}, {"data", "数据"},
    {"electrical machine", "电机"}, {"text format", "文本格式"}
};

const QHash<QString, QString> groups = {
    {"settings", "设置"}, {"simulation", "仿真"}, {"materials", "材料"},
    {"shapes", "形状"}, {"picks", "拾取"}, {"tools", "工具"}, {"edit", "编辑"},
    {"curves", "曲线"}, {"wcs", "工作坐标系"}, {"transform", "变换"},
    {"sources and loads", "激励与负载"}, {"sources & loads", "激励与负载"},
    {"monitors", "监视器"}, {"mesh", "网格"}, {"mesh settings", "网格设置"},
    {"solver", "求解器"}, {"solvers", "求解器"}, {"selection", "选择"},
    {"mouse control", "鼠标操作"}, {"visibility", "可见性"}, {"windows", "窗口"},
    {"clipboard", "剪贴板"}, {"components", "组件"}, {"results", "结果"},
    {"field monitors", "场监视器"}, {"background", "背景"}, {"boundaries", "边界条件"}
};

QString translated(const QString &original, const QHash<QString, QString> &dictionary)
{
    QString key;
    QChar mnemonic;
    for (int i = 0; i < original.size(); ++i) {
        const QChar c = original.at(i);
        if (c == '&' && i + 1 < original.size()) {
            const QChar next = original.at(i + 1);
            if (next == '&') {
                key += '&';
                ++i;
                continue;
            }
            if (next.isLetterOrNumber()) {
                mnemonic = next;
                continue;
            }
        }
        key += c;
    }
    const auto found = dictionary.constFind(key.simplified().toCaseFolded());
    if (found == dictionary.cend())
        return {};
    return mnemonic.isNull() ? found.value() : found.value() + "(&" + mnemonic.toUpper() + ")";
}

const char *captionProperty(const QObject *object)
{
    if (object->inherits("Qtitan::RibbonGroup") || object->inherits("Qtitan::RibbonPage"))
        return "title";
    if (object->inherits("Qtitan::RibbonSystemButton"))
        return "text";
    return nullptr;
}

bool insideRibbon(const QObject *object)
{
    for (auto parent = object->parent(); parent; parent = parent->parent()) {
        if (parent->inherits("Qtitan::RibbonBar"))
            return true;
    }
    return false;
}

class InterfaceTranslator final : public QTranslator
{
public:
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *source, const char *, int) const override
    {
        return source ? uiText(QString::fromUtf8(source)) : QString();
    }
};


void diagnosticSnapshot()
{
    const auto path = qEnvironmentVariable("CST_ZH_DIAGNOSTICS");
    if (path.isEmpty()) return;
    QFile f(path + QStringLiteral(".") + QString::number(GetCurrentProcessId()) + QStringLiteral(".txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QTextStream out(&f); out.setCodec("UTF-8");
    for (auto w : QApplication::allWidgets()) {
        if (!w->isVisible() && !insideRibbon(w)) continue;
        out << "W " << w->metaObject()->className() << " name=" << w->objectName()
            << " title=" << w->windowTitle() << " parent=" << (w->parent() ? w->parent()->metaObject()->className() : "none") << "\n";
        if (auto v = qobject_cast<QAbstractItemView *>(w)) {
            out << "TREE delegate=" << (v->itemDelegate() ? v->itemDelegate()->metaObject()->className() : "none") << "\n";
            if(v->itemDelegate()) out << "PAINTED " << v->itemDelegate()->property("cstZhPaintedItems").toStringList().join(" | ") << "\n";
            for(auto a=w->parent(); a; a=a->parent())
                out << "ANCESTOR " << a->metaObject()->className() << " title=" << a->property("windowTitle").toString() << "\n";
            if(v->model()) for(int i=0;i<qMin(40,v->model()->rowCount(v->rootIndex()));++i){
                auto ix=v->model()->index(i,0,v->rootIndex());
                out << "ROOT " << ix.data().toString() << " flags=" << int(ix.flags()) << "\n";
            }
        }
        for(auto a:w->actions()) out << "ACTION " << a->text() << " dataType=" << a->data().typeName() << "\n";
    }
    struct Context { QTextStream *out; } ctx{&out};
    EnumWindows([](HWND top, LPARAM data)->BOOL {
        EnumChildWindows(top, [](HWND h, LPARAM data)->BOOL {
            wchar_t cls[256]={}; GetClassNameW(h,cls,256);
            if(GetWindowThreadProcessId(h,nullptr)==GetCurrentThreadId()) *reinterpret_cast<Context*>(data)->out << "NATIVE " << QString::fromWCharArray(cls) << "\n";
            return TRUE;
        },data); return TRUE;
    },reinterpret_cast<LPARAM>(&ctx));
}

class InterfaceLabels final : public QObject
{
public:
    InterfaceLabels()
    {
        // Qt constructs generic plugins before the host has finished constructing QApplication.
        QTimer::singleShot(0, this, [this]() {
            auto app = qobject_cast<QApplication *>(QCoreApplication::instance());
            if (!app)
                return;
            app->installTranslator(&translator);
            app->installEventFilter(this);
            for (auto widget : QApplication::allWidgets())
                track(widget);
            connect(&refresh, &QTimer::timeout, this, &InterfaceLabels::updateLabels);
            refresh.start(150);
            connect(&dialogs, &QTimer::timeout, this, &translateThreadDialogs);
            dialogs.start(950);
            if (!qEnvironmentVariable("CST_ZH_DIAGNOSTICS").isEmpty()) {
                auto diagnostics = new QTimer(this);
                connect(diagnostics, &QTimer::timeout, this, &diagnosticSnapshot);
                diagnostics->start(5000);
            }
        });
    }

    ~InterfaceLabels() override
    {
        refresh.stop();
        dialogs.stop();
        if (auto app = QCoreApplication::instance())
            app->removeEventFilter(this);
        // QObject disconnects connections safely in its own destructor. Host shutdown
        // can disconnect destroyed callbacks before deleting the tracked widgets, so
        // this de-duplication set must never be dereferenced at teardown (dump 25848).
        known.clear();
        targets.clear();
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        // Register only fully constructed controls. Revisiting live controls also covers text
        // changed without a Qt event (e.g. an existing QLabel whose setText is called again).
        if (event->type() == QEvent::Polish || event->type() == QEvent::Show ||
            event->type() == QEvent::ActionChanged)
            track(object);
        if (event->type() == QEvent::ChildAdded &&
            (object->inherits("Qtitan::RibbonBar") || insideRibbon(object))) {
            QPointer<QObject> child(static_cast<QChildEvent *>(event)->child());
            QTimer::singleShot(0, this, [this, child]() { if (child) track(child); });
        }
        return false;
    }

private:
    InterfaceTranslator translator;
    QTimer refresh, dialogs;
    QSet<QObject *> known;
    std::deque<QPointer<QObject>> targets;

    void track(QObject *object)
    {
        if ((!object->isWidgetType() && !qobject_cast<QAction *>(object)) || known.contains(object))
            return;
        known.insert(object);
        targets.emplace_back(object);
        connect(object, &QObject::destroyed, this, [this](QObject *obj) { known.remove(obj); });
    }

    void updateLabels()
    {
        // Bound each pass; large projects can own thousands of widgets.
        QElapsedTimer elapsed;
        elapsed.start();
        const size_t count = std::min<size_t>(targets.size(), 128);
        for (size_t i = 0; i < count && elapsed.elapsed() < 5; ++i) {
            const auto object = targets.front();
            targets.pop_front();
            if (!object) continue;
            targets.push_back(object);
            auto widget = qobject_cast<QWidget *>(object.data());
            const char *property = captionProperty(object);
            if (widget && !widget->isVisible() && !property) continue;
            if (property && insideRibbon(object)) {
                const auto source = object->property(property).toString();
                auto label = uiText(source);
                if (label.isNull())
                    label = translated(source, object->inherits("Qtitan::RibbonGroup") ? groups : pages);
                if (!label.isEmpty() && label != source)
                    object->setProperty(property, label);
            }
            if (object) localizeWizardWebView(object);
            if (object) localizeSurface(object, &uiText);
        }
    }
};

} // namespace

QObject *CstRibbonZhPlugin::create(const QString &name, const QString &)
{
    if (name.compare("cstribbonzh", Qt::CaseInsensitive) != 0)
        return nullptr;
    const QFileInfo executableFile(QCoreApplication::applicationFilePath());
    const auto executable = executableFile.fileName();
    const auto expectedBin = qEnvironmentVariable("CST_ZH_BIN");
    static const QStringList hosts = {
        "CST DESIGN ENVIRONMENT_AMD64.exe", "modeler_AMD64.exe", "schematic_AMD64.exe",
        "SchematicEditor_AMD64.exe", "AssemblyModeler_AMD64.exe", "MachineEditor_AMD64.exe",
        "3DViewer_AMD64.exe", "ProjectViewer_AMD64.exe", "Viewer1D_AMD64.exe",
        "Viewer2D_AMD64.exe", "Viewer3D_AMD64.exe", "WizardViewer_AMD64.exe", "ICfrontend_AMD64.exe"
    };
    const bool isCst = !expectedBin.isEmpty() && QDir::isAbsolutePath(expectedBin) &&
        hosts.contains(executable, Qt::CaseInsensitive) &&
        QDir(expectedBin).canonicalPath().compare(executableFile.canonicalPath(), Qt::CaseInsensitive) == 0;
    const bool isTest = executable.compare("CstRibbonZhSmoke.exe", Qt::CaseInsensitive) == 0
                     && QCoreApplication::arguments().contains("--verify-plugin");
    return isCst || isTest ? new InterfaceLabels : nullptr;
}

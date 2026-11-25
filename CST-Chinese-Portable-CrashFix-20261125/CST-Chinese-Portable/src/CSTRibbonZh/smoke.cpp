#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QLibrary>
#include <QtCore/QMetaProperty>
#include <QtCore/QPluginLoader>
#include <QtCore/QThread>
#include <QtCore/QVariant>
#include <QtGui/QGenericPlugin>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QStyledItemDelegate>
#include <QtGui/QPainter>
#include <functional>
#include <new>
#include <cstdio>
#include <windows.h>

// Stand-ins for the public Qt properties in the optional commercial ribbon SDK.
// All of these fixtures live in the offscreen test process.
namespace Qtitan {
class RibbonBar : public QMenuBar
{
    Q_OBJECT
public:
    using QMenuBar::QMenuBar;
};

class RibbonPage : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString title MEMBER title)
public:
    using QWidget::QWidget;
    QString title;
};

class RibbonGroup : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString title MEMBER title)
public:
    using QWidget::QWidget;
    QString title;
};

class RibbonSystemButton : public QToolButton
{
    Q_OBJECT
public:
    using QToolButton::QToolButton;
};
}

static bool waitUntil(const std::function<bool()> &condition)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < 4000) {
        QApplication::processEvents();
        QThread::msleep(10);
    }
    return condition();
}

static bool inspectVendorTypes()
{
    wchar_t path[32768];
    auto core = GetModuleHandleW(L"Qt5CoreCST_AMD64.dll");
    if (!core || !GetModuleFileNameW(core, path, sizeof(path) / sizeof(wchar_t))) {
        std::fprintf(stderr, "Expected CST's Qt runtime in the test process.\n");
        return false;
    }
    const auto folder = QFileInfo(QString::fromWCharArray(path)).absoluteDir();
    QLibrary library(folder.filePath(QStringLiteral("QtitanRibbon6.dll")));
    if (!library.load()) {
        std::fprintf(stderr, "Cannot verify Qtitan properties: %s\n", qPrintable(library.errorString()));
        return false;
    }
    for (const auto &type : {"RibbonBar", "RibbonPage", "RibbonGroup", "RibbonSystemButton"}) {
        QByteArray symbol = QByteArray("?staticMetaObject@") + type + "@Qtitan@@2UQMetaObject@@B";
        const auto meta = reinterpret_cast<const QMetaObject *>(library.resolve(symbol.constData()));
        const QByteArray className = QByteArray("Qtitan::") + type;
        if (!meta || QByteArray(meta->className()) != className) {
            std::fprintf(stderr, "Missing Qtitan class: %s\n", type);
            return false;
        }
        if (className == "Qtitan::RibbonBar")
            continue;
        const char *property = className == "Qtitan::RibbonSystemButton" ? "text" : "title";
        const int index = meta->indexOfProperty(property);
        if (index < 0 || !meta->property(index).isWritable()) {
            std::fprintf(stderr, "Missing writable property: %s.%s\n", type, property);
            return false;
        }
    }
    std::puts("QtitanRibbon6.dll: native metaobjects and writable properties verified.");
    return true;
}

static int exerciseRibbon()
{
    QWidget main;
    Qtitan::RibbonBar ribbon(&main);
    Qtitan::RibbonPage home(&ribbon), modeling(&ribbon), unknown(&ribbon);
    home.title = QStringLiteral("Home");
    modeling.title = QStringLiteral("Modeling");
    unknown.title = QStringLiteral("Original custom tab");
    Qtitan::RibbonGroup group(&home);
    group.title = QStringLiteral("Settings");
    Qtitan::RibbonSystemButton file(&ribbon);
    file.setText(QStringLiteral("&File"));
    Qtitan::RibbonPage outsidePage(&main);
    outsidePage.title = QStringLiteral("Simulation");
    QToolButton command(&group), outside(&main);
    QAction action(QStringLiteral("Home"), &group);
    outside.setText(QStringLiteral("Home"));
    command.setDefaultAction(&action);
    int triggers = 0;
    QObject::connect(&action, &QAction::triggered, [&]() { ++triggers; });
    main.show();  // QT_QPA_PLATFORM=offscreen

    if (!waitUntil([&]() {
        return home.title == QStringLiteral("首页") && modeling.title == QStringLiteral("建模") &&
               group.title == QStringLiteral("设置") && file.text() == QStringLiteral("文件(&F)");
    })) {
        std::fprintf(stderr, "Ribbon labels after plugin activation: %s, %s, %s, %s\n",
                     qPrintable(home.title), qPrintable(modeling.title),
                     qPrintable(group.title), qPrintable(file.text()));
        return 1;
    }
    command.click();
    if (triggers != 1 || unknown.title != QStringLiteral("Original custom tab") ||
        outside.text() != QStringLiteral("Home") || outsidePage.title != QStringLiteral("Simulation") ||
        action.text() != QStringLiteral("首页")) {
        std::fputs("An existing command, unknown tab or unrelated widget changed.\n", stderr);
        return 1;
    }
    Qtitan::RibbonPage added(&ribbon);
    added.title = QStringLiteral("Post-Processing");
    added.show();
    if (!waitUntil([&]() { return added.title == QStringLiteral("后处理"); })) {
        std::fputs("A tab created after initialization was not translated.\n", stderr);
        return 1;
    }
    // The host can reuse widgets when switching between 3D and schematic projects.
    home.title = QStringLiteral("S&imulation");
    group.title = QStringLiteral("Sources and Loads");
    if (!waitUntil([&]() {
        return home.title == QStringLiteral("仿真(&I)") && group.title == QStringLiteral("激励与负载");
    })) {
        std::fputs("Reused ribbon labels or keyboard mnemonics changed unexpectedly.\n", stderr);
        return 1;
    }
    std::puts("Offscreen ribbon fixture passed: exact translations, mnemonics, late/reused tab, command and scope.");
    return 0;
}

static bool exerciseInterface()
{
    const auto tr = [](const char *s) { return QCoreApplication::translate("Smoke", s); };
    if (tr("Fixture override") != QStringLiteral("测试") ||
        tr("Value %1").arg(23) != QStringLiteral("值 23") || tr("Reject %1") != "Reject %1" ||
        tr("Bad %ld") != "Bad %ld" || tr("Order %s %d") != "Order %s %d" ||
        tr("< Back") != QStringLiteral("上一步") || tr("Next >") != QStringLiteral("下一步") ||
        tr("S-Parameter\nCalculations") != QStringLiteral("S 参数计算") ||
        tr("mmWave\nCDF+SPD") != QStringLiteral("毫米波覆盖与功率密度") ||
        tr("Dimensions") != QStringLiteral("尺寸") || tr("Field Sources") != QStringLiteral("场源") ||
        tr("UnknownItem42") != "UnknownItem42" || tr("<b>Open</b>") != QStringLiteral("<b>打开</b>") ||
        tr("<p title=\"Open\">Open</p>") != QStringLiteral("<p title=\"Open\">打开</p>") ||
        tr("<style>Open</style>") != "<style>Open</style>")
        return false;

    QDialog dialog;
    QLabel label("Simulation", &dialog);
    QPushButton button("&Open", &dialog);
    QLineEdit input("Simulation", &dialog);
    input.setPlaceholderText("<Filter>");
    QTreeWidget tree(&dialog);
    auto row = new QTreeWidgetItem(&tree, {"Simulation"});
    QTabWidget tabs(&dialog), documents(&dialog);
    tabs.addTab(new QWidget, "Home");
    documents.setTabsClosable(true);
    documents.addTab(new QWidget, "Home");
    QMenu menu(&dialog);
    auto section = menu.addSection("Create Simulation Project");
    auto open = menu.addAction("&Open");
    open->setShortcut(QKeySequence("Ctrl+O"));
    auto userAction = menu.addAction("Open");
    userAction->setData("project-name");
    auto commandAction = menu.addAction("Open");
    commandAction->setData(42);
    int triggered = 0;
    QObject::connect(open, &QAction::triggered, [&]() { ++triggered; });
    dialog.show();
    menu.show();
    if (!waitUntil([&](){
        return label.text() == QStringLiteral("仿真") && button.text() == QStringLiteral("打开(&O)") &&
               tabs.tabText(0) == QStringLiteral("首页") && open->text() == QStringLiteral("打开(&O)");
    })) return false;
    open->trigger();
    if (section->text() != QStringLiteral("创建仿真工程") || triggered != 1 || open->shortcut() != QKeySequence("Ctrl+O") || input.text() != "Simulation" ||
        input.placeholderText() != QStringLiteral("筛选") || row->text(0) != "Simulation" || documents.tabText(0) != "Home" ||
        userAction->text() != "Open" || userAction->data() != "project-name" ||
        commandAction->text() != QStringLiteral("打开") || commandAction->data().toInt() != 42) return false;

    QMessageBox message(QMessageBox::Information, "Help", "Simulation\n27075@host");
    message.show();
    if (!waitUntil([&](){ return message.text() == QStringLiteral("仿真\n27075@host"); })) return false;
    for (int i = 0; i < 300; ++i) {
        auto ephemeral = new QLabel("Open", &dialog);
        ephemeral->show();
        delete ephemeral;
    }
    label.setText("Fixture override");
    return waitUntil([&](){ return label.text() == QStringLiteral("测试"); });
}

static bool exerciseScreenshotCommands()
{
    QWidget host;
    Qtitan::RibbonBar ribbon(&host);
    Qtitan::RibbonGroup group(&ribbon);
    QToolButton button(&group);
    QAction action("Voltage\nMonitor", &group);
    action.setData(QStringLiteral("CST.Command.VoltageMonitor"));
    action.setCheckable(true);
    action.setChecked(true);
    button.setDefaultAction(&action);
    QLabel normal("Normal:", &group), position("Position:", &group);
    QMenu outside;
    auto user = outside.addAction("Voltage Monitor");
    user->setData(QStringLiteral("user-model-name"));
    int triggered=0;
    QObject::connect(&action,&QAction::triggered,[&](){++triggered;});
    host.show(); outside.show();
    if (!waitUntil([&](){return action.text()==QStringLiteral("电压监视器") && normal.text()==QStringLiteral("法向：") && position.text()==QStringLiteral("位置：");})) {
        std::fprintf(stderr,"Ribbon actual: %s / %s / %s\n",qPrintable(action.text()),qPrintable(normal.text()),qPrintable(position.text())); return false;
    }
    if (!action.isChecked() || triggered || action.data().toString()!=QStringLiteral("CST.Command.VoltageMonitor") ||
        user->text()!=QStringLiteral("Voltage Monitor") || user->data().toString()!=QStringLiteral("user-model-name")) return false;
    action.trigger();
    return triggered==1 && !action.isChecked() &&
        QCoreApplication::translate("Smoke","Cutting\nPlane")==QStringLiteral("剖切平面") &&
        QCoreApplication::translate("Smoke","Intersection Check")==QStringLiteral("相交检查");
}

namespace RemoteUI {
class PropertyTreeView : public QTreeWidget { Q_OBJECT public: using QTreeWidget::QTreeWidget; };
class PropertyDelegate : public QStyledItemDelegate { Q_OBJECT public: using QStyledItemDelegate::QStyledItemDelegate; };
}

static bool exerciseNavigation(bool remote = false)
{
    QDockWidget dock("Navigation Tree");
    QTreeWidget *tree = remote ? new RemoteUI::PropertyTreeView(&dock) : new QTreeWidget(&dock);
    if(remote) tree->setItemDelegate(new RemoteUI::PropertyDelegate(tree));
    dock.setWidget(tree);
    tree->setColumnCount(1);
    auto category = new QTreeWidgetItem(tree, {"Components"});
    category->setFlags(remote ? (category->flags() | Qt::ItemIsEditable) : (category->flags() & ~Qt::ItemIsEditable));
    auto child = new QTreeWidgetItem(category, {"Components"});
    auto editable = new QTreeWidgetItem(tree, {remote ? "User model name" : "Components"});
    editable->setFlags(editable->flags() | Qt::ItemIsEditable);
    QTreeWidget reference;
    auto expected = new QTreeWidgetItem(&reference, {QStringLiteral("组件")});
    expected->setFlags(category->flags());
    int mutations = 0;
    QObject::connect(tree->model(), &QAbstractItemModel::dataChanged, [&](){ ++mutations; });
    dock.show();
    if (!waitUntil([&](){ return tree->itemDelegate()->property("cstZhNavigation").toBool(); })) return false;
    auto render = [](QAbstractItemDelegate *delegate, const QModelIndex &index) {
        QImage result(240, 30, QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::white);
        QPainter painter(&result);
        QStyleOptionViewItem option;
        option.rect = QRect(0, 0, 240, 30);
        option.state = QStyle::State_Enabled;
        delegate->paint(&painter, option, index);
        return result;
    };
    const auto rootIndex = tree->model()->index(0, 0);
    const auto childIndex = tree->model()->index(0, 0, rootIndex);
    const auto editableIndex = tree->model()->index(1, 0);
    QStyledItemDelegate original;
    return render(tree->itemDelegate(), rootIndex) == render(reference.itemDelegate(), reference.model()->index(0,0)) &&
           render(tree->itemDelegate(), childIndex) == render(&original, childIndex) &&
           render(tree->itemDelegate(), editableIndex) == render(&original, editableIndex) &&
           category->text(0) == "Components" && child->text(0) == "Components" && mutations == 0;
}

static bool exerciseNativeDialog()
{
    // A hidden Win32 dialog belonging strictly to this test process and thread.
    HWND dialog = CreateWindowExW(0, L"#32770", L"Simulation", WS_POPUP,
        0, 0, 200, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dialog) return false;
    auto control = [dialog](const wchar_t *name, const wchar_t *text, DWORD style) {
        return CreateWindowExW(0, name, text, WS_CHILD | style, 0, 0, 50, 18, dialog,
                               nullptr, GetModuleHandleW(nullptr), nullptr);
    };
    HWND label = control(L"Static", L"Simulation", SS_LEFT);
    HWND button = control(L"Button", L"Open", BS_AUTOCHECKBOX);
    HWND input = control(L"Edit", L"Open", ES_AUTOHSCROLL);
    SendMessageW(button, BM_SETCHECK, BST_CHECKED, 0);
    auto caption = [](HWND window) {
        wchar_t text[80] = {};
        GetWindowTextW(window, text, 80);
        return QString::fromWCharArray(text);
    };
    bool ok = label && button && input && waitUntil([&]() {
        return caption(dialog) == QStringLiteral("仿真") && caption(label) == QStringLiteral("仿真") &&
               caption(button) == QStringLiteral("打开");
    }) && caption(input) == "Open" && SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED;
    DestroyWindow(dialog);
    HWND outer = CreateWindowExW(0, L"Static", L"Simulation", WS_POPUP, 0,0,20,20,
                                nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND embedded = CreateWindowExW(0,L"#32770",L"Simulation",WS_CHILD,0,0,20,20,
                                   outer,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND inner = CreateWindowExW(0,L"Static",L"Open",WS_CHILD|SS_LEFT,0,0,20,20,
                                embedded,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND editor = CreateWindowExW(0,L"Edit",L"Copper",WS_CHILD,0,0,20,20,
                                 embedded,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND editorChild = CreateWindowExW(0,L"Static",L"Copper",WS_CHILD|SS_LEFT,0,0,20,20,
                                      editor,nullptr,GetModuleHandleW(nullptr),nullptr);
    ok = ok && outer && embedded && inner && waitUntil([&]() {
        return caption(embedded) == QStringLiteral("仿真") && caption(inner) == QStringLiteral("打开");
    }) && caption(outer) == "Simulation" && editor && editorChild &&
         caption(editor) == "Copper" && caption(editorChild) == "Copper";
    if (outer) DestroyWindow(outer);
    return ok;
}

static bool exerciseDisconnectedTeardown(const QString &plugin)
{
    QPluginLoader loader(plugin);
    auto factory = qobject_cast<QGenericPlugin *>(loader.instance());
    if (!factory) return false;
    for (int attempt=0; attempt<30; ++attempt) {
        auto controller = factory->create(QStringLiteral("cstribbonzh"), {});
        if (!controller) return false;
        void *memory=VirtualAlloc(nullptr,sizeof(QLabel),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if (!memory) {delete controller; return false;}
        auto widget = ::new(memory) QLabel("Fixture override");
        widget->show();
        // Let the controller register live widgets before simulating host shutdown.
        QCoreApplication::processEvents();
        QObject::disconnect(widget, nullptr, controller, nullptr);
        widget->~QLabel();
        // Release the backing page so stale-address reads cannot accidentally pass
        // just because the allocator has not reused the freed widget's memory yet.
        VirtualFree(memory,0,MEM_RELEASE);
        delete controller;
        QCoreApplication::processEvents();
    }
    return true;
}

int main(int argc, char **argv)
{
    if (qgetenv("QT_QPA_PLATFORM") != "offscreen") {
        std::fputs("The test requires QT_QPA_PLATFORM=offscreen.\n", stderr);
        return 1;
    }
    QApplication application(argc, argv);
    if (application.arguments().contains(QStringLiteral("--verify-host-guard"))) {
        QPluginLoader loader(application.arguments().last());
        auto factory = qobject_cast<QGenericPlugin *>(loader.instance());
        if (!factory || factory->create(QStringLiteral("cstribbonzh"), {}) != nullptr) {
            std::fputs("Could not verify that an unapproved host is ignored.\n", stderr);
            return 1;
        }
        std::puts("Host guard passed: the plugin declined activation without a matching host.");
        return 0;
    }
    if (!application.arguments().contains(QStringLiteral("--verify-plugin")) ||
        !QFileInfo::exists(application.arguments().last())) {
        std::fputs("Usage: CstRibbonZhSmoke.exe --verify-plugin <dll>\n", stderr);
        return 1;
    }
    if (!inspectVendorTypes() || exerciseRibbon() != 0) return 1;
    if (!exerciseInterface()) {
        std::fputs("Qt interface, catalog or data isolation regression.\n", stderr);
        return 1;
    }
    if (!exerciseScreenshotCommands()) {
        std::fputs("Screenshot ribbon command/context regression.\n", stderr); return 1;
    }
    std::puts("PASS screenshot ribbon commands, command IDs, checked state, context labels and user menu isolation.");
    if (!exerciseNavigation() || !exerciseNavigation(true)) {
        std::fputs("Navigation paint/data preservation regression.\n", stderr);
        return 1;
    }
    std::puts("PASS navigation translated painting, original model values, child and editable name protection.");
    if (!exerciseNativeDialog()) {
        std::fputs("Native dialog translation or control state regression.\n", stderr);
        return 1;
    }
    std::puts("PASS Qt dialogs, menus, tabs, translators, placeholders, overrides, lifetime, data isolation; native Win32 dialog and checkbox state.");
    if (!exerciseDisconnectedTeardown(application.arguments().last())) return 1;
    std::puts("PASS 30 teardown cycles after host disconnects destruction notifications.");
    return 0;
}

#include "smoke.moc"

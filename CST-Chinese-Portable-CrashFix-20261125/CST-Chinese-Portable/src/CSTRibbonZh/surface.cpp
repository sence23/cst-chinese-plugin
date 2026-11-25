#include "surface.h"

#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QLineEdit>
#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QAction>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QStatusBar>

namespace {
using Lookup = QString (*)(const QString &);

// Translate only presentation: never write back to CST's item model.
class NavigationDelegate final : public QStyledItemDelegate
{
public:
    NavigationDelegate(QAbstractItemView *view, Lookup lookup, bool remote, bool placeholder=false) : QStyledItemDelegate(view), lookup(lookup), original(view->itemDelegate()), remote(remote), placeholder(placeholder) {
        connect(original, &QAbstractItemDelegate::commitData, this, &QAbstractItemDelegate::commitData);
        connect(original, &QAbstractItemDelegate::closeEditor, this, &QAbstractItemDelegate::closeEditor);
        connect(original, &QAbstractItemDelegate::sizeHintChanged, this, &QAbstractItemDelegate::sizeHintChanged);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const override {
        QStyleOptionViewItem translated(o); initStyleOption(&translated,i);
        if (original && translated.text == i.data().toString()) original->paint(p,o,i);
        else {
            QStyledItemDelegate::paint(p,o,i);
            if (!qEnvironmentVariable("CST_ZH_DIAGNOSTICS").isEmpty()) {
                auto rows = property("cstZhPaintedItems").toStringList();
                const auto entry=i.data().toString()+QStringLiteral(" => ")+translated.text;
                if (!rows.contains(entry) && rows.size()<80) {
                    rows.append(entry); const_cast<NavigationDelegate *>(this)->setProperty("cstZhPaintedItems",rows);
                }
            }
        }
    }
    QSize sizeHint(const QStyleOptionViewItem &o,const QModelIndex &i) const override {
        auto translated=QStyledItemDelegate::sizeHint(o,i);
        return original ? translated.expandedTo(original->sizeHint(o,i)) : translated;
    }
    QWidget *createEditor(QWidget *p,const QStyleOptionViewItem &o,const QModelIndex &i) const override {return original ? original->createEditor(p,o,i) : nullptr;}
    void setEditorData(QWidget *e,const QModelIndex &i) const override {if(original) original->setEditorData(e,i);}
    void setModelData(QWidget *e,QAbstractItemModel *m,const QModelIndex &i) const override {if(original) original->setModelData(e,m,i);}
    void updateEditorGeometry(QWidget *e,const QStyleOptionViewItem &o,const QModelIndex &i) const override {if(original) original->updateEditorGeometry(e,o,i);}
    void destroyEditor(QWidget *e,const QModelIndex &i) const override {if(original)original->destroyEditor(e,i);}
    bool editorEvent(QEvent *e,QAbstractItemModel *m,const QStyleOptionViewItem &o,const QModelIndex &i) override {return original && original->editorEvent(e,m,o,i);}
    bool helpEvent(QHelpEvent *e,QAbstractItemView *v,const QStyleOptionViewItem &o,const QModelIndex &i) override {return original && original->helpEvent(e,v,o,i);}
protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if (placeholder) {
            if (index.flags()==Qt::NoItemFlags && option->text==QStringLiteral("Click on 'Open' to search for projects.")) {
                const auto target=lookup(option->text); if(!target.isEmpty()) option->text=target;
            }
            return;
        }
        static const QSet<QString> categories = {
            "Components", "Groups", "Materials", "Faces", "Curves", "WCS", "Anchor Points",
            "Wires", "Voxel Data", "Dimensions", "Design Space", "Lumped Elements", "Plane Wave",
            "Farfield Sources", "Field Sources", "Ports", "Excitation Signals", "Field Monitors",
            "Voltage and Current Monitors", "Probes", "Mesh", "1D Results", "2D/3D Results",
            "Farfields", "Tables"
        };
        if (index.column() != 0 || (!remote && (index.flags() & Qt::ItemIsEditable))) return;
        bool fixed = !index.parent().isValid() && categories.contains(option->text);
        if (remote && index.parent().isValid() && !index.parent().parent().isValid() &&
            index.parent().data().toString() == QStringLiteral("Mesh")) {
            fixed = option->text == QStringLiteral("Control") || option->text == QStringLiteral("Information") || option->text == QStringLiteral("Imported");
        }
        if (!fixed) return;
        const auto translated = lookup(option->text);
        if (!translated.isEmpty()) option->text = translated;
    }
private:
    Lookup lookup;
    QPointer<QAbstractItemDelegate> original;
    bool remote;
    bool placeholder;
};

void localizeNavigation(QTreeView *tree, Lookup lookup)
{
    bool navigation = false;
    for (auto parent = tree->parentWidget(); parent; parent = parent->parentWidget()) {
        const auto title = parent->windowTitle().remove('&').trimmed();
        if (title == QStringLiteral("Navigation Tree") || title == QStringLiteral("导航树")) {
            navigation = true; break;
        }
    }
    if (!navigation || !tree->itemDelegate() || tree->itemDelegate()->property("cstZhNavigation").toBool())
        return;
    // Preserve vendor-specific painting and editors instead of replacing custom delegates.
    const auto delegateClass = QString::fromLatin1(tree->itemDelegate()->metaObject()->className());
    const bool remote = tree->inherits("RemoteUI::PropertyTreeView") && delegateClass == QStringLiteral("RemoteUI::PropertyDelegate");
    if (!remote && delegateClass != QStringLiteral("QStyledItemDelegate")) return;
    auto delegate = new NavigationDelegate(tree, lookup, remote);
    delegate->setProperty("cstZhNavigation", true);
    tree->setItemDelegate(delegate);
    tree->viewport()->update();
}

bool inDataWidget(const QObject *object)
{
    // Delegates and editors can display arbitrary project data, even on a push button.
    for (auto p = object; p; p = p->parent()) {
        if (p->inherits("QAbstractItemView") || p->inherits("QLineEdit") ||
            p->inherits("QAbstractSpinBox") || p->inherits("QComboBox") ||
            p->inherits("QTextEdit") || p->inherits("QPlainTextEdit") ||
            p->inherits("QFileDialog"))
            return true;
    }
    return false;
}

bool inRibbon(const QObject *object)
{
    for (auto p = object; p; p = p->parent()) {
        if (p->inherits("Qtitan::RibbonBar"))
            return true;
    }
    return false;
}

bool dynamicMenu(const QMenu *menu)
{
    const QString title = menu->title().remove('&').trimmed().toCaseFolded();
    const QString name = menu->objectName().toCaseFolded();
    return title.contains("recent") || title.contains(QStringLiteral("最近")) ||
           name.contains("recent") || title == "window" || title == "windows" ||
           title == QStringLiteral("窗口") || name.contains("windowlist");
}

void translateProperty(const QPointer<QObject> &object, const char *property, Lookup lookup)
{
    if (!object)
        return;
    const QString source = object->property(property).toString();
    if (source.isEmpty())
        return;
    const QString target = lookup(source);
    if (!target.isEmpty() && target != source)
        object->setProperty(property, target);
}

void translateHints(const QPointer<QObject> &object, Lookup lookup)
{
    translateProperty(object, "toolTip", lookup);
    translateProperty(object, "statusTip", lookup);
    translateProperty(object, "whatsThis", lookup);
}

bool fixedRibbonCaption(const QString &text)
{
    static const QSet<QString> captions = {
        "view options", "copy view", "export image", "add to report", "hide", "show",
        "text and labels", "wire frame", "working plane", "dimension", "rectangle selection",
        "zoom", "pan", "rotate", "dynamic zoom", "rotate in plane", "reset view", "orthographic",
        "axes scaling", "select view", "cutting plane", "windows", "tile horizontally", "tile vertically", "tile three",
        "field source port", "field import", "voltage monitor", "current monitor", "pick lists", "mesh view",
        "global properties", "intersection check", "electrical connections", "logfile", "change view"
    };
    return captions.contains(QString(text).remove('&').simplified().toCaseFolded());
}

bool fixedRibbonAction(const QAction *action)
{
    if (!fixedRibbonCaption(action->text())) return false;
    for (auto widget : action->associatedWidgets())
        if (inRibbon(widget) && !inDataWidget(widget)) return true;
    return false;
}

bool staticAction(const QAction *action)
{
    if ((action->isSeparator() && action->text().isEmpty()) || (action->data().isValid() &&
        action->data().type() != QVariant::Int && action->data().type() != QVariant::UInt &&
        action->data().type() != QVariant::LongLong && action->data().type() != QVariant::ULongLong &&
        !(action->data().type() == QVariant::String && fixedRibbonAction(action))) ||
        action->inherits("QWidgetAction") || inDataWidget(action))
        return false;
    bool attached = false;
    for (const QWidget *widget : action->associatedWidgets()) {
        if (inDataWidget(widget))
            return false;
        if (auto menu = qobject_cast<const QMenu *>(widget)) {
            if (dynamicMenu(menu))
                return false;
            attached = true;
        } else if (qobject_cast<const QMenuBar *>(widget) ||
                   qobject_cast<const QToolBar *>(widget) || widget->inherits("Qtitan::RibbonBackstageView") || inRibbon(widget)) {
            attached = true;
        }
    }
    return attached;
}

void translateAction(QAction *action, Lookup lookup)
{
    if (!action || !staticAction(action))
        return;
    const QPointer<QObject> guard(action);
    // Qt propagates ActionChanged to associated widgets. Triggered/toggled are not invoked.
    translateProperty(guard, "text", lookup);
    translateProperty(guard, "iconText", lookup);
    translateHints(guard, lookup);
}

void translateActions(const QWidget *container, Lookup lookup)
{
    // Host slots invoked by ActionChanged can remove another action during this pass.
    QList<QPointer<QAction>> actions;
    for (auto action : container->actions())
        actions.append(action);
    for (const auto &action : actions)
        translateAction(action.data(), lookup);
}

void translateTabs(QTabWidget *tabs, Lookup lookup)
{
    // Project/document tabs contain user-supplied names. Only fixed dialog pages qualify.
    if (!qobject_cast<QDialog *>(tabs->window()) || tabs->tabsClosable() ||
        tabs->documentMode() || tabs->isMovable())
        return;
    const QPointer<QTabWidget> guard(tabs);
    for (int index = 0; guard && index < guard->count(); ++index) {
        if (guard->tabBar()->tabData(index).isValid() || !guard->widget(index) ||
            !guard->widget(index)->windowFilePath().isEmpty())
            continue;
        const auto source = guard->tabText(index);
        const auto target = lookup(source);
        if (!target.isEmpty() && target != source)
            guard->setTabText(index, target);
    }
}
} // namespace

void localizeSurface(QObject *object, QString (*lookup)(const QString &))
{
    if (!object || !lookup)
        return;
    if (auto action = qobject_cast<QAction *>(object)) {
        translateAction(action, lookup);
        return;
    }
    auto widget = qobject_cast<QWidget *>(object);
    if (auto list = qobject_cast<QAbstractItemView *>(widget)) {
        if (list->inherits("ModuleListWidget") && list->objectName()==QStringLiteral("listRecentFiles") && list->itemDelegate() &&
            !list->itemDelegate()->property("cstZhNavigation").toBool() &&
            QString::fromLatin1(list->itemDelegate()->metaObject()->className())==QStringLiteral("QStyledItemDelegate")) {
            auto delegate=new NavigationDelegate(list,lookup,false,true);
            delegate->setProperty("cstZhNavigation",true);list->setItemDelegate(delegate);list->viewport()->update();
        }
    }
    if (auto tree = qobject_cast<QTreeView *>(widget)) {
        localizeNavigation(tree, lookup);
        return;
    }
    if (auto editor = qobject_cast<QLineEdit *>(widget)) {
        if (!inDataWidget(editor->parent()))
            translateProperty(QPointer<QObject>(editor), "placeholderText", lookup);
        return;
    }
    if (!widget || inDataWidget(widget))
        return;
    const QPointer<QObject> guard(widget);

    if (auto menu = qobject_cast<QMenu *>(widget)) {
        translateProperty(guard, "title", lookup);
        if (guard && !dynamicMenu(menu))
            translateActions(menu, lookup);
    } else if (qobject_cast<QMenuBar *>(widget) || qobject_cast<QToolBar *>(widget) || widget->inherits("Qtitan::RibbonBackstageView")) {
        translateActions(widget, lookup);
        if (guard && qobject_cast<QToolBar *>(widget))
            translateProperty(guard, "windowTitle", lookup);
    } else if (qobject_cast<QDialog *>(widget) || qobject_cast<QDockWidget *>(widget)) {
        if (widget->windowFilePath().isEmpty())
            translateProperty(guard, "windowTitle", lookup);
        if (guard && qobject_cast<QMessageBox *>(widget)) {
            translateProperty(guard, "text", lookup);
            translateProperty(guard, "informativeText", lookup);
        }
    } else if (auto tabs = qobject_cast<QTabWidget *>(widget)) {
        translateTabs(tabs, lookup);
    } else if (qobject_cast<QGroupBox *>(widget)) {
        translateProperty(guard, "title", lookup);
    } else if (auto label = qobject_cast<QLabel *>(widget)) {
        // Main-window labels may also contain user data. Restrict new coverage to panel headings.
        const auto text = label->text().trimmed();
        const bool backstageLabel = label->objectName() == QStringLiteral("labelRecentConfigurations") ||
            label->objectName() == QStringLiteral("labelConfigurationInfo") || label->objectName() == QStringLiteral("labelModules");
        bool inBackstage=false;
        for(auto ancestor=label->parent();ancestor;ancestor=ancestor->parent())
            if(ancestor->inherits("BackstagePageNew")) {inBackstage=true;break;}
        if (inRibbon(label) && (text == QStringLiteral("Normal:") || text == QStringLiteral("Position:"))) {
            label->setText(text == QStringLiteral("Normal:") ? QStringLiteral("法向：") : QStringLiteral("位置："));
        } else if (label->buddy() || qobject_cast<QDialog *>(widget->window()) ||
            qobject_cast<QStatusBar *>(widget->parentWidget()) || (inBackstage && backstageLabel) ||
            text == QStringLiteral("Result Navigator") || text == QStringLiteral("Navigation Tree") ||
            text == QStringLiteral("Parameter List"))
            translateProperty(guard, "text", lookup);
    } else if (qobject_cast<QAbstractButton *>(widget)) {
        auto tool = qobject_cast<QToolButton *>(widget);
        if (tool && tool->defaultAction()) {
            if (staticAction(tool->defaultAction()))
                translateAction(tool->defaultAction(), lookup);
            // Some Qtitan buttons cache iconText separately from QAction::text.
            if (guard && inRibbon(tool) && fixedRibbonCaption(tool->text()))
                translateProperty(guard, "text", lookup);
        } else if (!tool || qobject_cast<QDialog *>(widget->window()) ||
                   qobject_cast<QToolBar *>(widget->parentWidget()) || inRibbon(widget)) {
            translateProperty(guard, "text", lookup);
        }
    }
    translateHints(guard, lookup);
}

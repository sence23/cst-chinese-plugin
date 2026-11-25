#include "webui.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtCore/QVariant>
#include <QtWidgets/QWidget>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
QString resource(const char *name)
{
    QFile file(QString::fromLatin1(name));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QString quote(const QString &value)
{
    const auto json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}
}

void localizeWizardWebView(QObject *object)
{
    if (!object || !object->inherits("QWebEngineView")) return;
    // Only the installed local configuration wizard qualifies, never arbitrary web content.
    const auto bin = qEnvironmentVariable("CST_ZH_BIN");
    if (bin.isEmpty()) return;
    const QString expected = QFileInfo(QDir(bin).filePath("../Library/ConfigurationWizard/index.html")).canonicalFilePath();
    const auto url = object->property("url").toUrl();
    if (expected.isEmpty() || !url.isLocalFile() ||
        QFileInfo(url.toLocalFile()).canonicalFilePath().compare(expected, Qt::CaseInsensitive) != 0) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - object->property("cstZhWebLastAttempt").toLongLong() < 1500) return;

    // Optional ABI bridge for CST 2025's renamed Qt 5.15 MSVC x64 DLL. These public
    // method exports were verified with dumpbin. No foreign objects are constructed,
    // no layout offsets are used, and missing exports simply disable this adapter.
    const HMODULE module = GetModuleHandleW(L"Qt5WebEngineWidgetsCST_AMD64.dll");
    if (!module || !QString::fromLatin1(qVersion()).startsWith("5.15.")) return;
    using Page = QObject *(*)(const void *);
    using Run = void (*)(void *, const QString &, quint32);
    auto pageOf = reinterpret_cast<Page>(GetProcAddress(module, "?page@QWebEngineView@@QEBAPEAVQWebEnginePage@@XZ"));
    auto run = reinterpret_cast<Run>(GetProcAddress(module, "?runJavaScript@QWebEnginePage@@QEAAXAEBVQString@@I@Z"));
    if (!pageOf || !run) return;
    auto view = qobject_cast<QWidget *>(object);
    QObject *page = view ? pageOf(view) : nullptr;
    if (!page || !page->inherits("QWebEnginePage")) return;
    static const QString js = resource(":/cst-zh-web/wizard.js");
    static const QByteArray dictionary = QJsonDocument::fromJson(resource(":/cst-zh-web/wizard-zh.json").toUtf8()).toJson(QJsonDocument::Compact);
    if (js.isEmpty() || dictionary.isEmpty()) return;
    const QString script = QStringLiteral("(%1)(%2,%3);").arg(js, QString::fromUtf8(dictionary), quote(url.toString(QUrl::FullyEncoded)));
    object->setProperty("cstZhWebLastAttempt", now);
    // ApplicationWorld has its own JS globals; the shared DOM is intentionally used
    // for display only. The script checks the document URL again against navigation races.
    run(page, script, 1);
    object->setProperty("cstZhWebAdapter", QStringLiteral("configuration-wizard-v1"));
}

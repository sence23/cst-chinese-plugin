#include "catalog.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QRegularExpression>
#include <QtGui/QTextDocumentFragment>

namespace {
QStringList placeholders(const QString &s)
{
    // Keep printf/FormatMessage modifiers and the order of unnumbered arguments intact.
    // The final alternative conservatively preserves unrecognized percent expressions.
    static const QRegularExpression pattern(QStringLiteral(
        "%(?:[1-9][0-9]*![^!\\r\\n]+!|(?:[1-9][0-9]*\\$)?[-+ #0']*"
        "(?:\\*|[0-9]+)?(?:\\.(?:\\*|[0-9]+))?(?:hh|ll|I32|I64|[hljztL])?"
        "[diuoxXfFeEgGaAcspn]|L?[1-9][0-9]*|L?n|%|[^\\s]*)"));
    QStringList result;
    auto it = pattern.globalMatch(s);
    while (it.hasNext())
        result.append(it.next().captured());
    return result;
}

void readFile(QHash<QString, QString> &words, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
        return;
    while (!file.atEnd()) {
        auto line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QChar(0xfeff)))
            line.remove(0, 1);
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const auto fields = line.split('\t');
        if (fields.size() != 2)
            continue;
        const auto source = fields[0].simplified();
        const auto target = fields[1].trimmed();
        if (source.isEmpty() || target.isEmpty() || source.size() > 4096 || target.size() > 8192 ||
            target.contains('<') || target.contains('>') || target.contains('&') ||
            source.contains(QChar::ReplacementCharacter) || target.contains(QChar::ReplacementCharacter) ||
            placeholders(source) != placeholders(target))
            continue;
        words.insert(source.toCaseFolded(), target);
    }
}

QHash<QString, QString> loadWords()
{
    QHash<QString, QString> words;
    const QDir builtIn(QStringLiteral(":/cst-zh"));
    for (const auto &file : builtIn.entryList({"*.tsv"}, QDir::Files, QDir::Name))
        readFile(words, builtIn.filePath(file));
    const auto user = qEnvironmentVariable("CST_ZH_USER_CATALOG");
    if (!user.isEmpty() && QDir::isAbsolutePath(user))
        readFile(words, user);
    return words;
}

QString singleLine(const QString &original)
{
    static const auto words = loadWords();
    int start = 0, end = original.size();
    while (start < end && original[start].isSpace()) ++start;
    while (end > start && original[end - 1].isSpace()) --end;
    if (end == start)
        return {};
    const QString source = original.mid(start, end - start);
    QString key, suffix;
    QChar mnemonic;
    for (int i = 0; i < source.size(); ++i) {
        if (source[i] == '&' && i + 1 < source.size()) {
            if (source[i+1] == '&') {
                key += '&';
                ++i;
                continue;
            }
            if (source[i+1].isLetterOrNumber()) {
                mnemonic = source[i+1];
                continue;
            }
        }
        key += source[i];
    }
    key = key.simplified().toCaseFolded();
    auto found = words.constFind(key);
    if (found == words.constEnd()) {
        if (key.endsWith("...")) { key.chop(3); suffix = "…"; }
        else if (key.endsWith(QChar(0x2026))) { key.chop(1); suffix = "…"; }
        else if (key.endsWith(':')) { key.chop(1); suffix = "："; }
        else return {};
        found = words.constFind(key.trimmed());
        if (found == words.constEnd())
            return {};
    }
    QString value = found.value();
    if (!mnemonic.isNull())
        value += QStringLiteral("(&") + mnemonic.toUpper() + ')';
    return original.left(start) + value + suffix + original.mid(end);
}
}

QString uiText(const QString &source)
{
    const auto trimmed = source.trimmed();
    const bool wizardButton = trimmed.compare("< Back", Qt::CaseInsensitive) == 0 ||
                              trimmed.compare("Next >", Qt::CaseInsensitive) == 0;
    if (source.isEmpty() || source.size() > 8192 ||
        (!wizardButton && trimmed != QStringLiteral("<Filter>") &&
         !trimmed.startsWith(QStringLiteral("<html"), Qt::CaseInsensitive) &&
         !trimmed.startsWith(QStringLiteral("<qt>"), Qt::CaseInsensitive) &&
         !trimmed.startsWith(QStringLiteral("<p"), Qt::CaseInsensitive) &&
         !trimmed.startsWith(QStringLiteral("<b>"), Qt::CaseInsensitive) &&
         !trimmed.startsWith(QStringLiteral("<span"), Qt::CaseInsensitive) &&
         (source.contains('<') || source.contains('>'))))
        return {};
    if (!wizardButton && trimmed != QStringLiteral("<Filter>") && source.contains('<')) {
        // Preserve markup and attributes byte-for-byte; translate text nodes only.
        static const QRegularExpression pieces(QStringLiteral("<[^>]*>|[^<]+"));
        auto matches=pieces.globalMatch(source);
        QString result; bool changed=false; bool raw=false;
        while(matches.hasNext()) {
            const auto token=matches.next().captured();
            if(token.startsWith('<')) {
                const auto lower=token.toLower();
                if(lower.startsWith("<style") || lower.startsWith("<script")) raw=true;
                if(lower.startsWith("</style") || lower.startsWith("</script")) raw=false;
                result+=token; continue;
            }
            const auto plain=QTextDocumentFragment::fromHtml(token).toPlainText();
            const auto target=raw ? QString() : singleLine(plain);
            if(!target.isNull() && target!=plain) {result+=target.toHtmlEscaped();changed=true;}
            else result+=token;
        }
        return changed ? result : QString();
    }
    // A Windows menu caption can append a shortcut with a TAB. Keep it verbatim.
    int tab = source.indexOf('\t');
    if (tab >= 0 && !source.contains('\n')) {
        const auto translated = singleLine(source.left(tab));
        return translated.isNull() ? QString() : translated + source.mid(tab);
    }
    if (!source.contains('\n'))
        return singleLine(source);
    const auto whole = singleLine(source.simplified());
    if (!whole.isNull()) return whole;
    auto lines = source.split('\n');
    bool changed = false;
    for (auto &line : lines) {
        const auto value = singleLine(line);
        if (!value.isNull() && line != value) { line = value; changed = true; }
    }
    return changed ? lines.join('\n') : QString();
}

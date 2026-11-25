#pragma once

#include <QtCore/QString>

class QObject;

// The caller supplies an exact UI-text lookup. A null/empty result leaves the source intact.
void localizeSurface(QObject *object, QString (*lookup)(const QString &));

#pragma once

#include <QtGui/QGenericPlugin>

class CstRibbonZhPlugin final : public QGenericPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QGenericPluginFactoryInterface_iid FILE "ribbonplugin.json")
public:
    QObject *create(const QString &name, const QString &spec) override;
};

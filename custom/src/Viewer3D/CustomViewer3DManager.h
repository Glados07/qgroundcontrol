/****************************************************************************
 *
 * Custom Viewer3D QML type registrar and object owner.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>

class OsmParser;
class Viewer3DQmlBackend;

class CustomViewer3DManager : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("src/Viewer3D/OsmParser.h")
    Q_MOC_INCLUDE("custom/src/Viewer3D/Viewer3DQmlBackend.h")

    Q_PROPERTY(OsmParser *osmParser MEMBER _osmParser CONSTANT)
    Q_PROPERTY(Viewer3DQmlBackend *qmlBackend MEMBER _qmlBackend CONSTANT)

public:
    explicit CustomViewer3DManager();
    ~CustomViewer3DManager() override;

    static void registerQmlTypes();

private:
    OsmParser *_osmParser = nullptr;
    Viewer3DQmlBackend *_qmlBackend = nullptr;
};

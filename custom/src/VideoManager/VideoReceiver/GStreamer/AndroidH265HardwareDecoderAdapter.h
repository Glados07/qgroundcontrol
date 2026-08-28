/****************************************************************************
 *
 * Android H.265 MediaCodec adapter for QGC's hvc1 decoder input.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

class AndroidH265HardwareDecoderAdapter
{
public:
    /// Registers a high-rank decoder bin which converts hvc1 to Annex-B before
    /// handing the stream to the preferred vendor Android MediaCodec decoder.
    /// Additional pad-compatible MediaCodec candidates are registered as
    /// rank-NONE adapter factories for bounded, receiver-specific recovery.
    ///
    /// Returns false without changing the registry when no compatible vendor
    /// decoder can be constructed. This must run after GStreamer is initialized
    /// and before decodebin3 is created.
    static bool registerElement();

    static const char *elementFactoryName();
    static const char *selectedHardwareDecoderFactoryName();

    /// Registered adapter factories in deterministic MediaCodec preference
    /// order. The first entry is elementFactoryName(); later entries preserve
    /// the same A8 Mini normalization topology but bind another decoder.
    static QStringList registeredElementFactoryNames();

    /// Rank-NONE adapter factories which may be selected explicitly after the
    /// preferred A8-compatible route fails for one receiver/URI.
    static QStringList alternativeElementFactoryNames();

    static bool isAdapterElementFactoryName(const QString &factoryName);
    static QString hardwareDecoderFactoryNameForElementFactory(
        const QString &factoryName);
    static bool adapterRouteContainsFactory(const QString &adapterFactoryName,
                                            const QString &reportedFactoryName);

    /// Shared factory-name filter used by both adapter selection and rank
    /// policy so known Android software MediaCodec wrappers are never logged
    /// or prioritized as vendor hardware.
    static bool isVendorHardwareDecoderFactoryName(const char *factoryName);
};

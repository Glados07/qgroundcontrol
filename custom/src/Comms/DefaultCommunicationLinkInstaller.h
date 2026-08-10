/****************************************************************************
 *
 * Custom default communication-link installer.
 * Reconciles the single default UDP configuration before LinkManager loads it.
 *
 ****************************************************************************/

#pragma once

class DefaultCommunicationLinkInstaller
{
public:
    static void ensureInstalled();
};

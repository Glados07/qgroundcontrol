/****************************************************************************
 *
 * Custom default communication-link installer.
 * Adds required UDP configurations before QGC's LinkManager loads them.
 *
 ****************************************************************************/

#pragma once

class DefaultCommunicationLinkInstaller
{
public:
    static void ensureInstalled();
};

/****************************************************************************
 *
 * Custom default communication-link installer.
 * Installs the project default UDP configuration only when the saved
 * communication-link list is empty. Existing configurations are untouched.
 *
 ****************************************************************************/

#pragma once

class DefaultCommunicationLinkInstaller
{
public:
    static void ensureInstalled();
};

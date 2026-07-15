/****************************************************************************
 *
 * custom 默认通信链路安装器。
 * 在 QGC 原生 LinkManager 加载配置前补充项目需要的默认 UDP 链路。
 *
 ****************************************************************************/

#pragma once

class DefaultCommunicationLinkInstaller
{
public:
    static void ensureInstalled();
};

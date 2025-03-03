/****************************************************************************
**
** Copyright (C) 2016 Tim Sander <tim@krieglstein.org>
** Copyright (C) 2016 Denis Shienkov <denis.shienkov@gmail.com>
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "baremetalconstants.h"
#include "baremetaldevice.h"
#include "baremetaldeviceconfigurationwidget.h"
#include "baremetaldeviceconfigurationwizard.h"
#include "debugserverprovidermanager.h"

// GDB server providers.
#include "debugservers/gdb/defaultgdbserverprovider.h"
#include "debugservers/gdb/gdbserverproviderprocess.h"

#include <coreplugin/id.h>

#include <ssh/sshconnection.h>
#include <utils/qtcassert.h>

#include <QCoreApplication>

using namespace ProjectExplorer;

namespace BareMetal {
namespace Internal {

const char debugServerProviderIdKeyC[] = "IDebugServerProviderId";

// BareMetalDevice

BareMetalDevice::BareMetalDevice()
{
    setDisplayType(QCoreApplication::translate("BareMetal::Internal::BareMetalDevice",
                                               "Bare Metal"));
    setDefaultDisplayName(defaultDisplayName());
    setOsType(Utils::OsTypeOther);
}

BareMetalDevice::~BareMetalDevice()
{
    if (IDebugServerProvider *provider = DebugServerProviderManager::findProvider(
                m_debugServerProviderId))
        provider->unregisterDevice(this);
}

QString BareMetalDevice::defaultDisplayName()
{
    return QCoreApplication::translate("BareMetal::Internal::BareMetalDevice",
                                       "Bare Metal Device");
}

QString BareMetalDevice::debugServerProviderId() const
{
    return m_debugServerProviderId;
}

void BareMetalDevice::setDebugServerProviderId(const QString &id)
{
    if (id == m_debugServerProviderId)
        return;
    if (IDebugServerProvider *currentProvider =
            DebugServerProviderManager::findProvider(m_debugServerProviderId))
        currentProvider->unregisterDevice(this);
    m_debugServerProviderId = id;
    if (IDebugServerProvider *provider = DebugServerProviderManager::findProvider(id)) {
        setChannelByServerProvider(provider);
        provider->registerDevice(this);
    }
}

void BareMetalDevice::unregisterDebugServerProvider(IDebugServerProvider *provider)
{
    if (provider->id() == m_debugServerProviderId)
        m_debugServerProviderId.clear();
}

void BareMetalDevice::debugServerProviderUpdated(IDebugServerProvider *provider)
{
    IDebugServerProvider *myProvider = DebugServerProviderManager::findProvider(
                m_debugServerProviderId);
    if (provider == myProvider)
        setChannelByServerProvider(provider);
}

void BareMetalDevice::setChannelByServerProvider(IDebugServerProvider *provider)
{
    QTC_ASSERT(provider, return);
    if (provider->engineType() != Debugger::GdbEngineType)
        return;

    const auto gdbProvider = static_cast<GdbServerProvider *>(provider);
    const QString channel = gdbProvider->channelString();
    const int colon = channel.indexOf(':');
    if (colon < 0)
        return;
    QSsh::SshConnectionParameters sshParams = sshParameters();
    sshParams.setHost(channel.left(colon));
    sshParams.setPort(channel.midRef(colon + 1).toUShort());
    setSshParameters(sshParams);
}

void BareMetalDevice::fromMap(const QVariantMap &map)
{
    IDevice::fromMap(map);
    QString providerId = map.value(debugServerProviderIdKeyC).toString();
    if (providerId.isEmpty()) {
        const QString name = displayName();
        if (IDebugServerProvider *provider =
                DebugServerProviderManager::findByDisplayName(name)) {
            providerId = provider->id();
        } else {
            const QSsh::SshConnectionParameters sshParams = sshParameters();
            const auto newProvider = new DefaultGdbServerProvider;
            newProvider->setChannel(sshParams.url);
            newProvider->setDisplayName(name);
            if (DebugServerProviderManager::registerProvider(newProvider))
                providerId = newProvider->id();
            else
                delete newProvider;
        }
    }
    setDebugServerProviderId(providerId);
}

QVariantMap BareMetalDevice::toMap() const
{
    QVariantMap map = IDevice::toMap();
    map.insert(debugServerProviderIdKeyC, debugServerProviderId());
    return map;
}

DeviceProcessSignalOperation::Ptr BareMetalDevice::signalOperation() const
{
    return DeviceProcessSignalOperation::Ptr();
}

IDeviceWidget *BareMetalDevice::createWidget()
{
    return new BareMetalDeviceConfigurationWidget(sharedFromThis());
}

bool BareMetalDevice::canCreateProcess() const
{
    if (IDebugServerProvider *provider = DebugServerProviderManager::findProvider(
                m_debugServerProviderId)) {
        if (provider->engineType() == Debugger::GdbEngineType)
            return true;
    }

    return false;
}

DeviceProcess *BareMetalDevice::createProcess(QObject *parent) const
{
    if (IDebugServerProvider *provider = DebugServerProviderManager::findProvider(
                m_debugServerProviderId)) {
        if (provider->engineType() == Debugger::GdbEngineType)
            return new GdbServerProviderProcess(sharedFromThis(), parent);
    }

    return nullptr;
}

// Factory

BareMetalDeviceFactory::BareMetalDeviceFactory()
    : IDeviceFactory(Constants::BareMetalOsType)
{
    setDisplayName(tr("Bare Metal Device"));
    setCombinedIcon(":/baremetal/images/baremetaldevicesmall.png",
                    ":/baremetal/images/baremetaldevice.png");
    setCanCreate(true);
    setConstructionFunction(&BareMetalDevice::create);
}

IDevice::Ptr BareMetalDeviceFactory::create() const
{
    BareMetalDeviceConfigurationWizard wizard;
    if (wizard.exec() != QDialog::Accepted)
        return {};
    return wizard.device();
}

} //namespace Internal
} //namespace BareMetal

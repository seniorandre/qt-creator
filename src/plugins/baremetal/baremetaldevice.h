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

#pragma once

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/idevicefactory.h>

namespace BareMetal {
namespace Internal {

class IDebugServerProvider;

// BareMetalDevice

class BareMetalDevice final : public ProjectExplorer::IDevice
{
public:
    using Ptr = QSharedPointer<BareMetalDevice>;
    using ConstPtr = QSharedPointer<const BareMetalDevice>;

    static Ptr create() { return Ptr(new BareMetalDevice); }
    ~BareMetalDevice() final;

    static QString defaultDisplayName();

    ProjectExplorer::IDeviceWidget *createWidget() final;

    ProjectExplorer::DeviceProcessSignalOperation::Ptr signalOperation() const final;

    bool canCreateProcess() const final;
    ProjectExplorer::DeviceProcess *createProcess(QObject *parent) const final;

    QString debugServerProviderId() const;
    void setDebugServerProviderId(const QString &id);
    void unregisterDebugServerProvider(IDebugServerProvider *provider);
    void debugServerProviderUpdated(IDebugServerProvider *provider);

    void fromMap(const QVariantMap &map) final;
    QVariantMap toMap() const final;

private:
    BareMetalDevice();

    // Only for GDB servers!
    void setChannelByServerProvider(IDebugServerProvider *provider);

    QString m_debugServerProviderId;
};

// BareMetalDeviceFactory

class BareMetalDeviceFactory final : public ProjectExplorer::IDeviceFactory
{
    Q_OBJECT

public:
   explicit BareMetalDeviceFactory();

   ProjectExplorer::IDevice::Ptr create() const final;
};

} //namespace Internal
} //namespace BareMetal

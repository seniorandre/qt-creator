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

namespace BareMetal {
namespace Constants {

const char BareMetalOsType[] = "BareMetalOsType";

const char ACTION_ID[] = "BareMetal.Action";
const char MENU_ID[] = "BareMetal.Menu";

const char GDB_PROVIDERS_SETTINGS_ID[] = "EE.BareMetal.GdbServerProvidersOptions";

// Debugger Server Provider Ids
const char OPENOCD_PROVIDER_ID[] = "BareMetal.GdbServerProvider.OpenOcd";
const char JLINK_PROVIDER_ID[] = "BareMetal.GdbServerProvider.JLink";
const char DEFAULT_PROVIDER_ID[] = "BareMetal.GdbServerProvider.Default";
const char STLINK_UTIL_PROVIDER_ID[] = "BareMetal.GdbServerProvider.STLinkUtil";

// Toolchain types.
const char IAREW_TOOLCHAIN_TYPEID[] = "BareMetal.ToolChain.Iar";
const char KEIL_TOOLCHAIN_TYPEID[] = "BareMetal.ToolChain.Keil";
const char SDCC_TOOLCHAIN_TYPEID[] = "BareMetal.ToolChain.Sdcc";

} // namespace BareMetal
} // namespace Constants

/*
 * Copyright Intel Corp. 2020
 *
 * ch_driver.h: header file for Cloud-Hypervisor driver functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "internal.h"

int virCHProcessStart(virCHDriverPtr  driver,
                      virDomainObjPtr vm,
                      virDomainRunningReason reason);
int virCHProcessStop(virCHDriverPtr driver,
                     virDomainObjPtr vm,
                     virDomainShutoffReason reason);

int virCHProcessSetupVcpu(virDomainObjPtr vm,
                          unsigned int vcpuid);

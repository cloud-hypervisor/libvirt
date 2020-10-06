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

#include "virdomainobjlist.h"
#include "virthread.h"

#define CH_DRIVER_NAME "CH"
#define CH_CMD "cloud-hypervisor"

#define CH_STATE_DIR RUNSTATEDIR "/libvirt/ch"
#define CH_LOG_DIR LOCALSTATEDIR "/log/libvirt/ch"

typedef struct _virCHDriver virCHDriver;
typedef virCHDriver *virCHDriverPtr;

typedef struct _virCHDriverConfig virCHDriverConfig;
typedef virCHDriverConfig *virCHDriverConfigPtr;

struct _virCHDriverConfig {
    virObject parent;

    char *stateDir;
    char *logDir;

    int cgroupControllers;
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC(virCHDriverConfig, virObjectUnref);

struct _virCHDriver
{
    virMutex lock;

    bool privileged;

    /* Embedded Mode: Not yet supported */
    char *embeddedRoot;

    /* Require lock to get a reference on the object,
     * lockless access thereafter */
    virCapsPtr caps;

    /* Immutable pointer, Immutable object */
    virDomainXMLOptionPtr xmlopt;

    /* Immutable pointer, self-locking APIs */
    virDomainObjListPtr domains;

    /* Cloud-Hypervisor version */
    int version;

    /* Require lock to get reference on 'config',
     * then lockless thereafter */
    virCHDriverConfigPtr config;

    /* pid file FD, ensures two copies of the driver can't use the same root */
    int lockFD;
};

virCapsPtr virCHDriverCapsInit(void);
virCapsPtr virCHDriverGetCapabilities(virCHDriverPtr driver,
                                      bool refresh);
virDomainXMLOptionPtr chDomainXMLConfInit(virCHDriverPtr driver);
virCHDriverConfigPtr virCHDriverConfigNew(void);
virCHDriverConfigPtr virCHDriverGetConfig(virCHDriverPtr driver);
int chExtractVersion(virCHDriverPtr driver);
int chStrToInt(const char *str);

static inline void chDriverLock(virCHDriverPtr driver)
{
    virMutexLock(&driver->lock);
}

static inline void chDriverUnlock(virCHDriverPtr driver)
{
    virMutexUnlock(&driver->lock);
}

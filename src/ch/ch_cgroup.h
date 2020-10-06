/*
 * ch_cgroup.h: CH cgroup management
 *
 * Copyright (C) 2006-2007, 2009-2014 Red Hat, Inc.
 * Copyright (C) 2006 Daniel P. Berrange
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

#include "virusb.h"
#include "vircgroup.h"
#include "domain_conf.h"
#include "ch_conf.h"

int chConnectCgroup(virDomainObjPtr vm);
int chSetupCgroup(virDomainObjPtr vm,
                  size_t nnicindexes,
                  int *nicindexes);
int chSetupCgroupVcpuBW(virCgroupPtr cgroup,
                        unsigned long long period,
                        long long quota);
int chSetupCgroupCpusetCpus(virCgroupPtr cgroup, virBitmapPtr cpumask);
int chSetupGlobalCpuCgroup(virDomainObjPtr vm);
int chRemoveCgroup(virDomainObjPtr vm);

typedef struct _chCgroupEmulatorAllNodesData chCgroupEmulatorAllNodesData;
typedef chCgroupEmulatorAllNodesData *chCgroupEmulatorAllNodesDataPtr;
struct _chCgroupEmulatorAllNodesData {
    virCgroupPtr emulatorCgroup;
    char *emulatorMemMask;
};

int chCgroupEmulatorAllNodesAllow(virCgroupPtr cgroup,
                                  chCgroupEmulatorAllNodesDataPtr *data);
void chCgroupEmulatorAllNodesRestore(chCgroupEmulatorAllNodesDataPtr data);

/*
 * ch_hostdev.h: Cloud Hypervisor hotplug support
 *
 * Copyright Intel Corp. 2020
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

#include "ch_cgroup.h"
#include "ch_domain.h"
#include "ch_hotplug.h"
#include "ch_monitor.h"
#include "ch_process.h"
#include "domain_audit.h"
#include "virerror.h"
#include "virlog.h"

#define VIR_FROM_THIS VIR_FROM_CH

VIR_LOG_INIT("ch.ch_hotplug");

/**
 * virCHDomainSelectHotplugVcpuEntities:
 *
 * @def: domain definition
 * @nvcpus: target vcpu count
 *
 * Validate vcpu entities will be able to be enabled/disabled.
 *
 * Returns the true if the nvcpus can be set, false if not.
 */
static bool
virCHDomainValidateHotplugVcpuEntities(virDomainDefPtr def,
                                       unsigned int nvcpus)
{
    unsigned int maxvcpus = virDomainDefGetVcpusMax(def);

    if (nvcpus < 1 || nvcpus > maxvcpus) {
        virReportError(VIR_ERR_INVALID_ARG,
                       _("requested vcpus is greater than max allowable"
                         " vcpus for the live domain: %u > %u"),
                       nvcpus, maxvcpus);
        return false;
    }

    return true;
}

static int
virCHDomainHotplugSetVcpus(virDomainObjPtr vm,
                           unsigned int nvcpus)
{
    unsigned int maxvcpus = virDomainDefGetVcpusMax(vm->def);
    int oldvcpus = virDomainDefGetVcpus(vm->def);
    virCHDomainObjPrivatePtr priv = vm->privateData;
    virCHMonitorPtr mon = priv->monitor;
    int ret = -1;

    VIR_DEBUG("Setting cpu count to %d", nvcpus);
    ret = virCHMonitorResizeCPU(mon, nvcpus);

    virDomainAuditVcpu(vm, oldvcpus, nvcpus, "update", ret == 0);

    if (ret < 0)
        goto error;

    /* Set cpu status for validation (after the monitor sees the change) */
    for (int i = 0; i < nvcpus; i++) {
        virDomainVcpuDefPtr vcpuinfo = virDomainDefGetVcpu(vm->def, i);
        vcpuinfo->online = true;
    }
    for (int i = nvcpus; i < maxvcpus; i++) {
        virDomainVcpuDefPtr vcpuinfo = virDomainDefGetVcpu(vm->def, i);
        vcpuinfo->online = false;
    }

    ret = 0;

 error:
    return ret;

}

static int
virCHDomainSetVcpusLive(virDomainObjPtr vm,
                        unsigned int nvcpus)
{
    virCHDomainObjPrivatePtr priv = vm->privateData;
    chCgroupEmulatorAllNodesDataPtr emulatorCgroup = NULL;
    int ret = -1;

    if (chCgroupEmulatorAllNodesAllow(priv->cgroup, &emulatorCgroup) < 0)
        goto cleanup;

    if (virCHDomainHotplugSetVcpus(vm, nvcpus) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    chCgroupEmulatorAllNodesRestore(emulatorCgroup);

    return ret;
}

int
virCHDomainSetVcpusInternal(virDomainObjPtr vm,
                            virDomainDefPtr def,
                            unsigned int nvcpus)
{
    int ret = -1;

    if (def) {
        if (!virCHDomainValidateHotplugVcpuEntities(def, nvcpus))
            goto error;

        if (virCHDomainSetVcpusLive(vm, nvcpus) < 0)
            goto error;
    }

    ret = 0;

 error:
    return ret;
}

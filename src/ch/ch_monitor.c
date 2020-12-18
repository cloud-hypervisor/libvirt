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

#include <config.h>

#include <stdio.h>
#include <curl/curl.h>

#include "ch_conf.h"
#include "ch_monitor.h"
#include "viralloc.h"
#include "vircommand.h"
#include "virerror.h"
#include "virfile.h"
#include "virjson.h"
#include "virlog.h"
#include "virtime.h"

#define VIR_FROM_THIS VIR_FROM_CH

VIR_LOG_INIT("ch.ch_monitor");

static virClassPtr virCHMonitorClass;
static void virCHMonitorDispose(void *obj);

static void virCHMonitorThreadInfoFree(virCHMonitorPtr mon);

static int virCHMonitorOnceInit(void)
{
    if (!VIR_CLASS_NEW(virCHMonitor, virClassForObjectLockable()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virCHMonitor);

int virCHMonitorShutdownVMM(virCHMonitorPtr mon);
int virCHMonitorPutNoContent(virCHMonitorPtr mon, const char *endpoint);
int virCHMonitorGet(virCHMonitorPtr mon, const char *endpoint);
int virCHMonitorPingVMM(virCHMonitorPtr mon);

static int
virCHMonitorBuildCPUJson(virJSONValuePtr content, virDomainDefPtr vmdef)
{
    virJSONValuePtr cpus;
    unsigned int maxvcpus = 0;
    unsigned int nvcpus = 0;
    virDomainVcpuDefPtr vcpu;
    size_t i;

    /* count maximum allowed number vcpus and enabled vcpus when boot.*/
    maxvcpus = virDomainDefGetVcpusMax(vmdef);
    for (i = 0; i < maxvcpus; i++) {
        vcpu = virDomainDefGetVcpu(vmdef, i);
        if (vcpu->online)
            nvcpus++;
    }

    if (maxvcpus != 0 || nvcpus != 0) {
        cpus = virJSONValueNewObject();
        if (virJSONValueObjectAppendNumberInt(cpus, "boot_vcpus", nvcpus) < 0)
            goto cleanup;
        if (virJSONValueObjectAppendNumberInt(cpus, "max_vcpus", vmdef->maxvcpus) < 0)
            goto cleanup;
        if (virJSONValueObjectAppend(content, "cpus", cpus) < 0)
            goto cleanup;
    }

    return 0;

 cleanup:
    virJSONValueFree(cpus);
    return -1;
}

static int
virCHMonitorBuildKernelJson(virJSONValuePtr content, virDomainDefPtr vmdef)
{
    virJSONValuePtr kernel;

    if (vmdef->os.kernel == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Kernel image path in this domain is not defined"));
        return -1;
    } else {
        kernel = virJSONValueNewObject();
        if (virJSONValueObjectAppendString(kernel, "path", vmdef->os.kernel) < 0)
            goto cleanup;
        if (virJSONValueObjectAppend(content, "kernel", kernel) < 0)
            goto cleanup;
    }

    return 0;

 cleanup:
    virJSONValueFree(kernel);
    return -1;
}

static int
virCHMonitorBuildCmdlineJson(virJSONValuePtr content, virDomainDefPtr vmdef)
{
    virJSONValuePtr cmdline;

    cmdline = virJSONValueNewObject();
    if (vmdef->os.cmdline) {
        if (virJSONValueObjectAppendString(cmdline, "args", vmdef->os.cmdline) < 0)
            goto cleanup;
        if (virJSONValueObjectAppend(content, "cmdline", cmdline) < 0)
            goto cleanup;
    }

    return 0;

 cleanup:
    virJSONValueFree(cmdline);
    return -1;
}

static int
virCHMonitorBuildMemoryJson(virJSONValuePtr content, virDomainDefPtr vmdef)
{
    virJSONValuePtr memory;
    unsigned long long total_memory = virDomainDefGetMemoryInitial(vmdef) * 1024;

    if (total_memory != 0) {
        memory = virJSONValueNewObject();
        if (virJSONValueObjectAppendNumberUlong(memory, "size", total_memory) < 0)
            goto cleanup;
        if (virJSONValueObjectAppend(content, "memory", memory) < 0)
            goto cleanup;
    }

    return 0;

 cleanup:
    virJSONValueFree(memory);
    return -1;
}

static int
virCHMonitorBuildInitramfsJson(virJSONValuePtr content, virDomainDefPtr vmdef)
{
    virJSONValuePtr initramfs;

    if (vmdef->os.initrd != NULL) {
        initramfs = virJSONValueNewObject();
        if (virJSONValueObjectAppendString(initramfs, "path", vmdef->os.initrd) < 0)
            goto cleanup;
        if (virJSONValueObjectAppend(content, "initramfs", initramfs) < 0)
            goto cleanup;
    }

    return 0;

 cleanup:
    virJSONValueFree(initramfs);
    return -1;
}

static int
virCHMonitorBuildDiskJson(virJSONValuePtr disks, virDomainDiskDefPtr diskdef)
{
    virJSONValuePtr disk;

    if (diskdef->src != NULL && diskdef->src->path != NULL) {
        disk = virJSONValueNewObject();
        if (virJSONValueObjectAppendString(disk, "path", diskdef->src->path) < 0)
            goto cleanup;
        if (diskdef->src->readonly) {
            if (virJSONValueObjectAppendBoolean(disk, "readonly", true) < 0)
                goto cleanup;
        }
        if (virJSONValueArrayAppend(disks, disk) < 0)
            goto cleanup;
    }

    return 0;

 cleanup:
    virJSONValueFree(disk);
    return -1;
}

static int
virCHMonitorBuildDisksJson(virJSONValuePtr content, virDomainDefPtr vmdef)
{
    virJSONValuePtr disks;
    size_t i;

    if (vmdef->ndisks > 0) {
        disks = virJSONValueNewArray();

        for (i = 0; i < vmdef->ndisks; i++) {
            if (virCHMonitorBuildDiskJson(disks, vmdef->disks[i]) < 0)
                goto cleanup;
        }
        if (virJSONValueObjectAppend(content, "disks", disks) < 0)
            goto cleanup;
    }

    return 0;

 cleanup:
    virJSONValueFree(disks);
    return -1;
}

static int
virCHMonitorBuildNetJson(virJSONValuePtr nets, virDomainNetDefPtr netdef,
                         size_t *nnicindexes, int **nicindexes)
{
    virDomainNetType netType = virDomainNetGetActualType(netdef);
    char macaddr[VIR_MAC_STRING_BUFLEN];
    virJSONValuePtr net;

    // check net type at first
    net = virJSONValueNewObject();

    switch (netType) {
    case VIR_DOMAIN_NET_TYPE_ETHERNET:
        if (netdef->guestIP.nips == 1) {
            const virNetDevIPAddr *ip = netdef->guestIP.ips[0];
            g_autofree char *addr = NULL;
            virSocketAddr netmask;
            g_autofree char *netmaskStr = NULL;
            if (!(addr = virSocketAddrFormat(&ip->address)))
                goto cleanup;
            if (virJSONValueObjectAppendString(net, "ip", addr) < 0)
                goto cleanup;

            if (virSocketAddrPrefixToNetmask(ip->prefix, &netmask, AF_INET) < 0) {
                virReportError(VIR_ERR_INTERNAL_ERROR,
                               _("Failed to translate net prefix %d to netmask"),
                               ip->prefix);
                goto cleanup;
            }
            if (!(netmaskStr = virSocketAddrFormat(&netmask)))
                goto cleanup;
            if (virJSONValueObjectAppendString(net, "mask", netmaskStr) < 0)
                goto cleanup;
        }

        /* network and bridge use a tap device, and direct uses a
         * macvtap device
         */
        if (nicindexes && nnicindexes && netdef->ifname) {
            int nicindex;
            if (virNetDevGetIndex(netdef->ifname, &nicindex) < 0 ||
                VIR_APPEND_ELEMENT(*nicindexes, *nnicindexes, nicindex) < 0)
                goto cleanup;
        }

        break;
    case VIR_DOMAIN_NET_TYPE_VHOSTUSER:
        if ((virDomainChrType)netdef->data.vhostuser->type != VIR_DOMAIN_CHR_TYPE_UNIX) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("vhost_user type support UNIX socket in this CH"));
            goto cleanup;
        } else {
            if (virJSONValueObjectAppendString(net, "vhost_socket", netdef->data.vhostuser->data.nix.path) < 0)
                goto cleanup;
            if (virJSONValueObjectAppendBoolean(net, "vhost_user", true) < 0)
                goto cleanup;
        }
        break;
    case VIR_DOMAIN_NET_TYPE_BRIDGE:
    case VIR_DOMAIN_NET_TYPE_NETWORK:
    case VIR_DOMAIN_NET_TYPE_DIRECT:
    case VIR_DOMAIN_NET_TYPE_USER:
    case VIR_DOMAIN_NET_TYPE_SERVER:
    case VIR_DOMAIN_NET_TYPE_CLIENT:
    case VIR_DOMAIN_NET_TYPE_MCAST:
    case VIR_DOMAIN_NET_TYPE_INTERNAL:
    case VIR_DOMAIN_NET_TYPE_HOSTDEV:
    case VIR_DOMAIN_NET_TYPE_UDP:
    case VIR_DOMAIN_NET_TYPE_LAST:
    default:
        virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                       _("Only ethernet and vhost_user type network types are "
                         "supported in this CH"));
        goto cleanup;
    }

    if (netdef->ifname != NULL) {
        if (virJSONValueObjectAppendString(net, "tap", netdef->ifname) < 0)
            goto cleanup;
    }
    if (virJSONValueObjectAppendString(net, "mac", virMacAddrFormat(&netdef->mac, macaddr)) < 0)
        goto cleanup;


    if (netdef->virtio != NULL) {
        if (netdef->virtio->iommu == VIR_TRISTATE_SWITCH_ON) {
            if (virJSONValueObjectAppendBoolean(net, "iommu", true) < 0)
                goto cleanup;
        }
    }
    if (netdef->driver.virtio.queues) {
        if (virJSONValueObjectAppendNumberInt(net, "num_queues", netdef->driver.virtio.queues) < 0)
            goto cleanup;
    }

    if (netdef->driver.virtio.rx_queue_size || netdef->driver.virtio.tx_queue_size) {
        if (netdef->driver.virtio.rx_queue_size != netdef->driver.virtio.tx_queue_size) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
               _("virtio rx_queue_size option %d is not same with tx_queue_size %d"),
               netdef->driver.virtio.rx_queue_size,
               netdef->driver.virtio.tx_queue_size);
            goto cleanup;
        }
        if (virJSONValueObjectAppendNumberInt(net, "queue_size", netdef->driver.virtio.rx_queue_size) < 0)
            goto cleanup;
    }

    if (virJSONValueArrayAppend(nets, net) < 0)
        goto cleanup;

    return 0;

 cleanup:
    virJSONValueFree(net);
    return -1;
}

static int
virCHMonitorBuildNetsJson(virJSONValuePtr content, virDomainDefPtr vmdef,
                          size_t *nnicindexes, int **nicindexes)
{
    virJSONValuePtr nets;
    size_t i;

    if (vmdef->nnets > 0) {
        nets = virJSONValueNewArray();

        for (i = 0; i < vmdef->nnets; i++) {
            if (virCHMonitorBuildNetJson(nets, vmdef->nets[i],
                                         nnicindexes, nicindexes) < 0)
                goto cleanup;
        }
        if (virJSONValueObjectAppend(content, "net", nets) < 0)
            goto cleanup;
    }

    return 0;

 cleanup:
    virJSONValueFree(nets);
    return -1;
}

static int
virCHMonitorBuildDeviceJson(virJSONValuePtr devices, virDomainHostdevDefPtr hostdevdef)
{
    virJSONValuePtr device;

    if (hostdevdef->mode == VIR_DOMAIN_HOSTDEV_MODE_SUBSYS &&
        hostdevdef->source.subsys.type == VIR_DOMAIN_HOSTDEV_SUBSYS_TYPE_PCI) {
        g_autofree char *name = NULL;
        g_autofree char *path = NULL;
        virDomainHostdevSubsysPCIPtr pcisrc = &hostdevdef->source.subsys.u.pci;
        device = virJSONValueNewObject();
        name = g_strdup_printf(VIR_PCI_DEVICE_ADDRESS_FMT, pcisrc->addr.domain,
                               pcisrc->addr.bus, pcisrc->addr.slot,
                               pcisrc->addr.function);
        path = g_strdup_printf("/sys/bus/pci/devices/%s/", name);
        if (!virFileExists(path)) {
            virReportError(VIR_ERR_DEVICE_MISSING,
                           _("host pci device %s not found"), path);
            goto cleanup;
        }
        if (virJSONValueObjectAppendString(device, "path", path) < 0)
            goto cleanup;
        if (virJSONValueArrayAppend(devices, device) < 0)
            goto cleanup;
    }

    return 0;

 cleanup:
    virJSONValueFree(device);
    return -1;
}

static int
virCHMonitorBuildDevicesJson(virJSONValuePtr content, virDomainDefPtr vmdef)
{
    virJSONValuePtr devices;
    size_t i;

    if (vmdef->nhostdevs > 0) {
        devices = virJSONValueNewArray();
        for (i = 0; i < vmdef->nhostdevs; i++) {
            if (virCHMonitorBuildDeviceJson(devices, vmdef->hostdevs[i]) < 0)
                goto cleanup;
        }
        if (virJSONValueObjectAppend(content, "devices", devices) < 0)
            goto cleanup;
    }

    return 0;

 cleanup:
    virJSONValueFree(devices);
    return -1;
}

static int
virCHMonitorBuildVMJson(virDomainDefPtr vmdef, char **jsonstr,
                        size_t *nnicindexes, int **nicindexes)
{
    virJSONValuePtr content = virJSONValueNewObject();
    int ret = -1;

    if (vmdef == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("VM is not defined"));
        goto cleanup;
    }

    if (virCHMonitorBuildCPUJson(content, vmdef) < 0)
        goto cleanup;

    if (virCHMonitorBuildMemoryJson(content, vmdef) < 0)
        goto cleanup;

    if (virCHMonitorBuildKernelJson(content, vmdef) < 0)
        goto cleanup;

    if (virCHMonitorBuildCmdlineJson(content, vmdef) < 0)
        goto cleanup;

    if (virCHMonitorBuildInitramfsJson(content, vmdef) < 0)
        goto cleanup;

    if (virCHMonitorBuildDisksJson(content, vmdef) < 0)
        goto cleanup;

    if (virCHMonitorBuildNetsJson(content, vmdef,
                                  nnicindexes, nicindexes) < 0)
        goto cleanup;

    if (virCHMonitorBuildDevicesJson(content, vmdef) < 0)
        goto cleanup;

    if (!(*jsonstr = virJSONValueToString(content, false)))
        goto cleanup;

    ret = 0;

 cleanup:
    virJSONValueFree(content);
    return ret;
}

/* generate command to launch Cloud-Hypervisor socket
   return -1 - error
           0 - OK
   Caller has to free the cmd
*/
static virCommandPtr
chMonitorBuildSocketCmd(virDomainObjPtr vm, const char *socket_path)
{
    virCommandPtr cmd;

    if (vm->def == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("VM is not defined"));
        return NULL;
    }

    if (vm->def->emulator != NULL)
        cmd = virCommandNew(vm->def->emulator);
    else
        cmd = virCommandNew(CH_CMD);

    virCommandAddArgList(cmd, "--api-socket", socket_path, NULL);

    return cmd;
}

virCHMonitorPtr
virCHMonitorNew(virDomainObjPtr vm, const char *socketdir)
{
    virCHMonitorPtr ret = NULL;
    virCHMonitorPtr mon = NULL;
    virCommandPtr cmd = NULL;
    int pings = 0;

    if (virCHMonitorInitialize() < 0)
        return NULL;

    if (!(mon = virObjectLockableNew(virCHMonitorClass)))
        return NULL;

    mon->socketpath = g_strdup_printf("%s/%s-socket", socketdir, vm->def->name);

    /* prepare to launch Cloud-Hypervisor socket */
    if (!(cmd = chMonitorBuildSocketCmd(vm, mon->socketpath)))
        goto cleanup;

    if (virFileMakePath(socketdir) < 0) {
        virReportSystemError(errno,
                             _("Cannot create socket directory '%s'"),
                             socketdir);
        goto cleanup;
    }

    /* launch Cloud-Hypervisor socket */
    if (virCommandRunAsync(cmd, &mon->pid) < 0)
        goto cleanup;

    /* get a curl handle */
    mon->handle = curl_easy_init();

    /* try to ping VMM socket 5 times to make sure it is ready */
    while (pings < 5) {
        if (virCHMonitorPingVMM(mon) == 0)
            break;
        if (pings == 5)
            goto cleanup;

        g_usleep(100 * 1000);
    }

    /* now has its own reference */
    virObjectRef(mon);
    mon->vm = virObjectRef(vm);

    ret = mon;

 cleanup:
    virCommandFree(cmd);
    return ret;
}

static void virCHMonitorDispose(void *opaque)
{
    virCHMonitorPtr mon = opaque;

    VIR_DEBUG("mon=%p", mon);
    virCHMonitorThreadInfoFree(mon);
    virObjectUnref(mon->vm);
}

void virCHMonitorClose(virCHMonitorPtr mon)
{
    if (!mon)
        return;

    if (mon->pid > 0) {
        /* try cleaning up the Cloud-Hypervisor process */
        virProcessAbort(mon->pid);
        mon->pid = 0;
    }

    if (mon->handle)
        curl_easy_cleanup(mon->handle);

    if (mon->socketpath) {
        if (virFileRemove(mon->socketpath, -1, -1) < 0) {
            VIR_WARN("Unable to remove CH socket file '%s'",
                     mon->socketpath);
        }
        VIR_FREE(mon->socketpath);
    }

    virObjectUnref(mon);
    if (mon->vm)
        virObjectUnref(mon->vm);
}


struct data {
  char trace_ascii; /* 1 or 0 */
};

static void dump(const char *text,
                 FILE *stream,
                 unsigned char *ptr,
                 size_t size,
                 char nohex)
{
    size_t i;
    size_t c;

    unsigned int width = 0x10;

    if (nohex)
        /* without the hex output, we can fit more on screen */
        width = 0x40;

    fprintf(stream, "%s, %10.10lu bytes (0x%8.8lx)\n", text, (unsigned long)size,
            (unsigned long)size);

    for (i = 0; i < size; i += width) {

        fprintf(stream, "%4.4lx: ", (unsigned long)i);

        if (!nohex) {
            /* hex not disabled, show it */
            for (c = 0; c < width; c++) {
                if (i + c < size)
                    fprintf(stream, "%02x ", ptr[i + c]);
                else
                    fputs("   ", stream);
            }
        }

        for (c = 0; (c < width) && (i + c < size); c++) {
            /* check for 0D0A; if found, skip past and start a new line of output */
            if (nohex && (i + c + 1 < size) && ptr[i + c] == 0x0D &&
                ptr[i + c + 1] == 0x0A) {
                i += (c + 2 - width);
                break;
            }
            fprintf(stream, "%c",
                    (ptr[i + c] >= 0x20) && (ptr[i + c] < 0x80) ? ptr[i + c] : '.');
            /* check again for 0D0A, to avoid an extra \n if it's at width */
            if (nohex && (i + c + 2 < size) && ptr[i + c + 1] == 0x0D &&
                ptr[i + c + 2] == 0x0A) {
                i += (c + 3 - width);
                break;
            }
        }
        fputc('\n', stream); /* newline */
    }
    fflush(stream);
}

static int my_trace(CURL *handle,
                    curl_infotype type,
                    char *data,
                    size_t size,
                    void *userp)
{
    struct data *config = (struct data *)userp;
    const char *text = "";
    (void)handle; /* prevent compiler warning */

    switch (type) {
    case CURLINFO_TEXT:
        fprintf(stderr, "== Info: %s", data);
        /* FALLTHROUGH */
    case CURLINFO_END: /* in case a new one is introduced to shock us */
        break;
    case CURLINFO_HEADER_OUT:
        text = "=> Send header";
        break;
    case CURLINFO_DATA_OUT:
        text = "=> Send data";
        break;
    case CURLINFO_SSL_DATA_OUT:
        text = "=> Send SSL data";
        break;
    case CURLINFO_HEADER_IN:
        text = "<= Recv header";
        break;
    case CURLINFO_DATA_IN:
        text = "<= Recv data";
        break;
    case CURLINFO_SSL_DATA_IN:
        text = "<= Recv SSL data";
        break;
    }

    dump(text, stderr, (unsigned char *)data, size, config->trace_ascii);
    return 0;
}

static int
virCHMonitorCurlPerform(CURL *handle)
{
    CURLcode errorCode;
    long responseCode = 0;

    struct data config;

    config.trace_ascii = 1; /* enable ascii tracing */

    curl_easy_setopt(handle, CURLOPT_DEBUGFUNCTION, my_trace);
    curl_easy_setopt(handle, CURLOPT_DEBUGDATA, &config);

    /* the DEBUGFUNCTION has no effect until we enable VERBOSE */
    curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);

    errorCode = curl_easy_perform(handle);

    if (errorCode != CURLE_OK) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("curl_easy_perform() returned an error: %s (%d)"),
                       curl_easy_strerror(errorCode), errorCode);
        return -1;
    }

    errorCode = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE,
                                  &responseCode);

    if (errorCode != CURLE_OK) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("curl_easy_getinfo(CURLINFO_RESPONSE_CODE) returned an "
                         "error: %s (%d)"), curl_easy_strerror(errorCode),
                       errorCode);
        return -1;
    }

    if (responseCode < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("curl_easy_getinfo(CURLINFO_RESPONSE_CODE) returned a "
                         "negative response code"));
        return -1;
    }

    return responseCode;
}

int
virCHMonitorPutNoContent(virCHMonitorPtr mon, const char *endpoint)
{
    char *url;
    int responseCode = 0;
    int ret = -1;
    struct curl_slist *headers = NULL;

    url = g_strdup_printf("%s/%s", URL_ROOT, endpoint);

    virObjectLock(mon);

    /* reset all options of a libcurl session handle at first */
    curl_easy_reset(mon->handle);

    headers = curl_slist_append(headers, "Expect:");
    headers = curl_slist_append(headers, "Transfer-Encoding:");

    curl_easy_setopt(mon->handle, CURLOPT_UNIX_SOCKET_PATH, mon->socketpath);
    curl_easy_setopt(mon->handle, CURLOPT_URL, url);
    curl_easy_setopt(mon->handle, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(mon->handle, CURLOPT_HTTPHEADER, headers);

    responseCode = virCHMonitorCurlPerform(mon->handle);

    virObjectUnlock(mon);

    if (responseCode == 200 || responseCode == 204)
        ret = 0;

    VIR_FREE(url);
    curl_slist_free_all(headers);

    return ret;
}

int
virCHMonitorGet(virCHMonitorPtr mon, const char *endpoint)
{
    char *url;
    int responseCode = 0;
    int ret = -1;

    url = g_strdup_printf("%s/%s", URL_ROOT, endpoint);

    virObjectLock(mon);

    /* reset all options of a libcurl session handle at first */
    curl_easy_reset(mon->handle);

    curl_easy_setopt(mon->handle, CURLOPT_UNIX_SOCKET_PATH, mon->socketpath);
    curl_easy_setopt(mon->handle, CURLOPT_URL, url);

    responseCode = virCHMonitorCurlPerform(mon->handle);

    virObjectUnlock(mon);

    if (responseCode == 200 || responseCode == 204)
        ret = 0;

    VIR_FREE(url);
    return ret;
}

int
virCHMonitorPingVMM(virCHMonitorPtr mon)
{
    return virCHMonitorGet(mon, URL_VMM_PING);
}

int
virCHMonitorShutdownVMM(virCHMonitorPtr mon)
{
    return virCHMonitorPutNoContent(mon, URL_VMM_SHUTDOWN);
}

int
virCHMonitorCreateVM(virCHMonitorPtr mon,
                     size_t *nnicindexes, int **nicindexes)
{
    g_autofree char *url = NULL;
    int responseCode = 0;
    int ret = -1;
    g_autofree char *payload = NULL;
    struct curl_slist *headers = NULL;

    url = g_strdup_printf("%s/%s", URL_ROOT, URL_VM_CREATE);
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Expect:");

    if (virCHMonitorBuildVMJson(mon->vm->def, &payload,
                                nnicindexes, nicindexes) != 0)
        return -1;

    virObjectLock(mon);

    /* reset all options of a libcurl session handle at first */
    curl_easy_reset(mon->handle);

    curl_easy_setopt(mon->handle, CURLOPT_UNIX_SOCKET_PATH, mon->socketpath);
    curl_easy_setopt(mon->handle, CURLOPT_URL, url);
    curl_easy_setopt(mon->handle, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(mon->handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(mon->handle, CURLOPT_POSTFIELDS, payload);

    responseCode = virCHMonitorCurlPerform(mon->handle);

    virObjectUnlock(mon);

    if (responseCode == 200 || responseCode == 204)
        ret = 0;

    curl_slist_free_all(headers);
    VIR_FREE(url);
    VIR_FREE(payload);
    return ret;
}

int
virCHMonitorBootVM(virCHMonitorPtr mon)
{
    return virCHMonitorPutNoContent(mon, URL_VM_BOOT);
}

int
virCHMonitorShutdownVM(virCHMonitorPtr mon)
{
    return virCHMonitorPutNoContent(mon, URL_VM_SHUTDOWN);
}

int
virCHMonitorRebootVM(virCHMonitorPtr mon)
{
    return virCHMonitorPutNoContent(mon, URL_VM_REBOOT);
}

int
virCHMonitorSuspendVM(virCHMonitorPtr mon)
{
    return virCHMonitorPutNoContent(mon, URL_VM_Suspend);
}

int
virCHMonitorResumeVM(virCHMonitorPtr mon)
{
    return virCHMonitorPutNoContent(mon, URL_VM_RESUME);
}

static void
virCHMonitorThreadInfoFree(virCHMonitorPtr mon)
{
    mon->nthreads = 0;
    if (mon->threads)
        VIR_FREE(mon->threads);
}

static size_t
virCHMonitorRefreshThreadInfo(virCHMonitorPtr mon)
{
    virCHMonitorThreadInfoPtr info = NULL;
    g_autofree pid_t *tids = NULL;
    virDomainObjPtr vm = mon->vm;
    size_t ntids = 0;
    size_t i;


    virCHMonitorThreadInfoFree(mon);
    if (virProcessGetPids(vm->pid, &ntids, &tids) < 0) {
        mon->threads = NULL;
        return 0;
    }

    if (VIR_ALLOC_N(info, ntids) < 0)
        return -1;

    for (i = 0; i < ntids; i++) {
        g_autofree char *proc = NULL;
        g_autofree char *data = NULL;

        proc = g_strdup_printf("/proc/%d/task/%d/comm",
                (int)vm->pid, (int)tids[i]);

        if (virFileReadAll(proc, (1<<16), &data) < 0) {
            continue;
        }

        VIR_INFO("VM PID: %d, TID %d, COMM: %s",
                (int)vm->pid, (int)tids[i], data);
        if (STRPREFIX(data, "vcpu")) {
            int index;
            if ((index = strtol(data + 4, NULL, 0)) < 0) {
                VIR_WARN("Index is not specified correctly");
                continue;
            }
            info[i].type = virCHThreadTypeVcpu;
            info[i].vcpuInfo.tid = tids[i];
            info[i].vcpuInfo.online = true;
            info[i].vcpuInfo.cpuid = index;
            VIR_INFO("vcpu%d -> tid: %d", index, tids[i]);
        } else if (STRPREFIX(data, "virtio")) {
            info[i].type = virCHThreadTypeIO;
            info[i].ioInfo.tid = tids[i];
            strncpy(info[i].ioInfo.thrName, data, VIRCH_THREAD_NAME_LEN - 1);
        }else {
            info[i].type = virCHThreadTypeEmulator;
            info[i].emuInfo.tid = tids[i];
            strncpy(info[i].emuInfo.thrName, data, VIRCH_THREAD_NAME_LEN - 1);
        }
        mon->nthreads++;

    }


    mon->threads = info;

    return mon->nthreads;
}

/**
 * virCHMonitorGetThreadInfo:
 * @mon: Pointer to the monitor
 * @refresh: Refresh thread info or not
 *
 * Retrive thread info and store to @threads
 *
 * Returns count of threads on success.
 */
size_t
virCHMonitorGetThreadInfo(virCHMonitorPtr mon, bool refresh,
                          virCHMonitorThreadInfoPtr *threads)
{
    int nthreads = 0;

    if (refresh)
        nthreads = virCHMonitorRefreshThreadInfo(mon);

    *threads = mon->threads;

    return nthreads;
}

/**
 * virCHMonitorGetIOThreads:
 * @mon: Pointer to the monitor
 * @iothreads: Location to return array of IOThreadInfo data
 *
 * Retrieve the list of iothreads defined/running for the machine
 *
 * Returns count of IOThreadInfo structures on success
 *        -1 on error.
 */
int virCHMonitorGetIOThreads(virCHMonitorPtr mon,
                            virDomainIOThreadInfoPtr **iothreads)
{
    size_t nthreads = 0, niothreads=0;
    int i;
    virDomainIOThreadInfoPtr *iothreadinfolist = NULL, iothreadinfo = NULL;

    *iothreads = NULL;
    nthreads = virCHMonitorRefreshThreadInfo(mon);

    if (VIR_ALLOC_N(iothreadinfolist, nthreads) < 0)
        goto cleanup;

    for (i = 0; i < nthreads; i++){
        virBitmapPtr map = NULL;
        if (mon->threads[i].type == virCHThreadTypeIO) {
            if(VIR_ALLOC(iothreadinfo) < 0)
                goto cleanup;

            iothreadinfo->iothread_id = mon->threads[i].ioInfo.tid;

            if (!(map = virProcessGetAffinity(iothreadinfo->iothread_id)))
                goto cleanup;

            if (virBitmapToData(map, &(iothreadinfo->cpumap),
                            &(iothreadinfo->cpumaplen)) < 0) {
                virBitmapFree(map);
                goto cleanup;
            }
            virBitmapFree(map);
            //Append to iothreadinfolist
            iothreadinfolist[niothreads] = iothreadinfo;
            niothreads++;
        }
    }
    VIR_DELETE_ELEMENT_INPLACE(iothreadinfolist,
                                       niothreads, nthreads);
    *iothreads = iothreadinfolist;
    VIR_DEBUG("niothreads = %ld", niothreads);
    return niothreads;

    cleanup:
        if (iothreadinfolist) {
            for (i = 0; i < niothreads; i++)
                VIR_FREE(iothreadinfolist[i]);
            VIR_FREE(iothreadinfolist);
        }
        if (iothreadinfo)
            VIR_FREE(iothreadinfo);
        return -1;
}

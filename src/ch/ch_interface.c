#include "ch_interface.h"
#include "domain_nwfilter.h"
#include "virnetdev.h"
#include "virnetdevtap.h"
#include "network_conf.h"
#include "virlog.h"
#include "viralloc.h"
#include "domain_audit.h"
#include "virnetdevbridge.h"
#include "virebtables.h"
#include "virfile.h"
#include <fcntl.h>

#define VIR_FROM_THIS VIR_FROM_CH

VIR_LOG_INIT("ch.ch_interface");

/* chInterfaceEthernetConnect:
 * @def: the definition of the VM
 * @driver: ch driver data
 * @net: pointer to the VM's interface description
 * @tapfd: array of file descriptor return value for the new device
 * @tapfdsize: number of file descriptors in @tapfd
 *
 * Called *only* called if actualType is VIR_DOMAIN_NET_TYPE_ETHERNET
 * (i.e. if the connection is made with a tap device)
 */
int
chInterfaceEthernetConnect(virDomainDefPtr def,
                           virCHDriverPtr driver,
                           virDomainNetDefPtr net,
                           int *tapfd,
                           size_t tapfdSize)
{
    virMacAddr tapmac;
    int ret = -1;
    unsigned int tap_create_flags = VIR_NETDEV_TAP_CREATE_IFUP;
    bool template_ifname = false;
    virCHDriverConfigPtr cfg = virCHDriverGetConfig(driver);
    const char *tunpath = "/dev/net/tun";
    const char *auditdev = tunpath;

    if (net->backend.tap) {
        tunpath = net->backend.tap;
        if (!driver->privileged) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("cannot use custom tap device in session mode"));
            goto cleanup;
        }
    }

    if (virDomainNetIsVirtioModel(net))
        tap_create_flags |= VIR_NETDEV_TAP_CREATE_VNET_HDR;

    if (net->managed_tap == VIR_TRISTATE_BOOL_NO) {
        if (!net->ifname) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("target dev must be supplied when managed='no'"));
            goto cleanup;
        }
        if (virNetDevExists(net->ifname) != 1) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("target managed='no' but specified dev doesn't exist"));
            goto cleanup;
        }
        if (virNetDevMacVLanIsMacvtap(net->ifname)) {
            auditdev = net->ifname;
            if (virNetDevMacVLanTapOpen(net->ifname, tapfd, tapfdSize) < 0)
                goto cleanup;
            if (virNetDevMacVLanTapSetup(tapfd, tapfdSize,
                                         virDomainNetIsVirtioModel(net)) < 0) {
                goto cleanup;
            }
        } else {
            if (virNetDevTapCreate(&net->ifname, tunpath, tapfd, tapfdSize,
                                   tap_create_flags) < 0)
                goto cleanup;
        }
    } else {
        if (!net->ifname ||
            STRPREFIX(net->ifname, VIR_NET_GENERATED_TAP_PREFIX) ||
            strchr(net->ifname, '%')) {
            g_free(net->ifname);
            net->ifname = g_strdup(VIR_NET_GENERATED_TAP_PREFIX "%d");
            /* avoid exposing vnet%d in getXMLDesc or error outputs */
            template_ifname = true;
        }
        if (virNetDevTapCreate(&net->ifname, tunpath, tapfd, tapfdSize,
                               tap_create_flags) < 0) {
            goto cleanup;
        }

        /* The tap device's MAC address cannot match the MAC address
         * used by the guest. This results in "received packet on
         * vnetX with own address as source address" error logs from
         * the kernel.
         */
        virMacAddrSet(&tapmac, &net->mac);
        if (tapmac.addr[0] == 0xFE)
            tapmac.addr[0] = 0xFA;
        else
            tapmac.addr[0] = 0xFE;

        if (virNetDevSetMAC(net->ifname, &tapmac) < 0)
            goto cleanup;

        if (virNetDevSetOnline(net->ifname, true) < 0)
            goto cleanup;
    }

    if (net->script &&
        virNetDevRunEthernetScript(net->ifname, net->script) < 0)
        goto cleanup;

    if (cfg->macFilter &&
        ebtablesAddForwardAllowIn(driver->ebtables,
                                  net->ifname,
                                  &net->mac) < 0)
        goto cleanup;

    if (net->filter &&
        virDomainConfNWFilterInstantiate(def->name, def->uuid, net, false) < 0) {
        goto cleanup;
    }

    virDomainAuditNetDevice(def, net, auditdev, true);

    ret = 0;

 cleanup:
    if (ret < 0) {
        size_t i;

        virDomainAuditNetDevice(def, net, auditdev, false);
        for (i = 0; i < tapfdSize && tapfd[i] >= 0; i++)
            VIR_FORCE_CLOSE(tapfd[i]);
        if (template_ifname)
            g_free(net->ifname);
    }
    virObjectUnref(cfg);

    return ret;
}

/* chInterfaceBridgeConnect:
 * @def: the definition of the VM
 * @driver: qemu driver data
 * @net: pointer to the VM's interface description
 * @tapfd: array of file descriptor return value for the new device
 * @tapfdsize: number of file descriptors in @tapfd
 *
 * Called *only* called if actualType is VIR_DOMAIN_NET_TYPE_NETWORK or
 * VIR_DOMAIN_NET_TYPE_BRIDGE (i.e. if the connection is made with a tap
 * device connecting to a bridge device)
 */
int
chInterfaceBridgeConnect(virDomainDefPtr def,
                           virCHDriverPtr driver,
                           virDomainNetDefPtr net,
                           int *tapfd,
                           size_t *tapfdSize)
{
    const char *brname;
    int ret = 0;
    unsigned int tap_create_flags = VIR_NETDEV_TAP_CREATE_IFUP;
    bool template_ifname = false;
    virCHDriverConfigPtr cfg = virCHDriverGetConfig(driver);
    const char *tunpath = "/dev/net/tun";

    if (net->backend.tap) {
        tunpath = net->backend.tap;
        if (!driver->privileged) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("cannot use custom tap device in session mode"));
            goto cleanup;
        }
    }

    if (!(brname = virDomainNetGetActualBridgeName(net))) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s", _("Missing bridge name"));
        goto cleanup;
    }

    if (!net->ifname ||
        STRPREFIX(net->ifname, VIR_NET_GENERATED_TAP_PREFIX) ||
        strchr(net->ifname, '%')) {
        g_free(net->ifname);
        net->ifname = g_strdup(VIR_NET_GENERATED_TAP_PREFIX "%d");
        /* avoid exposing vnet%d in getXMLDesc or error outputs */
        template_ifname = true;
    }

    if (virDomainNetIsVirtioModel(net))
        tap_create_flags |= VIR_NETDEV_TAP_CREATE_VNET_HDR;

    if (driver->privileged) {
        if (virNetDevTapCreateInBridgePort(brname, &net->ifname, &net->mac,
                                           def->uuid, tunpath, tapfd, *tapfdSize,
                                           virDomainNetGetActualVirtPortProfile(net),
                                           virDomainNetGetActualVlan(net),
                                           virDomainNetGetActualPortOptionsIsolated(net),
                                           net->coalesce, 0, NULL,
                                           tap_create_flags) < 0) {
            virDomainAuditNetDevice(def, net, tunpath, false);
            goto cleanup;
        }
        if (virDomainNetGetActualBridgeMACTableManager(net)
            == VIR_NETWORK_BRIDGE_MAC_TABLE_MANAGER_LIBVIRT) {
            /* libvirt is managing the FDB of the bridge this device
             * is attaching to, so we need to turn off learning and
             * unicast_flood on the device to prevent the kernel from
             * adding any FDB entries for it. We will add an fdb
             * entry ourselves (during qemuInterfaceStartDevices(),
             * using the MAC address from the interface config.
             */
            if (virNetDevBridgePortSetLearning(brname, net->ifname, false) < 0)
                goto cleanup;
            if (virNetDevBridgePortSetUnicastFlood(brname, net->ifname, false) < 0)
                goto cleanup;
        }
    } else {
            /*
             *  Supporting non-privileged mode, session mode not supported
             */
            virReportError(VIR_ERR_OPERATION_UNSUPPORTED, "%s",
                           _("Cannot connect to bridge in session mode"));
            goto cleanup;
    }

    virDomainAuditNetDevice(def, net, tunpath, true);

    if (cfg->macFilter &&
        ebtablesAddForwardAllowIn(driver->ebtables,
                                  net->ifname,
                                  &net->mac) < 0)
        goto cleanup;

    if (net->filter &&
        virDomainConfNWFilterInstantiate(def->name, def->uuid, net, false) < 0) {
        goto cleanup;
    }

    ret = 0;

 cleanup:
    if (ret < 0) {
        size_t i;
        for (i = 0; i < *tapfdSize && tapfd[i] >= 0; i++)
            VIR_FORCE_CLOSE(tapfd[i]);
        if (template_ifname)
            g_free(net->ifname);
    }
    virObjectUnref(cfg);

    return ret;
}

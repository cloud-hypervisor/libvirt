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

#include "configmake.h"
#include "viralloc.h"
#include "virconf.h"
#include "vircommand.h"
#include "virlog.h"
#include "virobject.h"
#include "virstring.h"
#include "virutil.h"

#include "ch_conf.h"
#include "ch_domain.h"

#define VIR_FROM_THIS VIR_FROM_CH

VIR_LOG_INIT("ch.ch_conf");

static virClassPtr virCHDriverConfigClass;
static void virCHDriverConfigDispose(void *obj);

static int virCHConfigOnceInit(void)
{
    if (!VIR_CLASS_NEW(virCHDriverConfig, virClassForObject()))
        return -1;

    return 0;
}

VIR_ONCE_GLOBAL_INIT(virCHConfig);


/* Functions */
virCapsPtr virCHDriverCapsInit(void)
{
    virCapsPtr caps;
    virCapsGuestPtr guest;

    if ((caps = virCapabilitiesNew(virArchFromHost(),
                                   false, false)) == NULL)
        goto cleanup;

    if (!(caps->host.numa = virCapabilitiesHostNUMANewHost()))
        goto cleanup;

    if (virCapabilitiesInitCaches(caps) < 0)
        goto cleanup;

    if ((guest = virCapabilitiesAddGuest(caps,
                                         VIR_DOMAIN_OSTYPE_HVM,
                                         caps->host.arch,
                                         NULL,
                                         NULL,
                                         0,
                                         NULL)) == NULL)
        goto cleanup;

    if (virCapabilitiesAddGuestDomain(guest,
                                      VIR_DOMAIN_VIRT_CH,
                                      NULL,
                                      NULL,
                                      0,
                                      NULL) == NULL)
        goto cleanup;

    return caps;

 cleanup:
    virObjectUnref(caps);
    return NULL;
}

/**
 * virCHDriverGetCapabilities:
 *
 * Get a reference to the virCapsPtr instance for the
 * driver. If @refresh is true, the capabilities will be
 * rebuilt first
 *
 * The caller must release the reference with virObjetUnref
 *
 * Returns: a reference to a virCapsPtr instance or NULL
 */
virCapsPtr virCHDriverGetCapabilities(virCHDriverPtr driver,
                                      bool refresh)
{
    virCapsPtr ret;
    if (refresh) {
        virCapsPtr caps = NULL;
        if ((caps = virCHDriverCapsInit()) == NULL)
            return NULL;

        chDriverLock(driver);
        virObjectUnref(driver->caps);
        driver->caps = caps;
    } else {
        chDriverLock(driver);
    }

    ret = virObjectRef(driver->caps);
    chDriverUnlock(driver);
    return ret;
}

virDomainXMLOptionPtr
chDomainXMLConfInit(virCHDriverPtr driver)
{
    virCHDriverDomainDefParserConfig.priv = driver;
    return virDomainXMLOptionNew(&virCHDriverDomainDefParserConfig,
                                 &virCHDriverPrivateDataCallbacks,
                                 NULL, NULL, NULL);
}

virCHDriverConfigPtr
virCHDriverConfigNew(void)
{
    virCHDriverConfigPtr cfg;

    if (virCHConfigInitialize() < 0)
        return NULL;

    if (!(cfg = virObjectNew(virCHDriverConfigClass)))
        return NULL;

    cfg->stateDir = g_strdup(CH_STATE_DIR);
    cfg->logDir = g_strdup(CH_LOG_DIR);
    cfg->configDir = g_strdup(CH_CONF_BASE_DIR);
    cfg->autostartDir = g_strdup_printf("%s/autostart", cfg->configDir);
    cfg->logTimestamp = true;
    cfg->stdioLogD = false;
    cfg->cgroupControllers = -1; /* Auto detect */

    return cfg;
}

virCHDriverConfigPtr virCHDriverGetConfig(virCHDriverPtr driver)
{
    virCHDriverConfigPtr cfg;
    chDriverLock(driver);
    cfg = virObjectRef(driver->config);
    chDriverUnlock(driver);
    return cfg;
}

static void
virCHDriverConfigDispose(void *obj)
{
    virCHDriverConfigPtr cfg = obj;

    g_free(cfg->stateDir);
    g_free(cfg->logDir);
}

int
virCHVersionString(const char *str, unsigned long *version)
{
    unsigned int major, minor = 0, micro = 0;

    /**
     * Remove the from "cloud-hypervisor " to last "/" if present any from version output
     * Below snippet will give something line v<>.<>.(any dirty data if any)
     * 
     * upstream CLH release version : cloud-hypervisor v32.0.0
     * upstream CLH main branch build : cloud-hypervisor v33.0-104-ge0e3779e-dirty
     * msft CLH main branch build : cloud-hypervisor msft/v32.0.131-1-ga5d6db5c-dirty
    */
    char *clh_string = "cloud-hypervisor ";
    char *last_slash = strrchr(str, '/');
    char *clh_string_start = strstr(str, clh_string);
    char *version_string = "";

    if (clh_string_start == NULL){
        VIR_ERROR("No matching format found: %s\n", str);
        return -1;
    }

    /* If "/" is present in the input version string*/
    if (last_slash != NULL) {
        if (clh_string_start > last_slash) {
            /* -- Return -1 if "/" index is lower than "cloud-hypervisor " */
            VIR_ERROR("Invalid format found: %s\n", str);
            return -1;
        } else {
            version_string = last_slash + 1;
        }
    } else {
        /* If "/" is not present in input version string, remove "cloud-hypervisor " from beginning*/
        version_string = str + strlen(clh_string);
    }

    /* Try to get major, minor, micro version*/
    VIR_DEBUG("version string after trim down: %s\n", version_string);
    if(sscanf(version_string, "v%d.%d.%d", &major, &minor, &micro) != 3){
        /* Try to get major, minor version*/
        if(sscanf(version_string, "v%d.%d", &major, &minor) != 2){
            VIR_ERROR("Can not extract CLH version: %s\n", version_string);
            return -1;
        }
    }

    // Print the extracted version numbers
    VIR_DEBUG("Major: %d\n", major);
    VIR_DEBUG("Minor: %d\n", minor);
    VIR_DEBUG("Micro: %d\n", micro);

    *version = 1000000 * major + 1000 * minor + micro;

    return 0;

}

static int
chExtractVersionInfo(int *retversion)
{
    int ret = -1;
    unsigned long version;
    char *help = NULL;
    char *tmp;
    virCommandPtr cmd = virCommandNewArgList(CH_CMD, "--version", NULL);

    if (retversion)
        *retversion = 0;

    virCommandAddEnvString(cmd, "LC_ALL=C");
    virCommandSetOutputBuffer(cmd, &help);

    if (virCommandRun(cmd, NULL) < 0)
        goto cleanup;

    tmp = help;

    if (virCHVersionString(tmp, &version) < 0)
        goto cleanup;

    // v0.9.0 is the minimum supported version
    if ((unsigned int)(version / 1000000) < 1) {
        if (((unsigned int)((unsigned long)(version % 1000000)) / 1000) < 9) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           _("Cloud-Hypervisor version is too old (v0.9.0 is the minimum supported version)"));
            goto cleanup;
        }
    }


    if (retversion)
        *retversion = version;

    ret = 0;

 cleanup:
    virCommandFree(cmd);
    g_free(help);

    return ret;
}

int chExtractVersion(virCHDriverPtr driver)
{
    if (driver->version > 0)
        return 0;

    if (chExtractVersionInfo(&driver->version) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("Could not extract Cloud-Hypervisor version"));
        return -1;
    }

    return 0;
}

int chStrToInt(const char *str)
{
    int val;

    if (virStrToLong_i(str, NULL, 10, &val) < 0)
        return 0;

    return val;
}

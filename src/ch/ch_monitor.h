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

#include <curl/curl.h>

#include "virobject.h"
#include "domain_conf.h"

#define URL_ROOT "http://localhost/api/v1"
#define URL_VMM_SHUTDOWN "vmm.shutdown"
#define URL_VMM_PING "vmm.ping"
#define URL_VM_CREATE "vm.create"
#define URL_VM_DELETE "vm.delete"
#define URL_VM_BOOT "vm.boot"
#define URL_VM_SHUTDOWN "vm.shutdown"
#define URL_VM_REBOOT "vm.reboot"
#define URL_VM_Suspend "vm.pause"
#define URL_VM_RESUME "vm.resume"

#define VIRCH_THREAD_NAME_LEN   16

typedef enum {
    virCHThreadTypeEmulator,
    virCHThreadTypeVcpu,
    virCHThreadTypeIO,
    virCHThreadTypeMax
} virCHThreadType;

typedef struct _virCHMonitorCPUInfo virCHMonitorCPUInfo;
typedef virCHMonitorCPUInfo *virCHMonitorCPUInfoPtr;

struct _virCHMonitorCPUInfo {
    int cpuid;
    pid_t tid;

    bool online;
};

typedef struct _virCHMonitorEmuInfo virCHMonitorEmuInfo;
typedef virCHMonitorEmuInfo *virCHMonitorEmuInfoPtr;

struct _virCHMonitorEmuInfo {
    char    thrName[VIRCH_THREAD_NAME_LEN];
    pid_t   tid;
};

typedef struct _virCHMonitorIOInfo virCHMonitorIOInfo;
typedef virCHMonitorIOInfo *virCHMonitorIOInfoPtr;

struct _virCHMonitorIOInfo {
    char    thrName[VIRCH_THREAD_NAME_LEN];
    pid_t   tid;
};

typedef struct _virCHMonitorThreadInfo virCHMonitorThreadInfo;
typedef virCHMonitorThreadInfo *virCHMonitorThreadInfoPtr;

struct _virCHMonitorThreadInfo {
    virCHThreadType type;

    union {
        virCHMonitorCPUInfo vcpuInfo;
        virCHMonitorEmuInfo emuInfo;
        virCHMonitorIOInfo ioInfo;
    };
};

typedef struct _virCHMonitor virCHMonitor;
typedef virCHMonitor *virCHMonitorPtr;

struct _virCHMonitor {
    virObjectLockable parent;

    CURL *handle;

    char *socketpath;

    pid_t pid;

    virDomainObjPtr vm;

    size_t nthreads;
    virCHMonitorThreadInfoPtr threads;
};

virCHMonitorPtr virCHMonitorNew(virDomainObjPtr vm, const char *socketdir);
void virCHMonitorClose(virCHMonitorPtr mon);

int virCHMonitorCreateVM(virCHMonitorPtr mon,
                         size_t *nnicindexes, int **nicindexes);
int virCHMonitorBootVM(virCHMonitorPtr mon);
int virCHMonitorShutdownVM(virCHMonitorPtr mon);
int virCHMonitorRebootVM(virCHMonitorPtr mon);
int virCHMonitorSuspendVM(virCHMonitorPtr mon);
int virCHMonitorResumeVM(virCHMonitorPtr mon);

size_t virCHMonitorGetThreadInfo(virCHMonitorPtr mon, bool refresh,
                                 virCHMonitorThreadInfoPtr *threads);

#include "domain_conf.h"
#include "ch_conf.h"

int
chInterfaceEthernetConnect(virDomainDefPtr def,
                           virCHDriverPtr driver,
                           virDomainNetDefPtr net,
                           int *tapfd,
                           size_t tapfdSize);

int chInterfaceBridgeConnect(virDomainDefPtr def,
                           virCHDriverPtr driver,
                           virDomainNetDefPtr net,
                           int *tapfd,
                           size_t *tapfdSize);

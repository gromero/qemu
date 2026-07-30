#ifndef PCIE_TDISP_H
#define PCIE_TDISP_H

#include "qom/object.h"

#define TYPE_PCIE_TDISP_IF "pcie-tdisp-interface"

typedef struct PcieTdispIfClass PcieTdispIfClass;
DECLARE_CLASS_CHECKERS(PcieTdispIfClass, PCIE_TDISP_IF, TYPE_PCIE_TDISP_IF)
#define PCIE_TDISP_IF(obj) INTERFACE_CHECK(PcieTdispIf, (obj), TYPE_PCIE_TDISP_IF)

typedef struct PcieTdispIf PcieTdispIf;

struct PcieTdispIfClass {
    InterfaceClass parent_class;

    uint8_t (*get_device_interface_state)(PcieTdispIf *pcie_tdisp_if);
};

typedef struct PCIDevice PCIDevice;

uint8_t pcie_tdisp_get_device_interface_state(PCIDevice *dev);

#endif

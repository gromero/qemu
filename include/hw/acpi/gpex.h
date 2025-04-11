#ifndef HW_ACPI_GPEX_H
#define HW_ACPI_GPEX_H


#include "hw/acpi/pcihp.h"

/* void gpex_init(PCIDevice *gpex,  AcpiPciHpState *as); */

void gpex_device_pre_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                                Error **errp);
void gpex_device_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                            Error **errp);
void gpex__device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp);
void gpex_device_unplug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                              Error **errp);

#endif /* HW_ACPI_GPEX_H */

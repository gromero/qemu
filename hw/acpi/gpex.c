#include "qemu/osdep.h"
#include "hw/acpi/gpex.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"

#include "qemu/log.h"

void gpex_device_pre_plug_cb(HotplugHandler *hp_handler, DeviceState *dev,
                                Error **errp)
{
    qemu_log("gpex_device_pre_plug_cb() called!\n");

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        acpi_pcihp_device_pre_plug_cb(hp_handler, dev, errp);
    } else {
        qemu_log("meh, can't ACPI-ish hotplug anything other than PCI devices!\n");
    }
}

void gpex_device_plug_cb(HotplugHandler *hp_handler, DeviceState *dev,
                            Error **errp)
{
    GPEXRootState *grs = GPEX_ROOT_DEVICE(hp_handler);

    qemu_log("gpex_device_plug_cb() called!\n");

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        acpi_pcihp_device_plug_cb(hp_handler, &grs->acpi_pci_hotplug, dev, errp);
    } else {
        qemu_log("meh, can't ACPI-ish hotplug anything other than PCI devices!\n");
        /*
	error_setg(errp, "acpi: device plug request for not supported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
        */
    }
}

void gpex__device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp)
{
}

void gpex_device_unplug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                              Error **errp)
{
}

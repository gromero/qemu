#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/acpi/gpex.h"

void gpex_device_pre_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                                Error **errp)
{
    qemu_log("gpex_device_pre_plug_cb() called!\n");
}

void gpex_device_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                            Error **errp)
{
    qemu_log("gpex_device_plug_cb() called!\n");
}

void gpex__device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp)
{
}

void gpex_device_unplug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                              Error **errp)
{
}

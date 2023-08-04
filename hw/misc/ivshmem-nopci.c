#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/sysbus.h"
#include "sysemu/kvm.h"
#include "migration/blocker.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "qemu/event_notifier.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"
#include "chardev/char-fe.h"
#include "sysemu/hostmem.h"
#include "qapi/visitor.h"
#include "qom/object.h"
#include "hw/boards.h"

#define TYPE_IVSHMEM_NO_PCI "ivshmem-no-pci"
typedef struct IVSHMEMNOPCI IVSHMEMNOPCIState;
DECLARE_INSTANCE_CHECKER(IVSHMEMNOPCIState, IVSHMEM_NO_PCI, TYPE_IVSHMEM_NO_PCI)

struct IVSHMEMNOPCI {
    /*< private >*/
    SysBusDevice parent_obj;

    int x;
    int y;
    char *id;
};

static void my_device_realize(DeviceState *dev, Error **errp) {
    MachineState *machine = MACHINE(qdev_get_machine());
    CPUState *cpu = qemu_get_cpu(0);

//    NVICState *nvic = NVIC(cpu->env_ptr->nvic);
/*

hw/intc/armv7m_nvic.c
static void armv7m_nvic_reset(DeviceState *dev)
{
    int resetprio;
    NVICState *s = NVIC(dev);


    (gdb) p (*(NVICState *)cpu->env_ptr->nvic)->sysregmem->name
$10 = 0x5555571914c0 "nvic_sysregs"

*/

//    ARMCPU *cpu = qemu_get_cpu(0);
//    CPUARMState *env = &cpu->env;


    /* Get the first CPU of the machine */
//    cpu = CPU(qemu_get_cpu(machine, 0));

    printf("%s\n", machine->kernel_filename);
    printf("%p\n", cpu);
//  printf("vnic @%p\n", nvic);
}

static void ivshmem_no_pci_instance_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    printf("XXXXXXXXXXXXXXXXXX%d\n", sysbus_has_irq(sbd, 14));
}

static void ivshmem_no_pci_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	Object *o = OBJECT(klass);

	SysBusDevice *sb = SYS_BUS_DEVICE(o);

	dc->hotpluggable = true;
	dc->user_creatable = true;
	dc->realize = my_device_realize;

	set_bit(DEVICE_CATEGORY_MISC, dc->categories);

	printf("YYYYYYYYYYYYYYYYYY%p\n", sb);

	IVSHMEMNOPCIState inp;
	inp.x = 10;
	printf("%d\n", inp.x);
}

static const TypeInfo ivshmem_no_pci_info = {
	.name = TYPE_IVSHMEM_NO_PCI,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(IVSHMEMNOPCIState),
	.instance_init = ivshmem_no_pci_instance_init,
	.class_init = ivshmem_no_pci_class_init,
};

static void ivshmem_no_pci_register_types(void)
{
	type_register_static(&ivshmem_no_pci_info);
}

type_init(ivshmem_no_pci_register_types);

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/acpi/aml-build.h"
#include "hw/pci-host/gpex.h"
#include "hw/arm/virt.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_host.h"
#include "hw/acpi/cxl.h"
#include "hw/acpi/pcihp.h"

#include "qapi/qmp/qnum.h"
#include "qom/qom-qobject.h"

AcpiPciHpState s;

static void acpi_dsdt_add_pci_route_table(Aml *dev, uint32_t irq,
                                          Aml *scope, uint8_t bus_num)
{
    Aml *method, *crs;
    int i, slot_no;

    /* Declare the PCI Routing Table. */
    Aml *rt_pkg = aml_varpackage(PCI_SLOT_MAX * PCI_NUM_PINS);
    for (slot_no = 0; slot_no < PCI_SLOT_MAX; slot_no++) {
        for (i = 0; i < PCI_NUM_PINS; i++) {
            int gsi = (i + slot_no) % PCI_NUM_PINS;
            Aml *pkg = aml_package(4);
            aml_append(pkg, aml_int((slot_no << 16) | 0xFFFF));
            aml_append(pkg, aml_int(i));
            aml_append(pkg, aml_name("L%.02X%X", bus_num, gsi));
            aml_append(pkg, aml_int(0));
            aml_append(rt_pkg, pkg);
        }
    }
    aml_append(dev, aml_name_decl("_PRT", rt_pkg));

    /* Create GSI link device */
    for (i = 0; i < PCI_NUM_PINS; i++) {
        uint32_t irqs = irq + i;
        Aml *dev_gsi = aml_device("L%.02X%X", bus_num, i);
        aml_append(dev_gsi, aml_name_decl("_HID", aml_string("PNP0C0F")));
        aml_append(dev_gsi, aml_name_decl("_UID", aml_int(i)));
        crs = aml_resource_template();
        aml_append(crs,
                   aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                 AML_EXCLUSIVE, &irqs, 1));
        aml_append(dev_gsi, aml_name_decl("_PRS", crs));
        crs = aml_resource_template();
        aml_append(crs,
                   aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                 AML_EXCLUSIVE, &irqs, 1));
        aml_append(dev_gsi, aml_name_decl("_CRS", crs));
        method = aml_method("_SRS", 1, AML_NOTSERIALIZED);
        aml_append(dev_gsi, method);
        aml_append(scope, dev_gsi);
    }
}

static void acpi_dsdt_add_pci_osc(Aml *dev)
{
    Aml *method, *UUID, *ifctx, *ifctx1, *elsectx, *buf;

    /* Declare an _OSC (OS Control Handoff) method */
    aml_append(dev, aml_name_decl("SUPP", aml_int(0)));
    aml_append(dev, aml_name_decl("CTRL", aml_int(0)));
    method = aml_method("_OSC", 4, AML_NOTSERIALIZED);
    aml_append(method,
        aml_create_dword_field(aml_arg(3), aml_int(0), "CDW1"));

    /* PCI Firmware Specification 3.0
     * 4.5.1. _OSC Interface for PCI Host Bridge Devices
     * The _OSC interface for a PCI/PCI-X/PCI Express hierarchy is
     * identified by the Universal Unique IDentifier (UUID)
     * 33DB4D5B-1FF7-401C-9657-7441C03DD766
     */
    UUID = aml_touuid("33DB4D5B-1FF7-401C-9657-7441C03DD766");
    ifctx = aml_if(aml_equal(aml_arg(0), UUID));
    aml_append(ifctx,
        aml_create_dword_field(aml_arg(3), aml_int(4), "CDW2"));
    aml_append(ifctx,
        aml_create_dword_field(aml_arg(3), aml_int(8), "CDW3"));
    aml_append(ifctx, aml_store(aml_name("CDW2"), aml_name("SUPP")));
    aml_append(ifctx, aml_store(aml_name("CDW3"), aml_name("CTRL")));

    /*
     * Allow OS control for all 5 features:
     * ~PCIeHotplug~ SHPCHotplug PME AER PCIeCapability.
     */
    aml_append(ifctx, aml_and(aml_name("CTRL"), aml_int(0x1E),
                              aml_name("CTRL")));

    ifctx1 = aml_if(aml_lnot(aml_equal(aml_arg(1), aml_int(0x1))));
    aml_append(ifctx1, aml_or(aml_name("CDW1"), aml_int(0x08),
                              aml_name("CDW1")));
    aml_append(ifctx, ifctx1);

    ifctx1 = aml_if(aml_lnot(aml_equal(aml_name("CDW3"), aml_name("CTRL"))));
    aml_append(ifctx1, aml_or(aml_name("CDW1"), aml_int(0x10),
                              aml_name("CDW1")));
    aml_append(ifctx, ifctx1);

    aml_append(ifctx, aml_store(aml_name("CTRL"), aml_name("CDW3")));
    aml_append(ifctx, aml_store(aml_int(0x1F), aml_name("CDW2")));
    aml_append(ifctx, aml_return(aml_arg(3)));
    aml_append(method, ifctx);

    elsectx = aml_else();
    aml_append(elsectx, aml_or(aml_name("CDW1"), aml_int(4),
                               aml_name("CDW1")));
    aml_append(elsectx, aml_return(aml_arg(3)));
    aml_append(method, elsectx);
    aml_append(dev, method);

    method = aml_method("_DSM", 4, AML_NOTSERIALIZED);

    /* PCI Firmware Specification 3.0
     * 4.6.1. _DSM for PCI Express Slot Information
     * The UUID in _DSM in this context is
     * {E5C937D0-3553-4D7A-9117-EA4D19C3434D}
     */
    UUID = aml_touuid("E5C937D0-3553-4D7A-9117-EA4D19C3434D");
    ifctx = aml_if(aml_equal(aml_arg(0), UUID));
    ifctx1 = aml_if(aml_equal(aml_arg(2), aml_int(0)));
    uint8_t byte_list[1] = {1};
    buf = aml_buffer(1, byte_list);
    aml_append(ifctx1, aml_return(buf));
    aml_append(ifctx, ifctx1);
    aml_append(method, ifctx);

    byte_list[0] = 0;
    buf = aml_buffer(1, byte_list);
    aml_append(method, aml_return(buf));
    aml_append(dev, method);
}

static bool is_devfn_ignored_generic(const int devfn, const PCIBus *bus)
{
    const PCIDevice *pdev = bus->devices[devfn];

    if (PCI_FUNC(devfn)) {
        if (IS_PCI_BRIDGE(pdev)) {
            /*
             * Ignore only hotplugged PCI bridges on !0 functions, but
             * allow describing cold plugged bridges on all functions
             */
            if (DEVICE(pdev)->hotplugged) {
                return true;
            }
        }
    }
    return false;
}

static bool is_devfn_ignored_hotplug(const int devfn, const PCIBus *bus)
{
    PCIDevice *pdev = bus->devices[devfn];
    if (pdev) {
        return is_devfn_ignored_generic(devfn, bus) ||
               !DEVICE_GET_CLASS(pdev)->hotpluggable ||
               /* Cold plugged bridges aren't themselves hot-pluggable */
               (IS_PCI_BRIDGE(pdev) && !DEVICE(pdev)->hotplugged);
    } else { /* non populated slots */
         /*
         * hotplug is supported only for non-multifunction device
         * so generate device description only for function 0
         */
        if (PCI_FUNC(devfn) ||
            (pci_bus_is_express(bus) && PCI_SLOT(devfn) > 0)) {
            return true;
        }
    }
    return false;
}

static void build_append_pcihp_notify_entry(Aml *method, int slot)
{
    Aml *if_ctx;
    int32_t devfn = PCI_DEVFN(slot, 0);

    if_ctx = aml_if(aml_and(aml_arg(0), aml_int(0x1U << slot), NULL));
    aml_append(if_ctx, aml_notify(aml_name("S%.02X", devfn), aml_arg(1)));
    aml_append(method, if_ctx);
}

void build_append_pcihp_slots_(Aml *, PCIBus *);
void build_append_pcihp_slots_(Aml *parent_scope, PCIBus *bus)
{
    int devfn;
    Aml *dev, *notify_method = NULL, *method;
/*
    QObject *bsel = object_property_get_qobject(OBJECT(bus),
                        ACPI_PCIHP_PROP_BSEL, NULL);
    uint64_t bsel_val = qnum_get_uint(qobject_to(QNum, bsel));
    qobject_unref(bsel);
*/
    // aml_append(parent_scope, aml_name_decl("_ADR", aml_int(0x00060001 /* FIXME! */)));
    // aml_append(parent_scope, aml_name_decl("BSEL", aml_int(bsel_val)));
    notify_method = aml_method("DVNT", 2, AML_NOTSERIALIZED);

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        int slot = PCI_SLOT(devfn);
        int adr = slot << 16 | PCI_FUNC(devfn);
	adr = 0x0;

        if (is_devfn_ignored_hotplug(devfn, bus)) {
            continue;
        }

        if (bus->devices[devfn]) {
            // dev = aml_scope("S%.02X", devfn);
            dev = aml_device("S%.02X", devfn);
            aml_append(dev, aml_name_decl("_ADR", aml_int(adr)));
        } else {
            dev = aml_device("S%.02X", devfn);
            aml_append(dev, aml_name_decl("_ADR", aml_int(adr)));
        }

        /*
         * Can't declare _SUN here for every device as it changes 'slot'
         * enumeration order in linux kernel, so use another variable for it
         */
        aml_append(dev, aml_name_decl("ASUN", aml_int(slot)));
        // aml_append(dev, aml_pci_device_dsm());

        aml_append(dev, aml_name_decl("_SUN", aml_int(slot)));
        /* add _EJ0 to make slot hotpluggable  */
        method = aml_method("_EJ0", 1, AML_NOTSERIALIZED);
        aml_append(method,
           aml_call2("PCEJ", aml_name("BSEL"), aml_name("_SUN"))
           // aml_return(aml_int(0)) // Just added it for testing purposes..
        );
        aml_append(dev, method);

        build_append_pcihp_notify_entry(notify_method, slot);

        /* device descriptor has been composed, add it into parent context */
        aml_append(parent_scope, dev);
    }
    aml_append(parent_scope, notify_method);
}

static bool build_append_notfication_callback(Aml *parent_scope,
                                              const PCIBus *bus)
{
    Aml *method;
    PCIBus *sec;
    QObject *bsel;
    int nr_notifiers = 0;
    GQueue *pcnt_bus_list = g_queue_new();

    QLIST_FOREACH(sec, &bus->child, sibling) {
        Aml *br_scope = aml_scope("S%.02X", sec->parent_dev->devfn);
        if (pci_bus_is_root(sec)) {
            continue;
        }
        nr_notifiers = nr_notifiers +
                       build_append_notfication_callback(br_scope, sec);
        /*
         * add new child scope to parent
         * and keep track of bus that have PCNT,
         * bus list is used later to call children PCNTs from this level PCNT
         */
        if (nr_notifiers) {
            g_queue_push_tail(pcnt_bus_list, sec);
            aml_append(parent_scope, br_scope);
        }
    }

    /*
     * Append PCNT method to notify about events on local and child buses.
     * ps: hostbridge might not have hotplug (bsel) enabled but might have
     * child bridges that do have bsel.
     */
    method = aml_method("PCNT", 0, AML_NOTSERIALIZED);

    /* If bus supports hotplug select it and notify about local events */
    bsel = object_property_get_qobject(OBJECT(bus), ACPI_PCIHP_PROP_BSEL, NULL);
    // bsel = 0;
    if (bsel) {
        uint64_t bsel_val = qnum_get_uint(qobject_to(QNum, bsel));

        aml_append(method, aml_store(aml_int(bsel_val), aml_name("BNUM")));
        aml_append(method, aml_call2("DVNT", aml_name("PCIU"),
                                     aml_int(1))); /* Device Check */
        aml_append(method, aml_call2("DVNT", aml_name("PCID"),
                                     aml_int(3))); /* Eject Request */
        nr_notifiers++;
    }


    /* Notify about child bus events in any case */
    while ((sec = g_queue_pop_head(pcnt_bus_list))) {
        aml_append(method, aml_name("^S%.02X.PCNT", sec->parent_dev->devfn));
    }

    aml_append(parent_scope, method);
    qobject_unref(bsel);
    g_queue_free(pcnt_bus_list);
    return !!nr_notifiers;
}

static void build_append_pci_dsm_func0_common(Aml *ctx, Aml *retvar)
{
    Aml *UUID, *ifctx1;
    uint8_t byte_list[1] = { 0 }; /* nothing supported yet */

    aml_append(ctx, aml_store(aml_buffer(1, byte_list), retvar));
    /*
     * PCI Firmware Specification 3.1
     * 4.6.  _DSM Definitions for PCI
     */
    UUID = aml_touuid("E5C937D0-3553-4D7A-9117-EA4D19C3434D");
    ifctx1 = aml_if(aml_lnot(aml_equal(aml_arg(0), UUID)));
    {
        /* call is for unsupported UUID, bail out */
        aml_append(ifctx1, aml_return(retvar));
    }
    aml_append(ctx, ifctx1);

    ifctx1 = aml_if(aml_lless(aml_arg(1), aml_int(2)));
    {
        /* call is for unsupported REV, bail out */
        aml_append(ifctx1, aml_return(retvar));
    }
    aml_append(ctx, ifctx1);
}

static Aml *aml_pci_pdsm(void)
{
    Aml *method, *ifctx, *ifctx1;
    Aml *ret = aml_local(0);
    Aml *caps = aml_local(1);
    Aml *acpi_index = aml_local(2);
    Aml *zero = aml_int(0);
    Aml *one = aml_int(1);
    Aml *func = aml_arg(2);
    Aml *params = aml_arg(4);
    Aml *bnum = aml_derefof(aml_index(params, aml_int(0)));
    Aml *sunum = aml_derefof(aml_index(params, aml_int(1)));

    method = aml_method("PDSM", 5, AML_SERIALIZED);

    /* get supported functions */
    ifctx = aml_if(aml_equal(func, zero));
    {
        build_append_pci_dsm_func0_common(ifctx, ret);

        aml_append(ifctx, aml_store(zero, caps));
        aml_append(ifctx,
            aml_store(aml_call2("AIDX", bnum, sunum), acpi_index));
        /*
         * advertise function 7 if device has acpi-index
         * acpi_index values:
         *            0: not present (default value)
         *     FFFFFFFF: not supported (old QEMU without PIDX reg)
         *        other: device's acpi-index
         */
        ifctx1 = aml_if(aml_lnot(
                     aml_or(aml_equal(acpi_index, zero),
                            aml_equal(acpi_index, aml_int(0xFFFFFFFF)), NULL)
                 ));
        {
            /* have supported functions */
            aml_append(ifctx1, aml_or(caps, one, caps));
            /* support for function 7 */
            aml_append(ifctx1,
                aml_or(caps, aml_shiftleft(one, aml_int(7)), caps));
        }
        aml_append(ifctx, ifctx1);

        aml_append(ifctx, aml_store(caps, aml_index(ret, zero)));
        aml_append(ifctx, aml_return(ret));
    }
    aml_append(method, ifctx);

    /* handle specific functions requests */
    /*
     * PCI Firmware Specification 3.1
     * 4.6.7. _DSM for Naming a PCI or PCI Express Device Under
     *        Operating Systems
     */
    ifctx = aml_if(aml_equal(func, aml_int(7)));
    {
       Aml *pkg = aml_package(2);

       aml_append(pkg, zero);
       /*
        * optional, if not impl. should return null string
        */
       aml_append(pkg, aml_string("%s", ""));
       aml_append(ifctx, aml_store(pkg, ret));

       aml_append(ifctx, aml_store(aml_call2("AIDX", bnum, sunum), acpi_index));
       /*
        * update acpi-index to actual value
        */
       aml_append(ifctx, aml_store(acpi_index, aml_index(ret, zero)));
       aml_append(ifctx, aml_return(ret));
    }

    aml_append(method, ifctx);
    return method;
}

/* PCI Hot-plug registers bases. See docs/spec/acpi_pci_hotplug.txt */
#define ACPI_PCIHP_SEJ_BASE 0x8
#define ACPI_PCIHP_BNMR_BASE 0x10

static void build_x86_acpi_pci_hotplug(Aml *table, uint64_t pcihp_addr)
{
    Aml *scope;
    Aml *field;
    Aml *method;

    // scope =  aml_scope("_SB.PCI0");
    scope =  aml_scope("PCI0");

    aml_append(scope,
        aml_operation_region("PCST", AML_SYSTEM_IO, aml_int(pcihp_addr), 0x08));
    field = aml_field("PCST", AML_DWORD_ACC, AML_NOLOCK, AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field("PCIU", 32));
    aml_append(field, aml_named_field("PCID", 32));
    aml_append(scope, field);

    aml_append(scope,
        aml_operation_region("SEJ", AML_SYSTEM_IO,
                             aml_int(pcihp_addr + ACPI_PCIHP_SEJ_BASE), 0x04));
    field = aml_field("SEJ", AML_DWORD_ACC, AML_NOLOCK, AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field("B0EJ", 32));
    aml_append(scope, field);

    aml_append(scope,
        aml_operation_region("BNMR", AML_SYSTEM_IO,
                             aml_int(pcihp_addr + ACPI_PCIHP_BNMR_BASE), 0x08));
    field = aml_field("BNMR", AML_DWORD_ACC, AML_NOLOCK, AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field("BNUM", 32));
    aml_append(field, aml_named_field("PIDX", 32));
    aml_append(scope, field);

    aml_append(scope, aml_mutex("BLCK", 0));

    method = aml_method("PCEJ", 2, AML_NOTSERIALIZED);
    aml_append(method, aml_acquire(aml_name("BLCK"), 0xFFFF));
    aml_append(method, aml_store(aml_arg(0), aml_name("BNUM")));
    aml_append(method,
        aml_store(aml_shiftleft(aml_int(1), aml_arg(1)), aml_name("B0EJ")));
    aml_append(method, aml_release(aml_name("BLCK")));
    aml_append(method, aml_return(aml_int(0)));
    aml_append(scope, method);

    method = aml_method("AIDX", 2, AML_NOTSERIALIZED);
    aml_append(method, aml_acquire(aml_name("BLCK"), 0xFFFF));
    aml_append(method, aml_store(aml_arg(0), aml_name("BNUM")));
    aml_append(method,
        aml_store(aml_shiftleft(aml_int(1), aml_arg(1)), aml_name("PIDX")));
    aml_append(method, aml_store(aml_name("PIDX"), aml_local(0)));
    aml_append(method, aml_release(aml_name("BLCK")));
    aml_append(method, aml_return(aml_local(0)));
    aml_append(scope, method);

    aml_append(scope, aml_pci_pdsm());

    aml_append(table, scope);
}

void acpi_dsdt_add_gpex(Aml *table, Aml *scope, struct GPEXConfig *cfg)
{
    int nr_pcie_buses = cfg->ecam.size / PCIE_MMCFG_SIZE_MIN;
    Aml *method, *crs, *dev, *rbuf;
    PCIBus *bus = cfg->bus;
    CrsRangeSet crs_range_set;
    CrsRangeEntry *entry;
    int i;

    /* start to construct the tables for pxb */
    crs_range_set_init(&crs_range_set);
    if (bus) {
        QLIST_FOREACH(bus, &bus->child, sibling) {
            uint8_t bus_num = pci_bus_num(bus);
            uint8_t numa_node = pci_bus_numa_node(bus);
            uint32_t uid;
            bool is_cxl = pci_bus_is_cxl(bus);

            if (!pci_bus_is_root(bus)) {
                continue;
            }

            /*
             * 0 - (nr_pcie_buses - 1) is the bus range for the main
             * host-bridge and it equals the MIN of the
             * busNr defined for pxb-pcie.
             */
            if (bus_num < nr_pcie_buses) {
                nr_pcie_buses = bus_num;
            }

            uid = object_property_get_uint(OBJECT(bus), "acpi_uid",
                                           &error_fatal);
            dev = aml_device("PC%.02X", bus_num);
            if (is_cxl) {
                struct Aml *pkg = aml_package(2);
                aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0016")));
                aml_append(pkg, aml_eisaid("PNP0A08"));
                aml_append(pkg, aml_eisaid("PNP0A03"));
                aml_append(dev, aml_name_decl("_CID", pkg));
            } else {
                aml_append(dev, aml_name_decl("_HID", aml_string("PNP0A08")));
                aml_append(dev, aml_name_decl("_CID", aml_string("PNP0A03")));
            }
            aml_append(dev, aml_name_decl("_BBN", aml_int(bus_num)));
            aml_append(dev, aml_name_decl("_UID", aml_int(uid)));
            aml_append(dev, aml_name_decl("_STR", aml_unicode("pxb Device")));
            aml_append(dev, aml_name_decl("_CCA", aml_int(1)));
            if (numa_node != NUMA_NODE_UNASSIGNED) {
                aml_append(dev, aml_name_decl("_PXM", aml_int(numa_node)));
            }

            acpi_dsdt_add_pci_route_table(dev, cfg->irq, scope, bus_num);

            /*
             * Resources defined for PXBs are composed of the following parts:
             * 1. The resources the pci-brige/pcie-root-port need.
             * 2. The resources the devices behind pxb need.
             */
            crs = build_crs(PCI_HOST_BRIDGE(BUS(bus)->parent), &crs_range_set,
                            cfg->pio.base, 0, 0, 0);
            aml_append(dev, aml_name_decl("_CRS", crs));

            if (is_cxl) {
                build_cxl_osc_method(dev);
            } else {
                acpi_dsdt_add_pci_osc(dev);
            }

            aml_append(scope, dev);
        }
    }

    /* tables for the main */
    dev = aml_device("%s", "PCI0");
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0A08")));
    aml_append(dev, aml_name_decl("_CID", aml_string("PNP0A03")));
    aml_append(dev, aml_name_decl("_SEG", aml_int(0)));
    aml_append(dev, aml_name_decl("_BBN", aml_int(0)));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));
    aml_append(dev, aml_name_decl("_STR", aml_unicode("PCIe 0 Device")));
    aml_append(dev, aml_name_decl("_CCA", aml_int(1)));

    /* OS capability method */
    // Aml *osc = aml_method("_OSC", 4, AML_NOTSERIALIZED);
    // aml_append(osc, aml_return(aml_int(1)));
    // aml_append(dev, osc);

    /* Define a S00 device for notification test in \SB.PCI0 scope */
    // Aml *s00_dev =  aml_device("S%.02X", 0);
    // aml_device("S%.02X", 0);
    // aml_append(dev, s00_dev);

    // Basically, add BSEL prop to the PCI buses
    s.use_acpi_hotplug_bridge = true;
    acpi_pcihp_reset(&s);

    // Object *pci_host = acpi_get_i386_pci_host(); // Hacked to add Arm64 lookup
    PCIHostState *host = PCI_HOST_BRIDGE(object_resolve_path("/machine/gpex", NULL));
    if (host) {
        qemu_log("GPEX host bridge found by ACPI.\n");
    } else {
        exit(100);
    }
    Object *pci_host = OBJECT(host);
        // sb_scope = aml_scope("\\_SB");
        // PCIBus pbus = PCI_HOST_BRIDGE(pci_host)->bus;
        //Object *pci_host = acpi_get_i386_pci_host();
    {
         if (pci_host) {
             PCIBus *pbus = PCI_HOST_BRIDGE(pci_host)->bus;
	     PCIBus *sbus = pbus->child.lh_first;
             // Aml *rp = aml_device("S%.02X", 0x28);
             Aml *rp = aml_device("S%.02X", 0x20);
             aml_append(rp, aml_name_decl("_ADR", aml_int(0x00040000)));
             aml_append(rp, aml_name_decl("BSEL", aml_int(0)));
             /* Scan all PCI buses. Generate tables to support hotplug. */
             // build_append_pci_bus_devices(ascope, pbus);
             // if (object_property_find(OBJECT(pbus->child), ACPI_PCIHP_PROP_BSEL)) {
	     if (object_property_find(OBJECT(sbus), ACPI_PCIHP_PROP_BSEL)) {
                 // build_append_pcihp_slots_(ascope, sbus);
                 build_append_pcihp_slots_(rp, sbus);
             }
             aml_append(dev, rp);
         }
    }

    acpi_dsdt_add_pci_route_table(dev, cfg->irq, scope, 0);

    method = aml_method("_CBA", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_return(aml_int(cfg->ecam.base)));
    aml_append(dev, method);

    /*
     * At this point crs_range_set has all the ranges used by pci
     * busses *other* than PCI0.  These ranges will be excluded from
     * the PCI0._CRS.
     */
    rbuf = aml_resource_template();
    aml_append(rbuf,
        aml_word_bus_number(AML_MIN_FIXED, AML_MAX_FIXED, AML_POS_DECODE,
                            0x0000, 0x0000, nr_pcie_buses - 1, 0x0000,
                            nr_pcie_buses));
    if (cfg->mmio32.size) {
        crs_replace_with_free_ranges(crs_range_set.mem_ranges,
                                     cfg->mmio32.base,
                                     cfg->mmio32.base + cfg->mmio32.size - 1);
        for (i = 0; i < crs_range_set.mem_ranges->len; i++) {
            entry = g_ptr_array_index(crs_range_set.mem_ranges, i);
            aml_append(rbuf,
                aml_dword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                                 AML_NON_CACHEABLE, AML_READ_WRITE, 0x0000,
                                 entry->base, entry->limit,
                                 0x0000, entry->limit - entry->base + 1));
        }
    }
    if (cfg->pio.size) {
        crs_replace_with_free_ranges(crs_range_set.io_ranges,
                                     0x0000,
                                     cfg->pio.size - 1);
        for (i = 0; i < crs_range_set.io_ranges->len; i++) {
            entry = g_ptr_array_index(crs_range_set.io_ranges, i);
            aml_append(rbuf,
                aml_dword_io(AML_MIN_FIXED, AML_MAX_FIXED, AML_POS_DECODE,
                             AML_ENTIRE_RANGE, 0x0000, entry->base,
                             entry->limit, cfg->pio.base,
                             entry->limit - entry->base + 1));
        }
    }
    if (cfg->mmio64.size) {
        crs_replace_with_free_ranges(crs_range_set.mem_64bit_ranges,
                                     cfg->mmio64.base,
                                     cfg->mmio64.base + cfg->mmio64.size - 1);
        for (i = 0; i < crs_range_set.mem_64bit_ranges->len; i++) {
            entry = g_ptr_array_index(crs_range_set.mem_64bit_ranges, i);
            aml_append(rbuf,
                aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                                 AML_NON_CACHEABLE, AML_READ_WRITE, 0x0000,
                                 entry->base,
                                 entry->limit, 0x0000,
                                 entry->limit - entry->base + 1));
        }
    }
    aml_append(dev, aml_name_decl("_CRS", rbuf));

    acpi_dsdt_add_pci_osc(dev);

    Aml *dev_res0 = aml_device("%s", "RES0");
    aml_append(dev_res0, aml_name_decl("_HID", aml_string("PNP0C02")));
    crs = aml_resource_template();
    aml_append(crs,
        aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                         AML_NON_CACHEABLE, AML_READ_WRITE, 0x0000,
                         cfg->ecam.base,
                         cfg->ecam.base + cfg->ecam.size - 1,
                         0x0000,
                         cfg->ecam.size));
    aml_append(dev_res0, aml_name_decl("_CRS", crs));
    aml_append(dev, dev_res0);

    /* Define memory regions related to acpi-pci-hotplug device */
    /* FIXME: probably not the right place this function should be called */
    build_x86_acpi_pci_hotplug(dev, 0x0cc0);

    build_append_notfication_callback(dev, cfg->bus);

    aml_append(scope, dev);


    crs_range_set_free(&crs_range_set);
}

void acpi_dsdt_add_gpex_host(Aml *scope, uint32_t irq)
{
    bool ambig;
    Object *obj = object_resolve_path_type("", TYPE_GPEX_HOST, &ambig);

    if (!obj || ambig) {
        return;
    }

    GPEX_HOST(obj)->gpex_cfg.irq = irq;
    acpi_dsdt_add_gpex(NULL, scope, &GPEX_HOST(obj)->gpex_cfg);
}

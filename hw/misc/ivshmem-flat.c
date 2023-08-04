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
#include "hw/irq.h"
#include "qemu/option.h"
#include "qemu/option_int.h"
#include "qemu/config-file.h"
#include "exec/address-spaces.h"

#include "hw/misc/ivshmem-flat.h"

#define IVSHMEM_DEBUG 0
#define IVSHMEM_DPRINTF(fmt, ...)                       \
    do {                                                \
        if (IVSHMEM_DEBUG) {                            \
            printf("IVSHMEM: " fmt, ## __VA_ARGS__);    \
        }                                               \
    } while (0)

static int64_t ivshmem_flat_recv_msg(IvshmemFTState *s, int *pfd)
{
    int64_t msg;
    int n, ret;

    n = 0;
    do {
        ret = qemu_chr_fe_read_all(&s->server_chr, (uint8_t *)&msg + n,
                                   sizeof(msg) - n);
        if (ret < 0) {
            if (ret == -EINTR) {
                continue;
            }
	    exit(1);
        }
        n += ret;
    } while (n < sizeof(msg));

    *pfd = qemu_chr_fe_get_msgfd(&s->server_chr);
    return le64_to_cpu(msg);
}

static void ivshmem_flat_oops(void *opaque)
{
    VectorInfo *vi = opaque;
    EventNotifier *e = &vi->event_notifier;
    uint16_t vector_id;
    const VectorInfo (*v)[64];

    assert(e->initialized);

    vector_id = vi->id;

    /*
     * The vector info struct is passed to the handler via the 'opaque' pointer.
     * This struct pointer allows the retrieval of the vector ID and its
     * associated event notifier. However, for triggering an interrupt using
     * qemu_set_irq, it's necessary to also have a pointer to the device state,
     * i.e., a pointer to the IvshmemFTState struct. Since the vector info
     * struct is contained within the IvshmemFTState struct, its pointer can be
     * used to obtain the pointer to IvshmemFTState through simple pointer math.
     */
    v = (void *)(vi - vector_id); /* v =  &IvshmemPeer->vector[0] */
    IvshmemPeer *own_peer = container_of(v, IvshmemPeer, vector);
    IvshmemFTState *s = container_of(own_peer, IvshmemFTState, own);

    /* Clear event  */
    if (!event_notifier_test_and_clear(e)) {
        return;
    }

    IVSHMEM_DPRINTF("Interrupt caught, vector %d.\n", vector_id);

#ifdef NO_SPEC_COMPLIANT
    s->doorbell = vector_id << 16;
#endif

    /*
     * Toggle device's output line, which is connected to NVIC, generating an
     * interrupt request to the CPU.
     */
    qemu_set_irq(s->irq, true);
    qemu_set_irq(s->irq, false);
}

IvshmemPeer *ivshmem_flat_find_peer(IvshmemFTState *s, uint16_t peer_id)
{
    IvshmemPeer *peer;

    /* Own ID */
    if (s->own.id == peer_id) {
        return &s->own;
    }

    /* Peer ID */
    QTAILQ_FOREACH(peer, &s->peer, next) {
        if (peer->id == peer_id) {
            return peer;
	}
    }

    return NULL;
}

IvshmemPeer *ivshmem_flat_add_peer(IvshmemFTState *s, uint16_t peer_id)
{
    IvshmemPeer *new_peer;

    new_peer = g_malloc0(sizeof(*new_peer));
    new_peer->id = peer_id;
    new_peer->vector_counter = 0;

    QTAILQ_INSERT_TAIL(&s->peer, new_peer, next);

    IVSHMEM_DPRINTF("New peer: ID %d\n", peer_id);

    return new_peer;
}

static void ivshmem_flat_remove_peer(IvshmemFTState *s, uint16_t peer_id)
{
   IvshmemPeer *peer;

   peer = ivshmem_flat_find_peer(s, peer_id);
   assert(peer);

   QTAILQ_REMOVE(&s->peer, peer, next);
   for (int n = 0; n < peer->vector_counter; n++) {
       int efd;
       efd = event_notifier_get_fd(&(peer->vector[n].event_notifier));
       close(efd);
   }

   g_free(peer);
}

static void ivshmem_flat_add_vector(IvshmemFTState *s, IvshmemPeer *peer, int vector_fd)
{
    bool own = peer == &s->own ? true : false;

    if (peer->vector_counter < IVSHMEM_MAX_VECTOR_NUM) {
        IVSHMEM_DPRINTF("%s ID %d: adding vector %d (fd = %d)\n", own ? "Own" : "Peer", peer->id, peer->vector_counter, vector_fd);
    } else {
        IVSHMEM_DPRINTF("%s ID %d: failed to add vector %d (fd = %d), maximum number of vectors exceeded!\n", own ? "Own" : "Peer", peer->id, peer->vector_counter, vector_fd);
        // TODO(gromero): Does it affect other peers sharing the same eventfd?
        close(vector_fd);

	return;
    }

    /* Set vector ID and its associated event notifier and add it to the peer */
    peer->vector[peer->vector_counter].id = peer->vector_counter;
    g_unix_set_fd_nonblocking(vector_fd, true, NULL);
    event_notifier_init_fd(&peer->vector[peer->vector_counter].event_notifier, vector_fd);

    /*
     * If it's device's own ID, register also the handler for the eventfd so the
     * device can be notified by other peers.
     */
    if (own) {
        qemu_set_fd_handler(vector_fd, ivshmem_flat_oops, NULL, &peer->vector);
    }

    peer->vector_counter++;
}

static void ivshmem_flat_process_msg(IvshmemFTState *s, uint64_t msg, int fd) {
    uint16_t peer_id;
    IvshmemPeer *peer;

    // TODO(gromero): use UINT16_MAX instead of 0xffff
    peer_id = msg & 0xffff;
    peer = ivshmem_flat_find_peer(s, peer_id);

    if (!peer) {
        peer = ivshmem_flat_add_peer(s, peer_id);
    }

    if (fd >= 0) {
        ivshmem_flat_add_vector(s, peer, fd);
    } else { /* fd == -1 */
        ivshmem_flat_remove_peer(s, peer_id);
    }
}

static int ivshmem_flat_can_receive_data(void *opaque)
{
    IvshmemFTState *s = opaque;

    assert(s->msg_buffered_bytes < sizeof(s->msg_buf));
    return sizeof(s->msg_buf) - s->msg_buffered_bytes;
}

static void ivshmem_flat_read_msg(void *opaque, const uint8_t *buf, int size)
{
    IvshmemFTState *s = opaque;
    Error *err = NULL;
    int fd;
    int64_t msg;

    // FIXME(gromero): msg_buffered_bytes is not initialized?
    assert(size >= 0 && s->msg_buffered_bytes + size <= sizeof(s->msg_buf));
    memcpy((unsigned char *)&s->msg_buf + s->msg_buffered_bytes, buf, size);
    s->msg_buffered_bytes += size;
    if (s->msg_buffered_bytes < sizeof(s->msg_buf)) {
        return;
    }
    msg = le64_to_cpu(s->msg_buf);
    s->msg_buffered_bytes = 0;

    fd = qemu_chr_fe_get_msgfd(&s->server_chr);

    ivshmem_flat_process_msg(s, msg, fd);
    if (err) {
        error_report_err(err);
    }
}

static void ivshmem_flat_realize(DeviceState *dev, Error **errp) {
    IvshmemFTState *s = IVSHMEM_FLAT(dev);

/*
    // TODO(gromero): Remove code example below
    MachineState *machine = MACHINE(qdev_get_machine());
    CPUState *cpu = qemu_get_cpu(0);

    IVSHMEM_DPRINTF("%s\n", machine->kernel_filename);
    IVSHMEM_DPRINTF("%p\n", cpu);
    IVSHMEM_DPRINTF("chardev = %s\n", s->server_chr.chr->filename);
*/

/*

Message sequence from server on new connection:

 _____________________________________
|STEP| uint64_t msg  | int fd         |
 -------------------------------------

 0    PROTOCOL        -1              \
 1    OWN ID          -1               |-- Header
 2    -1              shmem fd        /

 3    PEER IDx        Peer's Vector 0 eventfd
 4    PEER IDx        Peer's Vector 1 eventfd
 .                    .
 .                    .
 .                    .
 N    PEER IDy        Peer's Vector 0 eventfd
 N+1  PEER IDy        Peer's Vector 1 eventfd
 .                    .
 .                    .
 .                    .

*/
    int64_t protocol_version, msg;
    int fd, shmem_fd, vector_fd;
    uint16_t peer_id;

    /** 0 step **/
    protocol_version = ivshmem_flat_recv_msg(s, &fd);

    /** 1 step **/
    msg = ivshmem_flat_recv_msg(s, &vector_fd);
    peer_id = 0xFFFF & msg;
    s->own.id = peer_id;
    s->own.vector_counter = 0;

    /** 2 step **/
    msg = ivshmem_flat_recv_msg(s, &shmem_fd);
    // TODO(gromero): Move code below to a separate function.
    if (msg == -1 && shmem_fd >= 0) {
        /*
	 * Map shmem fd into memory region
	 */
        struct stat fdstat;

        if (fstat(shmem_fd, &fdstat) != 0) {
            exit(1);
        }

        IVSHMEM_DPRINTF("Shmem fd total size is %ld byte(s)\n", fdstat.st_size);

        if (fdstat.st_size > s->shmem_maxsize) {
            IVSHMEM_DPRINTF("Can't map shmem fd: requested size exceeds device max size!\n");
        } else {
            IVSHMEM_DPRINTF("Mapping shmem fd at 0x40100000... ");
            memory_region_init_ram_from_fd(&s->shmem, OBJECT(s), "ivshmem-shmem", fdstat.st_size, RAM_SHARED, shmem_fd, 0, NULL);
            memory_region_add_subregion(get_system_memory(), 0x40100000, &s->shmem);
            IVSHMEM_DPRINTF("done!\n");
        }
    }

    // TODO(gromero): Remove prints below
    IVSHMEM_DPRINTF("---------------------------------------------------------\n");
    IVSHMEM_DPRINTF("Protocol version = %lx, Own Peer ID = %d, shmem_fd = %d\n", protocol_version, s->own.id, shmem_fd);
    IVSHMEM_DPRINTF("---------------------------------------------------------\n");

    /*
     * Beyond step 2 ivshmem_process_msg (called by ivshmem_flat_read_msg handler)
     * will handle the additional messages, which will be generated by the
     * server as peers connect or disconnect (TODO) from the server.
     */
    qemu_chr_fe_set_handlers(&s->server_chr, ivshmem_flat_can_receive_data, ivshmem_flat_read_msg, NULL, NULL, s, NULL, true);
}

static uint64_t ivshmem_flat_iomem_read(void *opaque, hwaddr offset, unsigned size)
{
    IvshmemFTState *s = opaque;
    uint32_t ret;

    IVSHMEM_DPRINTF("Read access from offset %ld\n", offset);

    switch (offset)
    {
        case INTMASK:
            ret = 0; /* Ignore read since all bits are reserved in rev 1. */
	    break;
	case INTSTATUS:
	    ret = 0; /* Ignore read since all bits are reserved in rev 1. */
	    break;
        case IVPOSITION:
	    ret = s->own.id;
	    break;
	case DOORBELL:
#ifdef NO_SPEC_COMPLIANT
	    ret = s->doorbell;
#elif
	    IVSHMEM_DPRINTF("DOORBELL register is write-only!\n");
            ret = 0;
#endif
            break;
	default:
	    /* Should never reach out here due to iomem map range. */
	    IVSHMEM_DPRINTF("No ivshmem register mapped at offset %ld\n!", offset);
	    ret = 0;
    }

    return ret;
}

static int ivshmem_flat_interrupt_peer(IvshmemFTState *s, uint16_t peer_id, uint16_t vector_id)
{
    IvshmemPeer *peer;

    peer = ivshmem_flat_find_peer(s, peer_id);
#ifdef NO_SPEC_COMPLIANT
    // TODO(gromero): Inform error in INTSTATUS register if peer == NULL
#endif
    if (!peer) {
        IVSHMEM_DPRINTF("Can't interrupt non-existing peer %d.\n", peer_id);
        return 1;
    }

    event_notifier_set(&(peer->vector[vector_id].event_notifier));

    return 0;
}

static void ivshmem_flat_iomem_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    IvshmemFTState *s = opaque;
    uint16_t vector_id = value >> 16;
    uint16_t peer_id = value & 0xFFFF;

    IVSHMEM_DPRINTF("Write access to offset %ld\n", offset);

    switch (offset) {
    case INTMASK:
        break;
    case INTSTATUS:
        break;
    case IVPOSITION:
        break;
    case DOORBELL:
        IVSHMEM_DPRINTF("Interrupting peer ID %d, vector %d... \n", peer_id, vector_id);
        if (ivshmem_flat_interrupt_peer(s, peer_id, vector_id)) {
            IVSHMEM_DPRINTF("Interruption failed!\n");
        }
        break;
    default:
        /* Should never reach out here due to iomem map range. */
        IVSHMEM_DPRINTF("No ivshmem register mapped at offset %ld\n!", offset);
        break;
    }

    return;
}

static const MemoryRegionOps ivshmem_flat_ops = {
    .read = ivshmem_flat_iomem_read,
    .write = ivshmem_flat_iomem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void ivshmem_flat_instance_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(obj);
    IvshmemFTState *s = IVSHMEM_FLAT(obj);

    struct QemuOptsList *optslist = NULL;
    struct QemuOpts *opts = NULL;
    struct QemuOpt *opt = NULL;

    /* Map 4 MMRs, 32 bits each => 4 * 4 = 16 bytes (0x10) */
    memory_region_init_io(&s->iomem, obj, &ivshmem_flat_ops, s, "ivshmem-mmr", 0x10);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Create 1 output IRQ, which will be connect to the NVIC */
    qdev_init_gpio_out_named(dev, &s->irq, "sysbus-irq", 1);

    /* Get ivshmem chardev option from command line */
    Chardev *chr = qemu_chr_find("ivshmem");
    /*
     * Set "chardev" property ("unix:") in the ivshmem dev. so it can be used to
     * listen to eventfd events.
     */
    qdev_prop_set_chr(dev, "chardev", chr);

    /*
     * Adjust shmem-maxsize property if it is provided by the user
     */
    /* Find '-ivshmem' options */
    optslist = qemu_find_opts("ivshmem");
    /* Find the first options in the options list */
    if ((opts = qemu_opts_find(optslist, NULL))) {
	 /* Find option 'shmem-maxsize=<SIZE>' in the options */
         if ((opt = qemu_opt_find(opts, "shmem-maxsize"))) {
             IVSHMEM_DPRINTF("Setting SHMEM MAXSIZE to %ld\n", opt->value.uint);
             qdev_prop_set_uint32(dev, "shmem-maxsize", opt->value.uint);
         }
    }

    QTAILQ_INIT(&s->peer);
}

static Property ivshmem_flat_props[] = {
    DEFINE_PROP_CHR("chardev", IvshmemFTState, server_chr),
    DEFINE_PROP_UINT32("shmem-maxsize", IvshmemFTState, shmem_maxsize, 256),
    DEFINE_PROP_END_OF_LIST(),
};

static QemuOptsList ivshmem_flat_opts = {
    .name = "ivshmem",
    .implied_opt_name = "shmem-maxsize",
    .head = QTAILQ_HEAD_INITIALIZER(ivshmem_flat_opts.head),
    .merge_lists = false,
    .desc = {
        {
            .name = "shmem-maxsize",
            .type = QEMU_OPT_SIZE,
        },
        { /* end of list */ }
    },
};

static void ivshmem_flat_register_config(void)
{
    qemu_add_opts(&ivshmem_flat_opts);
}

opts_init(ivshmem_flat_register_config);

static void ivshmem_flat_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->hotpluggable = true;
    dc->user_creatable = true;
    dc->realize = ivshmem_flat_realize;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, ivshmem_flat_props);
}

static const TypeInfo ivshmem_flat_info = {
    .name = TYPE_IVSHMEM_FLAT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IvshmemFTState),
    .instance_init = ivshmem_flat_instance_init,
    .class_init = ivshmem_flat_class_init,
};

static void ivshmem_flat_register_types(void)
{
    type_register_static(&ivshmem_flat_info);
}

type_init(ivshmem_flat_register_types);

#ifndef IVSHMEM_FLAT_H
#define IVSHMEM_FLAT_H

#define IVSHMEM_MAX_VECTOR_NUM 64

#define TYPE_IVSHMEM_FLAT "ivshmem-flat"
typedef struct IvshmemFTState IvshmemFTState;

DECLARE_INSTANCE_CHECKER(IvshmemFTState, IVSHMEM_FLAT, TYPE_IVSHMEM_FLAT)

/*
 * This enables the DOORBELL mmr to retain the vector ID that originates an IRQ
 * in the guest, allowing multiple vectors to exist in the guest, similar to the
 * PCI IVSHMEM device.
 */
#define NO_SPEC_COMPLIANT 1

/* ivshmem registers. See ./docs/specs/ivshmem-spec.txt for details. */
enum ivshmem_registers {
    INTMASK = 0,
    INTSTATUS = 4,
    IVPOSITION = 8,
    DOORBELL = 12,
};

typedef struct VectorInfo {
    EventNotifier event_notifier;
    uint16_t id;
} VectorInfo;

typedef struct IvshmemPeer {
    QTAILQ_ENTRY(IvshmemPeer) next;
    VectorInfo vector[IVSHMEM_MAX_VECTOR_NUM];
    int vector_counter;
    uint16_t id;
} IvshmemPeer;

struct IvshmemFTState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    uint64_t msg_buf;
    int msg_buffered_bytes;

    QTAILQ_HEAD(, IvshmemPeer) peer;
    IvshmemPeer own;

    MemoryRegion iomem;
    qemu_irq irq;

    CharBackend server_chr;

    /* MMRs */
    uint32_t intmask;
    uint32_t intstatus;
    uint32_t ivposition;
    uint32_t doorbell;

    /* Shared mem */
    int shmem_fd;
    uint32_t shmem_maxsize;
    MemoryRegion shmem;
};

IvshmemPeer *ivshmem_flat_find_peer(IvshmemFTState *, uint16_t);
IvshmemPeer *ivshmem_flat_add_peer(IvshmemFTState *, uint16_t);

#endif /* IVSHMEM_FLAT_H */

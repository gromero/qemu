#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie_tdisp.h"

#define SOCKET_TRANSPORT_TYPE_PCI_DOE  0x02

#define SOCKET_SPDM_COMMAND_NORMAL       0x0001
#define SOCKET_SPDM_COMMAND_DECAP_TDISP  0x0002

#define PCI_TDISP_MESSAGE_VERSION_10 0x10

#define PCI_TDISP_DEVICE_INTERFACE_STATE      0x05
#define PCI_TDISP_GET_DEVICE_INTERFACE_STATE  0x85
#define PCI_TDISP_ERROR                       0x7F

#define TDISP_INTERFACE_STATE_CONFIG_UNLOCKED 0
#define TDISP_INTERFACE_STATE_CONFIG_LOCKED   1
#define TDISP_INTERFACE_STATE_RUN             2
#define TDISP_INTERFACE_STATE_ERROR           3

#pragma pack(1)

typedef struct {
    uint32_t function_id;
    uint64_t reserved;
} pci_tdisp_interface_id_t;

typedef struct {
    uint8_t version;
    uint8_t message_type;
    uint8_t reserved[2];
    pci_tdisp_interface_id_t interface_id;
} pci_tdisp_header_t;

typedef struct {
    pci_tdisp_header_t header;
} pci_tdisp_get_device_interface_state_request_t;

typedef struct {
    pci_tdisp_header_t header;
    uint8_t tdi_state;
} pci_tdisp_device_interface_state_response_t;

#pragma pack()

static bool write_data(int socket, const void *buffer, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buffer;
    uint32_t sent = 0;

    while (sent < len) {
        ssize_t n = send(socket, p + sent, len - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += (uint32_t)n;
    }
    return true;
}

static bool read_data(int socket, void *buffer, uint32_t len)
{
    uint8_t *p = (uint8_t *)buffer;
    uint32_t received = 0;

    while (received < len) {
        ssize_t n = recv(socket, p + received, len - received, 0);
        if (n <= 0) {
            return false;
        }
        received += (uint32_t)n;
    }
    return true;
}

static bool send_platform_data(int socket, uint32_t command,
                               uint32_t transport_type,
                               const uint8_t *payload, uint32_t payload_size)
{
    uint32_t be;

    be = htonl(command);
    if (!write_data(socket, &be, sizeof(be))) {
        return false;
    }
    be = htonl(transport_type);
    if (!write_data(socket, &be, sizeof(be))) {
        return false;
    }
    be = htonl(payload_size);
    if (!write_data(socket, &be, sizeof(be))) {
        return false;
    }
    if (payload_size == 0) {
        return true;
    }
    return write_data(socket, payload, payload_size);
}

static bool receive_platform_data(int socket, uint32_t *command,
                                  uint32_t *transport_type,
                                  uint8_t *payload, uint32_t *payload_size)
{
    uint32_t be;
    uint32_t size;

    if (!read_data(socket, &be, sizeof(be))) {
        return false;
    }
    *command = ntohl(be);

    if (!read_data(socket, &be, sizeof(be))) {
        return false;
    }
    *transport_type = ntohl(be);

    if (!read_data(socket, &be, sizeof(be))) {
        return false;
    }
    size = ntohl(be);

    if (size > *payload_size) {
        return false;
    }
    if (size > 0 && !read_data(socket, payload, size)) {
        return false;
    }
    *payload_size = size;
    return true;
}

/*
 * Sends a TDISP GET_DEVICE_INTERFACE_STATE request for the given 'function_id'
 * over socket 'socket' using the SOCKET_SPDM_COMMAND_DECAP_TDISP command, and
 * returns the responder's TDI_STATE, i.e., the TDISP state.
 *
 * Returns true on success with '*state' set to one of
 * TDISP_INTERFACE_STATE_{CONFIG_UNLOCKED,CONFIG_LOCKED,RUN,ERROR}.
 *
 * Returns false on transport failure, malformed response, or a
 * PCI_TDISP_ERROR response from the responder.
 */
static bool tdisp_get_device_interface_state(int socket, uint32_t function_id,
                                             uint8_t *state)
{
    pci_tdisp_get_device_interface_state_request_t request;
    uint8_t response_buffer[256];
    uint32_t response_size;
    uint32_t resp_command;
    uint32_t resp_transport_type;
    pci_tdisp_device_interface_state_response_t *response;

    memset(&request, 0, sizeof(request));
    request.header.version = PCI_TDISP_MESSAGE_VERSION_10;
    request.header.message_type = PCI_TDISP_GET_DEVICE_INTERFACE_STATE;
    request.header.interface_id.function_id = function_id;

    if (!send_platform_data(socket, SOCKET_SPDM_COMMAND_DECAP_TDISP,
                            SOCKET_TRANSPORT_TYPE_PCI_DOE,
                            (const uint8_t *)&request, sizeof(request))) {
        return false;
    }

    response_size = sizeof(response_buffer);
    if (!receive_platform_data(socket, &resp_command, &resp_transport_type,
                                response_buffer, &response_size)) {
        return false;
    }

    if (resp_command != SOCKET_SPDM_COMMAND_NORMAL) {
        return false;
    }
    if (response_size < sizeof(pci_tdisp_header_t)) {
        return false;
    }

    response = (pci_tdisp_device_interface_state_response_t *)response_buffer;

    if (response->header.version != PCI_TDISP_MESSAGE_VERSION_10) {
        return false;
    }
    if (response->header.message_type == PCI_TDISP_ERROR) {
        return false;
    }
    if (response->header.message_type != PCI_TDISP_DEVICE_INTERFACE_STATE) {
        return false;
    }
    if (response->header.interface_id.function_id != function_id) {
        return false;
    }
    if (response_size != sizeof(pci_tdisp_device_interface_state_response_t)) {
        return false;
    }

    *state = response->tdi_state;
    return true;
}

uint8_t pcie_tdisp_get_device_interface_state(PCIDevice *dev)
{

    uint32_t function_id = pci_requester_id(dev);
    uint8_t tdisp_state;
    bool r;

    qemu_mutex_lock(&dev->doe_spdm.spdm_lock);
    r = tdisp_get_device_interface_state(dev->doe_spdm.spdm_socket, function_id, &tdisp_state);
    qemu_mutex_unlock(&dev->doe_spdm.spdm_lock);
    if (!r) {
         /* Quirk: Assume CONFIG_UNLOCKED in case of
          * a TDISP_ERROR 0x7F is returned because it
          * might be the case the device is not yet
          * configured hence 'function_id' device is not
          * known yet by TDISP.
          *
          * Maybe that can be improved by querying another
          * TDISP state beforehand, idk.
          */
        return 0; /* CONFIG_UNLOCKED */
    }

    return tdisp_state;
}

static const TypeInfo pcie_tdisp_interface_info = {
    .name = TYPE_PCIE_TDISP_IF,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(PcieTdispIfClass),
};

static void pcie_tdisp_register_types(void)
{
    type_register_static(&pcie_tdisp_interface_info);
}

type_init(pcie_tdisp_register_types);


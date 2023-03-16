/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIO_SHM_XCHG_DRIVER_H
#define VIRTIO_SHM_XCHG_DRIVER_H

#include <h2os/net.hh>
#include "drivers/virtio.hh"
#include "drivers/pci-device.hh"

namespace virtio {

struct virtio_net_ctrl_mq {
    uint8_t cmd_class;
    uint8_t command;
    uint16_t virtqueue_pairs;
    uint8_t ack;
};

struct virtio_net_hdr {
#define VIRTIO_NET_HDR_F_NEEDS_CSUM    1 
#define VIRTIO_NET_HDR_F_DATA_VALID    2 
#define VIRTIO_NET_HDR_F_RSC_INFO      4 
    uint8_t flags; 
#define VIRTIO_NET_HDR_GSO_NONE        0 
#define VIRTIO_NET_HDR_GSO_TCPV4       1 
#define VIRTIO_NET_HDR_GSO_UDP         3 
#define VIRTIO_NET_HDR_GSO_TCPV6       4 
#define VIRTIO_NET_HDR_GSO_ECN      0x80 
    uint8_t gso_type; 
    uint16_t hdr_len; 
    uint16_t gso_size; 
    uint16_t csum_start; 
    uint16_t csum_offset;
    uint16_t num_buffers;
};

struct virtq_buffer {
    struct virtio_net_hdr net_hdr;
    h2os::net::pkt pkt;
} __attribute__((packed));

class shm_xchg : public virtio_driver {
public:
    explicit shm_xchg(virtio_device& dev);
    virtual ~shm_xchg() { };

    static hw_driver *probe(hw_device* dev);
    static shm_xchg *get_instance() { return _instance; }
    std::string get_name() const override { return _driver_name; }
    void get_stats(h2os::net::dev_stats& stats);
    void get_queue_stats(unsigned queue, h2os::net::dev_stats& stats);

    int xmit_pkt(h2os::net::pkt& pkt);

protected:
    u64 get_driver_features() override;

private:
    // A queue represents a rx/tx virtq couple. One queue per vcpu
    class queue {
    public:
        queue(int id, shm_xchg& driver, vring *rx_virtq, vring *tx_virtq);
        ~queue();
        int xmit_pkt(h2os::net::pkt& pkt);
        void get_stats(h2os::net::dev_stats& stats);

    private:
        static const unsigned RX_VIRTQ_SIZE = 256;
        static const unsigned TX_VIRTQ_SIZE = 256;

        int _id;
        shm_xchg& _driver;
        vring *_rx_virtq;
        vring *_tx_virtq;
        // TODO: queue sizes are currently hardcoded to the default value used
        // by QEMU (256) to allow static allocation of buffers. Move to dynamic
        // allocation.
        // For every queue, the virtio device should expose the maximum queue
        // size it can handle, and it should be up to the driver to choose a
        // queue size below that value. However, OSv seems to automatically use
        // the max size.
        struct virtq_buffer _rx_buffers[RX_VIRTQ_SIZE];
        struct virtq_buffer _tx_buffers[TX_VIRTQ_SIZE];
        uint16_t _tx_freelist[TX_VIRTQ_SIZE];
        int _tx_freelist_head;
        // Should make this a unique_ptr but gives me errors
        sched::thread *_poll_task;
        h2os::net::dev_stats _stats;

        void poll_rx();
    };

    static bool _net_configured;
    static shm_xchg *_instance;
    std::string _driver_name;
    uint32_t _net_hdr_size;
    uint32_t _rx_message_size;
    std::vector<queue> _queues;
};

}

#endif
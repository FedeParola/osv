/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/virtio-shm-xchg.hh"

// Control channel is available.
#define VIRTIO_NET_F_CTRL_VQ 17
// Device supports multiqueue with automatic receive steering.
#define VIRTIO_NET_F_MQ 22

// Ctrl virtqueue ack values
#define VIRTIO_NET_OK     0 
#define VIRTIO_NET_ERR    1
#define VIRTIO_NET_CTRL_MQ    4 
#define VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET 0 // (for automatic receive steering) 
#define VIRTIO_NET_CTRL_MQ_RSS_CONFIG   1 // (for configurable receive steering) 
#define VIRTIO_NET_CTRL_MQ_HASH_CONFIG  2 // (for configurable hash calculation) 

namespace virtio {

struct virtio_net_config { 
    uint8_t mac[6];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
    uint16_t mtu;
    uint32_t speed;
    uint8_t duplex;
    uint8_t rss_max_key_size;
    uint16_t rss_max_indirection_table_length;
    uint32_t supported_hash_types;
};

shm_xchg::queue::queue(int id, shm_xchg& driver, vring *rx_virtq,
        vring *tx_virtq):
        _id(id), _driver(driver), _rx_virtq(rx_virtq), _tx_virtq(tx_virtq),
        _tx_freelist_head(-1)
{
    _poll_task = sched::thread::make([this] { this->poll_rx(); },
            sched::thread::attr().name("virtio-shm-xchg-rx")
            .pin(sched::cpus[_id]));
    _poll_task->start();

    // Setup interrupts, we only support virtio over PCI for now
    // The first interrupt for every MSI vector is triggered on vCPU 0, after
    // this, vector affinity is set to the same vCPU of the poll thread.
    // We can avoid this by writing a custom function to register the interrupt
    // (no msi.easy_register()) to immediately set the affinity.
    interrupt_factory int_factory;
    int_factory.register_msi_bindings
            = [this](interrupt_manager &msi) {
                msi.easy_register({
                    {
                        this->_rx_virtq->index(),
                        [&]{ 
                            // kprintf("RX interrupt on queue %d vcpu %d\n", this->_id, sched::cpu::current()->id);
                            this->_rx_virtq->disable_interrupts();
                        },
                        this->_poll_task
                    },
                    {
                        this->_tx_virtq->index(),
                        [&]{ this->_tx_virtq->disable_interrupts(); },
                        nullptr
                    }
            });
    };
    // int_factory.create_pci_interrupt
    //         = [this, _poll_task](pci::device &pci_dev) {
    //     return new pci_interrupt(
    //         pci_dev,
    //         [=]{ return this->ack_irq(); },
    //         [=]{ _poll_task->wake_with_irq_disabled(); });
    // };
    _driver._dev.register_interrupt(int_factory);

    // Fill rx virtqueue
    for (unsigned i = 0; i < RX_VIRTQ_SIZE; i++) {
        _rx_virtq->init_sg();
        _rx_virtq->add_in_sg(&_rx_buffers[i], sizeof(struct virtq_buffer));
        if (!_rx_virtq->add_buf(&_rx_buffers[i])) {
            throw std::runtime_error("shm-xchg: error filling rx virtqueue");
        }
    }

    // Fill tx buffers freelist
    for (unsigned i = 0; i < TX_VIRTQ_SIZE; i++) {
        _tx_freelist[i] = i;
    }
    _tx_freelist_head = TX_VIRTQ_SIZE - 1;
}

shm_xchg::queue::~queue()
{
    // What has to be cleaned?
}

int shm_xchg::queue::xmit_pkt(struct h2os_pkt *pkt)
{
    unsigned long buffer_idx;
    uint32_t virtio_len;

    if (_tx_freelist_head == -1) {
        // Recycle as many tx buffers as possible
        // Can this be done in batch?
        while ((buffer_idx
                = (unsigned long)_tx_virtq->get_buf_elem(&virtio_len) - 1)) {
            _tx_freelist[++_tx_freelist_head] = buffer_idx;
            _tx_virtq->get_buf_finalize();
        }

        if (_tx_freelist_head == -1) {
            // Tx buffers are all busy
            // TODO: what to do?
            return -1;
        }
    }

    buffer_idx = _tx_freelist[_tx_freelist_head--];
    void *buffer = &_tx_buffers[buffer_idx];
    // We don't care about the content of the net_hdr since we are not using any
    // net feature.
    // TODO: consider sharing a single net_hdr memory region for all buffers
    *(struct h2os_pkt *)(buffer + _driver._net_hdr_size) = *pkt;

    _tx_virtq->init_sg();
    _tx_virtq->add_out_sg(buffer,
            _driver._net_hdr_size + sizeof(struct h2os_pkt));
    // Since the cookie is technically a pointer, future calls to get_buf_elem()
    // will return nullptr (0) in case of no used buffers. Offset all
    // indexes by 1 so we can distinguish nullptr from idx 0
    int ret = _tx_virtq->add_buf((void *)(buffer_idx + 1));
    if (!ret) {
        kprintf("shm-xchg: error adding buffer to virtq\n");
        _tx_freelist_head++;
        return -1;
    } else {
        _tx_virtq->kick();
        return 0;
    }
}

void shm_xchg::queue::poll_rx()
{
    while (true) {
        _driver.wait_for_queue(_rx_virtq, &vring::used_ring_not_empty);
        _stats.rx_wakeups++;

        void *buffer;
        uint32_t len;
        while ((buffer = _rx_virtq->get_buf_elem(&len))) {
            _rx_virtq->get_buf_finalize();
            _stats.rx_pkts++;

            // Process the buffer
            if (len != _driver._rx_message_size) {
                throw std::runtime_error(
                        "shm-xchg: received unexpected message");
            }

            if (h2os_handle_pkt(
                    (struct h2os_pkt *)(buffer + _driver._net_hdr_size))) {
                // TODO: handle failing, if pkt could't be handled might
                // need backpressure
                _stats.rx_sockq_full++;
            }

            // Recylce the buffer. Is there an advantage in postponing this
            // operation (i.e., recycle all buffers toghether)?
            _rx_virtq->init_sg();
            _rx_virtq->add_in_sg(buffer, sizeof(struct virtq_buffer));
            if (!_rx_virtq->add_buf(buffer)) {
                throw std::runtime_error(
                        "shm-xchg: error filling rx virtqueue");
            }
        }
        // Notify the device that there are new buffers available.
        // When to do this? By doing it after all buffers have being recylced
        // (as it's done here) we force the device to stop under heavy traffic
        _rx_virtq->kick();
    }
}

int shm_xchg::queue::get_stats(struct h2os_dev_stats *stats)
{
    if (!stats) {
        return -1;
    }

    // I'm copying each field individually beacuse I'm worried a memcpy could
    // copy some field non atomically (it probably doesn't, just to be sure)
    stats->rx_pkts = _stats.rx_pkts;
    stats->rx_sockq_full = _stats.rx_sockq_full;
    stats->rx_wakeups = _stats.rx_wakeups;
    stats->tx_pkts = _stats.tx_pkts;
    stats->tx_errors = _stats.tx_errors;

    return 0;
}

shm_xchg *shm_xchg::_instance = nullptr;
bool shm_xchg::_net_configured = false;

int shm_xchg::xmit_pkt(struct h2os_pkt *pkt)
{
    int ret;

    // Need to pick the queue corresponding to the current core and tx on it.
    // We need a way to make sure this thread isn't rescheduled on a different
    // core while this operation is ongoing, and also to guarantee that tx
    // operations are not interleaved. Disabling preemption should do it
    sched::preempt_disable();
    ret = _queues[sched::cpu::current()->id].xmit_pkt(pkt);
    sched::preempt_enable();

    return ret;
}

u64 shm_xchg::get_driver_features()
{
    auto base = virtio_driver::get_driver_features();
    return (base | (1 << VIRTIO_NET_F_CTRL_VQ) | (1 << VIRTIO_NET_F_MQ));
}

shm_xchg::shm_xchg(virtio_device& dev)
    : virtio_driver(dev)
{
    _driver_name = "virtio-shm-xchg";
    
    // Steps 4, 5 & 6 - negotiate and confirm features
    setup_features();

    // Step 7 - device specific config
    _net_hdr_size = sizeof(struct virtio_net_hdr);
    _rx_message_size = sizeof(struct virtq_buffer);
    if (!_dev.is_modern()) {
        // If the device is legacy and VIRTIO_NET_F_MRG_RXBUF is not negotiated
        // (it never is in our driver), the net header doesn't use the
        // uint16_t num_buffers field. See https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html#x1-2060001
        _net_hdr_size -= sizeof(uint16_t);
        _rx_message_size -= sizeof(uint16_t);
    }

    // Initialize virtqueues
    if (sched::cpus.size() > 1  && (!get_guest_feature_bit(VIRTIO_NET_F_CTRL_VQ)
            || !get_guest_feature_bit(VIRTIO_NET_F_MQ))) {
        throw std::runtime_error("shm-xchg: the VM has multiple vcpus but "
                "multiqueue was not negotiated\n");
    }
    
    struct virtio_net_config cfg;
    virtio_conf_read(0, &cfg, sizeof(cfg));
    if (cfg.max_virtqueue_pairs < sched::cpus.size()) {
        // We need one virtq pair for each vcpu
        throw std::runtime_error(
                "shm-xchg: not enough virtq pairs to handle each vcpu");
    }

    probe_virt_queues();

    // Reserve space for queues in advance. The vecotr can't be relocated since
    // the rx_poll task is based on the address of the queue at initialization
    _queues.reserve(sched::cpus.size());
    for (unsigned i = 0; i < sched::cpus.size(); i++) {
        _queues.emplace_back(i, *this, get_virt_queue(2*i),
                get_virt_queue(2*i + 1));
    }

    if (sched::cpus.size() > 1) {
        // Enable multiple queues through the ctrl virtqueue
        vring *ctrl_virtq = get_virt_queue(2*sched::cpus.size());
        if (!ctrl_virtq) {
            throw std::runtime_error("shm-xchg: error retrieving ctrl virtq");
        }

        // For some reson, cannot get the phys addr (needed to put data on the
        // vring) if variable is allocated on the stack or with operator new
        struct virtio_net_ctrl_mq *cmd
                = (struct virtio_net_ctrl_mq *)calloc(1, sizeof(*cmd));
        if (!cmd) {
            throw std::runtime_error(
                    "shm-xchg: error allocating memory for command");
        }
        cmd->cmd_class = VIRTIO_NET_CTRL_MQ;
        cmd->command = VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET;
        cmd->virtqueue_pairs = (uint16_t)sched::cpus.size();
        ctrl_virtq->init_sg();
        ctrl_virtq->add_out_sg(cmd, sizeof(*cmd) - sizeof(cmd->ack));
        ctrl_virtq->add_in_sg(&cmd->ack, sizeof(cmd->ack));
        if (!ctrl_virtq->add_buf(cmd)) {
            throw std::runtime_error(
                    "shm-xchg: error sending command to device");
        }
        ctrl_virtq->kick();

        uint32_t tmp;
        while (!ctrl_virtq->get_buf_elem(&tmp));

        if (cmd->ack == VIRTIO_NET_ERR) {
            throw std::runtime_error(
                    "shm-xchg: error configuring number of queues on device");
        }

        free(cmd);
    }

    // Step 8
    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

    kprintf("shm-xchg: created device\n");
}

int shm_xchg::get_stats(struct h2os_dev_stats *stats)
{
    if (!stats) {
        return -1;
    }

    struct h2os_dev_stats qstats;
    memset(stats, 0, sizeof(*stats));
    for (auto& q : _queues) {
        q.get_stats(&qstats);
        stats->rx_pkts += qstats.rx_pkts;
        stats->rx_sockq_full += qstats.rx_sockq_full;
        stats->rx_wakeups += qstats.rx_wakeups;
        stats->tx_pkts += qstats.tx_pkts;
        stats->tx_errors += qstats.tx_errors;
    }

    return 0;
}

int shm_xchg::get_queue_stats(unsigned queue, struct h2os_dev_stats *stats)
{
    if (queue >= _queues.size()) {
        return -1;
    }

    return _queues[queue].get_stats(stats);
}

hw_driver *shm_xchg::probe(hw_device* dev)
{
    // TEMPORARY: let the first virtio-net device be handled by the standard
    // virtio-net driver, otherwise compilation doesn't complete
    if (auto virtio_dev = dynamic_cast<virtio_device*>(dev)) {
        if (virtio_dev->get_id()
                == hw_device_id(VIRTIO_VENDOR_ID, VIRTIO_ID_NET)) {
            if (!_net_configured) {
                _net_configured = true;
                return nullptr;
            }
            _instance = new shm_xchg(*virtio_dev);
            return _instance;
        }
    }

    return nullptr;
}

}
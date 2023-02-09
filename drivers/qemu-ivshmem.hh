/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef QEMU_IVSHMEM_H
#define QEMU_IVSHMEM_H

#include "driver.hh"

namespace qemu {

enum {
    IVSHMEM_VENDOR_ID = 0x1af4,
    IVSHMEM_DEVICE_ID = 0x1110,
};

class ivshmem : public hw_driver {
public:
    explicit ivshmem(pci::device& pci_dev);
    ~ivshmem();

    static hw_driver* probe(hw_device* hw_dev);
    std::string get_name() const { return _driver_name; }
    void dump_config() { _pci_dev.dump_config(); }
    volatile void *get_addr() { return _addr; }

private:
    static int _devs_count;
    std::string _driver_name;
    pci::device& _pci_dev;
    int _id;
    volatile void *_addr;
    u64 _size;
};

}

#endif
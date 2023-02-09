/*
 * Copyright (C) 2023 Federico Parola <federico.parola@polito.it>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/qemu-ivshmem.hh"
#include <osv/device.h>

namespace qemu {

int ivshmem::_devs_count = 0;

struct ivshmem_priv {
    ivshmem *driver;
};

static int ivshmem_ioctl(struct device *dev, u_long request, void *arg)
{
    // Any request is fine for now
    struct ivshmem_priv *priv
        = reinterpret_cast<struct ivshmem_priv*>(dev->private_data);
    *(void **)arg = (void *)priv->driver->get_addr();

    return 0;
}

static struct devops ivshmem_devops = {
    no_open,
    no_close,
    no_read,
    no_write,
    ivshmem_ioctl,
    no_devctl,
    no_strategy,
};

static struct driver ivshmem_driver = {
    "qemu_ivshmem",
    &ivshmem_devops,
    sizeof(struct ivshmem_priv),
};

ivshmem::ivshmem(pci::device& pci_dev)
    : hw_driver()
    , _pci_dev(pci_dev)
{
    _driver_name = "ivshmem";

    // In OSv BARs are numbered starting from 1 but the ivshmem spec starts from
    // 0, use the spec numbering internally
    auto bar2 = _pci_dev.get_bar(3);
    if (bar2 == nullptr) {
        throw std::runtime_error("ivshmem: unable to locate BAR2");
    }

    // TODO: check that the parameters of the BAR are correct

    _size = bar2->get_size();
    _addr = mmio_map(bar2->get_addr64(), _size, "ivshmem");

    // Create a device on the devfs
    _id = _devs_count++;
    std::string dev_name = std::string("ivshmem") + std::to_string(_id);
    struct device *dev
        = device_create(&ivshmem_driver, dev_name.c_str(), D_BLK);
    struct ivshmem_priv *priv
        = reinterpret_cast<struct ivshmem_priv*>(dev->private_data);
    priv->driver = this;

    debugf("ivshmem: created device %s, size=%lu\n", dev_name.c_str(), _size);
}

ivshmem::~ivshmem() { }

hw_driver* ivshmem::probe(hw_device* hw_dev)
{
    if (auto pci_dev = dynamic_cast<pci::device*>(hw_dev)) {
        if (pci_dev->get_vendor_id() == IVSHMEM_VENDOR_ID
            && pci_dev->get_device_id() == IVSHMEM_DEVICE_ID) {
            return new ivshmem(*pci_dev);
        }
    }

    return nullptr;
}

}
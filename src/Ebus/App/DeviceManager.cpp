/*
 * Copyright (C) 2026 Roland Jax
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "App/DeviceManager.hpp"

#include <set>

void ebus::DeviceManager::setHandler(ebus::Handler* handler) {
  handler_ = handler;
}

void ebus::DeviceManager::update(const std::vector<uint8_t>& master,
                                 const std::vector<uint8_t>& slave) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Addresses
  masters_[master[0]]++;
  if (ebus::isSlave(master[1])) slaves_[master[1]]++;

  // Devices
  if (handler_ && master[1] == handler_->getTargetAddress()) return;
  if (ebus::isSlave(master[1])) devices_[master[1]].update(master, slave);
}

void ebus::DeviceManager::resetAddresses() {
  std::lock_guard<std::mutex> lock(mutex_);
  masters_.clear();
  slaves_.clear();
}

std::vector<ebus::Device> ebus::DeviceManager::getDevices() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Device> result;
  result.reserve(devices_.size());
  for (const auto& device : devices_) {
    result.push_back(device.second);
  }
  return result;
}

std::map<uint8_t, uint32_t> ebus::DeviceManager::getMasters() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return masters_;
}

std::map<uint8_t, uint32_t> ebus::DeviceManager::getSlaves() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return slaves_;
}

std::set<uint8_t> ebus::DeviceManager::getObservedSlaves() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::set<uint8_t> slaves;

  if (handler_) {
    for (const auto& master : masters_) {
      if (master.first != handler_->getSourceAddress())
        slaves.insert(ebus::slaveOf(master.first));
    }

    for (const auto& slave : slaves_) {
      if (slave.first != handler_->getTargetAddress())
        slaves.insert(slave.first);
    }
  }
  return slaves;
}

const std::vector<std::vector<uint8_t>>
ebus::DeviceManager::vendorScanCommands() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::vector<uint8_t>> result;
  for (const auto& device : devices_) {
    const auto commands = device.second.createVendorScanCommands();
    if (!commands.empty())
      result.insert(result.end(), commands.begin(), commands.end());
  }
  return result;
}

const std::vector<std::vector<uint8_t>> ebus::DeviceManager::createScanCommands(
    const std::vector<std::string>& addresses) const {
  std::set<uint8_t> scanSlaves;
  for (const std::string& address : addresses) {
    const std::vector<uint8_t> bytes = ebus::to_vector(address);
    if (bytes.empty()) continue;
    uint8_t firstByte = bytes[0];
    if (ebus::isSlave(firstByte) &&
        (!handler_ || firstByte != handler_->getTargetAddress()))
      scanSlaves.insert(firstByte);
  }
  std::vector<std::vector<uint8_t>> result;
  for (const uint8_t slave : scanSlaves)
    result.push_back(Device::createScanCommand(slave));
  return result;
}

//
// Created by daviddaeschler on 7/9/26.
//

#include "include/ring_buffer.h"

#include <algorithm>
#include <cstring>
#include <drogon/drogon.h>
#include <fstream>
#include <utility>
#include <algorithm>
#include <cstring>

namespace sgb_api::ring_buffer {

    RingBuffer::RingBuffer(std::filesystem::path parentPath) : _parentPath(std::move(parentPath)) {
    }

    void RingBuffer::openExistingRingBuffer(const std::filesystem::path& ringBufferPath) {
        // validate basics about the file
        const auto sz = std::filesystem::file_size(ringBufferPath);
        if (sz < HEADER_SIZE) {
            throw std::runtime_error("Ring buffer file size is too small. Expected at least "
                + std::to_string( HEADER_SIZE) + " bytes");
        }

        if ((sz - HEADER_SIZE) % (_header.message_size + _header.handle_size) != 0) {
            throw std::runtime_error("Ring buffer file size is not a multiple of entry size");
        }

        // open the file and read the header to validate the signature
        _ringBufferFile.open(ringBufferPath, std::ios::in | std::ios::out | std::ios::binary);
        if (!_ringBufferFile.is_open()) {
            throw std::runtime_error("Failed to open ring buffer file");
        }

        Header testHeader;
        _ringBufferFile.read(reinterpret_cast<char *>(&testHeader), HEADER_SIZE);

        if (strncmp(testHeader.magic, _header.magic, sizeof(_header.magic)) != 0) {
            throw std::runtime_error("Ring buffer file has invalid magic");
        }

        if (testHeader.count > testHeader.max_entries) {
            throw std::runtime_error("Ring buffer header is corrupt: count > max_entries");
        }

        if (testHeader.max_entries == 0) {
            throw std::runtime_error("Ring buffer header is corrupt: max_entries == 0");
        }

        if (testHeader.sequence < testHeader.count) {
            throw std::runtime_error("Ring buffer header is corrupt: sequence < count");
        }

        _header = testHeader;
    }

    void RingBuffer::createNewRingBufferFile(const std::filesystem::path& ringBufferPath)
    {
        // if the file doesn't exist, create it and write the header
        _ringBufferFile.open(ringBufferPath, std::ios::out | std::ios::binary);
        if (!_ringBufferFile.is_open()) {
            throw std::runtime_error("Failed to create ring buffer file");
        }

        _ringBufferFile.write(reinterpret_cast<char *>(&_header), HEADER_SIZE);
        _ringBufferFile.close();

        _ringBufferFile.open(ringBufferPath, std::ios::in | std::ios::out | std::ios::binary);
        if (!_ringBufferFile.is_open()) {
            throw std::runtime_error("Failed to open ring buffer file for reading");
        }
    }

    void RingBuffer::open() {
        std::filesystem::path ringBufferPath = _parentPath / std::filesystem::path("ring_buffer.bin");
        _ringBufferFile.exceptions(std::ios::failbit | std::ios::badbit);

        if (std::filesystem::exists(ringBufferPath)) {
            openExistingRingBuffer(ringBufferPath);
            LOG_INFO << "Opened ring buffer file with version " << _header.version
                << " and sequence number " << _header.sequence;
            return;
        }

        createNewRingBufferFile(ringBufferPath);
        LOG_INFO << "Ring buffer file created with version " << _header.version;
    }

    void RingBuffer::writeNext(const std::string& handle, const std::string& message) {
        const auto nextWritePos =
            HEADER_SIZE +
                ((_header.sequence % _header.max_entries) * (_header.handle_size + _header.message_size));

        Entry entry{};

        std::strncpy(entry.handle, handle.c_str(), HANDLE_MAX_SIZE - 1);
        std::strncpy(entry.message, message.c_str(), MESSAGE_MAX_SIZE - 1);

        _ringBufferFile.seekp(static_cast<std::streamoff>(nextWritePos), std::ios::beg);
        _ringBufferFile.write(reinterpret_cast<char *>(&entry), ENTRY_SIZE);

        _header.sequence++;
        if (_header.count < _header.max_entries) {
            _header.count++;
        }
        _ringBufferFile.seekp(0, std::ios::beg);
        _ringBufferFile.write(reinterpret_cast<char *>(&_header), HEADER_SIZE);
        _ringBufferFile.flush();
    }

    std::vector<Entry> RingBuffer::readPage(std::uint32_t pageNumber) {
        // the page starts at the latest entry and works backwards.
        // this means we may not be able to read a full/contiguous region, so just
        // read the entries one by one.

        // first check boundary conditions
        if (_header.count == 0 && pageNumber == 0) {
            return std::vector<Entry>{};
        }

        if (_header.count == 0 && pageNumber > 0) {
            throw std::runtime_error("Page number out of range");
        }

        // compute the record given by the page number
        const auto pageOffset = pageNumber * PAGE_SIZE;

        if (pageOffset >= _header.count) {
            throw std::runtime_error("Page number out of range");
        }

        // we are either reading an entire page, or hitting the beginning of buffer
        const auto readCount = std::min(PAGE_SIZE, _header.count - pageOffset);

        // move to the front
        const auto startPosition = _header.sequence - pageOffset - readCount;


        std::vector<Entry> entries;
        for (std::uint8_t i = 0; i < readCount; i++) {
            const auto pos = (startPosition+i) % _header.max_entries;

            Entry entry;
            _ringBufferFile.seekg(static_cast<std::streamoff>(HEADER_SIZE + (pos * ENTRY_SIZE)), std::ios::beg);
            _ringBufferFile.read(reinterpret_cast<char *>(&entry), ENTRY_SIZE);
            entries.push_back(entry);
        }

        std::ranges::reverse(entries);
        return entries;
    }

}

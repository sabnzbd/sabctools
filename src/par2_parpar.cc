/*
 * Copyright 2007-2026 The SABnzbd-Team (sabnzbd.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "par2_parpar.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

#include <par2/hasher.h>
#include <par2/processor.h>

#include "gf16/controller_cpu.h"
#include "gf16/gfmat_inv.h"
#include "hasher/hasher.h"

namespace {

// par2's Processor on ParPar's GF16 pipeline
class ParParProcessor : public par2::Processor {
public:
    explicit ParParProcessor(const par2::ProcessorConfig& config) : numthreads(config.numthreads) {}

    ~ParParProcessor() override { proc.deinit(); }

    bool Init(size_t maxlength, uint32_t _outputcount) override {
        outputcount = _outputcount;

        if (!proc.init(maxlength, {{&cpu, 0, maxlength}}))
            return false;
        if (numthreads > 0)
            cpu.setNumThreads((int)numthreads);
        if (!cpu.init(GF16_AUTO))
            return false;

        return proc.setRecoverySlices(outputcount);
    }

    void SetChunkLength(size_t length) override { proc.setCurrentSliceSize(length); }

    void ResetOutput() override { proc.discardOutput(); }

    // ParPar works the coefficients out from the exponents itself
    bool OfferRecoveryExponents(uint32_t, const uint16_t* exponents, uint32_t exponentcount) override {
        return proc.setRecoverySlices(exponentcount, exponents);
    }

    // and solves the erasure with its own matrix inversion
    bool OfferErasures(const std::vector<bool>& present, std::vector<uint16_t>& exponents) override {
        const unsigned validcount = (unsigned)std::count(present.begin(), present.end(), true);

        if (numthreads > 0)
            matrix.setNumThreads((int)numthreads);
        if (!matrix.Compute(present, validcount, exponents))
            return false;

        ownerasures = true;
        return true;
    }

    void WaitForAdd() override { proc.waitForAdd(); }

    std::future<void> AddInput(const void* data, size_t length, uint32_t inputindex, const uint16_t* factors) override {
        if (factors)
            return proc.addInput(data, length, factors);

        if (ownerasures) {
            column.resize(outputcount);
            for (uint32_t output = 0; output < outputcount; output++)
                column[output] = matrix.GetFactor((uint16_t)inputindex, (uint16_t)output);
            return proc.addInput(data, length, column.data());
        }

        return proc.addInput(data, length, (uint16_t)inputindex);
    }

    void EndInput() override { proc.endInput().get(); }

    bool GetOutput(uint32_t index, void* out) override { return proc.getOutput(index, out).get(); }

private:
    uint32_t numthreads;
    uint32_t outputcount = 0;
    bool ownerasures = false;
    PAR2Proc proc;
    PAR2ProcCPU cpu;
    Galois16RecMatrix matrix;
    std::vector<uint16_t> column;
};

// par2's Hasher on ParPar's hashing: the stitched block and file hasher while
// creating, and the single block hasher while checking
class ParParHasher : public par2::Hasher {
public:
    ~ParParHasher() override {
        if (file)
            file->destroy();
        if (first16k)
            first16k->destroy();
    }

    bool Init(uint64_t _filesize, size_t _blocklength, bool _wholefile) override {
        filesize = _filesize;
        blocklength = _blocklength;
        fileoffset = 0;
        wholefile = _wholefile;
        results.clear();

        if (!first16k)
            first16k = HasherInput_Create();
        first16k->reset();

        if (wholefile) {
            if (!file)
                file = HasherInput_Create();
            file->reset();
        }

        return blocklength > 0;
    }

    void SubmitBlocks(const void* data, uint32_t count, size_t filelength) override {
        const uint8_t* at = (const uint8_t*)data;
        size_t remaining = filelength;

        for (uint32_t block = 0; block < count; block++) {
            uint8_t md5crc[20];

            if (wholefile && filelength > 0) {
                const size_t used = std::min(remaining, blocklength);
                file->update(at, used);
                file->getBlock(md5crc, blocklength - used);
                remaining -= used;
            } else {
                BlockHash(at, md5crc);
            }

            results.insert(results.end(), md5crc, md5crc + 20);
            at += blocklength;
        }

        UpdateFirst16k(data, filelength);
    }

    void CollectBlocks(void* out, uint32_t count) override {
        const size_t taken = (size_t)count * 20;
        std::copy(results.begin(), results.begin() + taken, (uint8_t*)out);
        results.erase(results.begin(), results.begin() + taken);
    }

    void CheckBlocks(const void* data, uint32_t count, size_t filelength, const void* expected, char* matched) override {
        const uint8_t* at = (const uint8_t*)data;
        const uint8_t* want = (const uint8_t*)expected;

        for (uint32_t block = 0; block < count; block++) {
            uint8_t md5crc[20];
            BlockHash(at, md5crc);
            matched[block] = memcmp(md5crc, want, 20) == 0 ? 1 : 0;

            at += blocklength;
            want += 20;
        }

        UpdateFirst16k(data, filelength);
    }

    void EndFile(void* hashfull, void* hash16k) override {
        first16k->end(hash16k);

        if (filesize < 16384)
            memcpy(hashfull, hash16k, 16);
        else if (wholefile)
            file->end(hashfull);
    }

private:
    // The MD5 then the CRC32, little endian
    void BlockHash(const uint8_t* at, uint8_t* md5crc) const {
        const uint32_t crc = MD5CRC_Calc(at, blocklength, 0, md5crc);
        md5crc[16] = (uint8_t)crc;
        md5crc[17] = (uint8_t)(crc >> 8);
        md5crc[18] = (uint8_t)(crc >> 16);
        md5crc[19] = (uint8_t)(crc >> 24);
    }

    void UpdateFirst16k(const void* data, size_t filelength) {
        if (fileoffset < 16384)
            first16k->update(data, (size_t)std::min<uint64_t>(filelength, 16384 - fileoffset));
        fileoffset += filelength;
    }

    IHasherInput* file = nullptr;
    IHasherInput* first16k = nullptr;
    uint64_t filesize = 0;
    size_t blocklength = 0;
    uint64_t fileoffset = 0;
    bool wholefile = false;
    std::vector<uint8_t> results;
};

}  // namespace

par2::Backends ParParBackends() {
    static std::once_flag setup;
    std::call_once(setup, [] { setup_hasher(); });

    par2::Backends backends;
    backends.processor = [](const par2::ProcessorConfig& config) {
        return std::unique_ptr<par2::Processor>(new ParParProcessor(config));
    };
    backends.hasher = [](const par2::HasherConfig&) {
        return std::unique_ptr<par2::Hasher>(new ParParHasher());
    };
    return backends;
}

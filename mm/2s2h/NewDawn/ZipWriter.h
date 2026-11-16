// Minimal thread-safe zip (o2r) writer shared by the New Dawn generators (RuntimeMaterials, Mm3dCharacters).
// Entries are deflated in the caller's thread and appended whole; archives over 4 GB or 65534 entries are an error.
#pragma once

#include <cstdint>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <zlib.h>

namespace NewDawn {

inline void PutLe32(uint8_t* p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

class ZipWriter {
  public:
    bool Open(const std::filesystem::path& path) {
        mOut.open(path, std::ios::binary | std::ios::trunc);
        return mOut.is_open();
    }

    // Thread safe. Entries are deflated outside the lock and appended whole.
    void Add(const std::string& name, const uint8_t* data, size_t n) {
        std::vector<uint8_t> packed;
        uint16_t method = 0;
        if (n > 0 && Deflate(data, n, packed) && packed.size() < n) {
            method = 8;
        }
        const uint8_t* body = method ? packed.data() : data;
        const size_t bodySize = method ? packed.size() : n;
        const uint32_t crc = (uint32_t)crc32(0, data, (uInt)n);

        std::lock_guard<std::mutex> lock(mMutex);
        if (mFailed || mPos + bodySize + name.size() + 30 > 0xFFFFFFF0ull || mEntries.size() >= 65534) {
            mFailed =
                true; // would need zip64; the maps are ~1 GB so this is a real error, not a limit to engineer around
            return;
        }
        uint8_t h[30] = {};
        PutLe32(h, 0x04034b50);
        h[4] = 20;
        h[8] = (uint8_t)method;
        h[11] = 0x00;
        h[12] = 0x21; // date 1980-01-01, time 0: reproducible
        PutLe32(h + 14, crc);
        PutLe32(h + 18, (uint32_t)bodySize);
        PutLe32(h + 22, (uint32_t)n);
        h[26] = (uint8_t)name.size();
        h[27] = (uint8_t)(name.size() >> 8);
        mOut.write(reinterpret_cast<const char*>(h), 30);
        mOut.write(name.data(), name.size());
        mOut.write(reinterpret_cast<const char*>(body), bodySize);
        mEntries.push_back({ name, crc, (uint32_t)bodySize, (uint32_t)n, (uint32_t)mPos, method });
        mPos += 30 + name.size() + bodySize;
    }

    bool Close() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mFailed || !mOut.is_open()) {
            return false;
        }
        const uint64_t cdStart = mPos;
        for (const auto& e : mEntries) {
            uint8_t h[46] = {};
            PutLe32(h, 0x02014b50);
            h[4] = 20;
            h[6] = 20;
            h[10] = (uint8_t)e.method;
            h[13] = 0x00;
            h[14] = 0x21;
            PutLe32(h + 16, e.crc);
            PutLe32(h + 20, e.csize);
            PutLe32(h + 24, e.usize);
            h[28] = (uint8_t)e.name.size();
            h[29] = (uint8_t)(e.name.size() >> 8);
            PutLe32(h + 42, e.offset);
            mOut.write(reinterpret_cast<const char*>(h), 46);
            mOut.write(e.name.data(), e.name.size());
            mPos += 46 + e.name.size();
        }
        uint8_t eocd[22] = {};
        PutLe32(eocd, 0x06054b50);
        eocd[8] = eocd[10] = (uint8_t)mEntries.size();
        eocd[9] = eocd[11] = (uint8_t)(mEntries.size() >> 8);
        PutLe32(eocd + 12, (uint32_t)(mPos - cdStart));
        PutLe32(eocd + 16, (uint32_t)cdStart);
        mOut.write(reinterpret_cast<const char*>(eocd), 22);
        mOut.close();
        return !mOut.fail();
    }

    size_t Count() const {
        return mEntries.size();
    }

  private:
    struct Entry {
        std::string name;
        uint32_t crc, csize, usize, offset;
        uint16_t method;
    };

    static bool Deflate(const uint8_t* in, size_t n, std::vector<uint8_t>& out) {
        z_stream s{};
        if (deflateInit2(&s, 3, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            return false;
        }
        out.resize(deflateBound(&s, (uLong)n));
        s.next_in = const_cast<Bytef*>(in);
        s.avail_in = (uInt)n;
        s.next_out = out.data();
        s.avail_out = (uInt)out.size();
        const int rc = deflate(&s, Z_FINISH);
        out.resize(s.total_out);
        deflateEnd(&s);
        return rc == Z_STREAM_END;
    }

    std::ofstream mOut;
    std::mutex mMutex;
    std::vector<Entry> mEntries;
    uint64_t mPos = 0;
    bool mFailed = false;
};

} // namespace NewDawn

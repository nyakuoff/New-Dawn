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

    // One central-directory record (also what a resumable generator journals per entry).
    struct Entry {
        std::string name;
        uint32_t crc, csize, usize, offset;
        uint16_t method;
    };
    // An entry deflated (or stored) and ready to append; made outside any lock.
    struct Packed {
        std::string name;
        std::vector<uint8_t> body;
        uint32_t crc = 0, usize = 0;
        uint16_t method = 0;
    };

    static Packed Pack(const std::string& name, const uint8_t* data, size_t n) {
        Packed p;
        p.name = name;
        std::vector<uint8_t> packed;
        if (n > 0 && Deflate(data, n, packed) && packed.size() < n) {
            p.method = 8;
            p.body = std::move(packed);
        } else {
            p.body.assign(data, data + n);
        }
        p.crc = (uint32_t)crc32(0, data, (uInt)n);
        p.usize = (uint32_t)n;
        return p;
    }

    // Thread safe. Entries are deflated outside the lock and appended whole.
    void Add(const std::string& name, const uint8_t* data, size_t n) {
        Packed p = Pack(name, data, n);
        std::lock_guard<std::mutex> lock(mMutex);
        Append(p, nullptr);
    }

    // Thread safe. Appends the entries as one unit, flushes the stream and returns their records and the archive position after them
    // (the checkpoint a resumable generator journals). False when the archive has failed.
    bool AddBatch(std::vector<Packed>& batch, std::vector<Entry>& records, uint64_t& posAfter) {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& p : batch) {
            Append(p, &records);
        }
        mOut.flush();
        posAfter = mPos;
        return !mFailed && !mOut.fail();
    }

    // Reopens a half-written archive at a journalled checkpoint: the file is cut back to `pos` and `entries` are the records before it.
    bool Resume(const std::filesystem::path& path, uint64_t pos, std::vector<Entry> entries) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || std::filesystem::file_size(path, ec) < pos) {
            return false;
        }
        std::filesystem::resize_file(path, pos, ec);
        if (ec) {
            return false;
        }
        mOut.open(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!mOut.is_open()) {
            return false;
        }
        mOut.seekp(0, std::ios::end);
        mPos = pos;
        mEntries = std::move(entries);
        return true;
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
    // Caller holds mMutex.
    void Append(const Packed& p, std::vector<Entry>* records) {
        if (mFailed || mPos + p.body.size() + p.name.size() + 30 > 0xFFFFFFF0ull || mEntries.size() >= 65534) {
            mFailed =
                true; // would need zip64; the maps are ~1 GB so this is a real error, not a limit to engineer around
            return;
        }
        uint8_t h[30] = {};
        PutLe32(h, 0x04034b50);
        h[4] = 20;
        h[8] = (uint8_t)p.method;
        h[11] = 0x00;
        h[12] = 0x21; // date 1980-01-01, time 0: reproducible
        PutLe32(h + 14, p.crc);
        PutLe32(h + 18, (uint32_t)p.body.size());
        PutLe32(h + 22, p.usize);
        h[26] = (uint8_t)p.name.size();
        h[27] = (uint8_t)(p.name.size() >> 8);
        mOut.write(reinterpret_cast<const char*>(h), 30);
        mOut.write(p.name.data(), p.name.size());
        mOut.write(reinterpret_cast<const char*>(p.body.data()), p.body.size());
        mEntries.push_back({ p.name, p.crc, (uint32_t)p.body.size(), p.usize, (uint32_t)mPos, p.method });
        if (records) {
            records->push_back(mEntries.back());
        }
        mPos += 30 + p.name.size() + p.body.size();
    }

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

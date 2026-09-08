#include "io/StlReader.h"

#include "geom/MeshTopology.h"

#include <QElapsedTimer>
#include <QFile>
#include <QtEndian>

#include <charconv>
#include <cstring>
#include <string_view>
#include <vector>

namespace suspkin {
namespace {

constexpr qint64 kBinaryHeaderSize = 84; ///< 80-byte free-form header + uint32 count
constexpr qint64 kBinaryFacetSize  = 50; ///< 12 floats + uint16 attribute byte count

std::uint32_t readLe32(const char* p)
{
    std::uint32_t v = 0;
    std::memcpy(&v, p, sizeof v);
    return qFromLittleEndian(v);
}

float readLeFloat(const char* p)
{
    // Swap as an integer, then bit-cast: byte-swapping a float directly is not
    // well defined, and STL is always little-endian regardless of host.
    std::uint32_t bits = 0;
    std::memcpy(&bits, p, sizeof bits);
    bits = qFromLittleEndian(bits);
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

bool parseAscii(const char* begin, qint64 size, std::vector<QVector3D>& corners, QString& error)
{
    const char* p = begin;
    const char* const end = begin + size;
    int line = 1;

    const auto skipSpace = [&] {
        while (p < end && static_cast<unsigned char>(*p) <= ' ') {
            if (*p == '\n') ++line;
            ++p;
        }
    };
    const auto nextToken = [&]() -> std::string_view {
        skipSpace();
        const char* const s = p;
        while (p < end && static_cast<unsigned char>(*p) > ' ') ++p;
        return std::string_view(s, static_cast<std::size_t>(p - s));
    };
    const auto readFloat = [&](float& out) -> bool {
        const std::string_view t = nextToken();
        if (t.empty()) return false;
        const auto res = std::from_chars(t.data(), t.data() + t.size(), out);
        return res.ec == std::errc{} && res.ptr == t.data() + t.size();
    };

    // Collecting every "vertex x y z" is enough: it tolerates the formatting and
    // keyword-spacing variation real exporters produce, while still rejecting a
    // file whose vertices do not group into whole triangles.
    for (;;) {
        const std::string_view t = nextToken();
        if (t.empty()) break;
        if (t != "vertex") continue;

        const int vertexLine = line;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!readFloat(x) || !readFloat(y) || !readFloat(z)) {
            error = QStringLiteral("Malformed vertex on line %1.").arg(vertexLine);
            return false;
        }
        corners.emplace_back(x, y, z);
    }

    if (corners.empty()) {
        error = QStringLiteral("No triangles found -- this does not look like an STL file.");
        return false;
    }
    if (corners.size() % 3 != 0) {
        error = QStringLiteral("Found %1 vertices, which is not a whole number of triangles.")
                    .arg(corners.size());
        return false;
    }
    return true;
}

void parseBinary(const char* data, std::uint32_t triCount, std::vector<QVector3D>& corners)
{
    corners.resize(static_cast<std::size_t>(triCount) * 3);
    const char* const base = data + kBinaryHeaderSize;

    for (std::uint32_t i = 0; i < triCount; ++i) {
        // Offsets are computed by hand because the 50-byte facet record is
        // unaligned: a struct mirroring it pads to 52 bytes and would silently
        // misread every facet after the first.
        const char* const f = base + static_cast<std::size_t>(i) * kBinaryFacetSize
                            + 12; // skip the stored facet normal, which we recompute
        for (int v = 0; v < 3; ++v) {
            const char* const q = f + v * 12;
            corners[static_cast<std::size_t>(i) * 3 + v] =
                QVector3D(readLeFloat(q), readLeFloat(q + 4), readLeFloat(q + 8));
        }
    }
}

} // namespace

bool isBinaryStl(const char* data, qint64 size, std::uint32_t* triCountOut)
{
    if (!data || size < kBinaryHeaderSize) return false;

    const std::uint32_t triCount = readLe32(data + 80);

    // Sniffing for a leading "solid" is unreliable in both directions: the
    // 80-byte binary header is free-form and exporters routinely write
    // "solid ..." into it. Comparing the size against the implied payload is the
    // dependable test.
    //
    // Widen before multiplying. An ASCII file's bytes at offset 80 decode to an
    // arbitrary uint32 -- often around 1.8e9 -- and 50x that overflows 32 bits.
    // Computed narrow, the product wraps and can falsely match the file size,
    // which would send a perfectly good text file through the binary parser and
    // turn it into garbage.
    const quint64 expected = static_cast<quint64>(kBinaryHeaderSize)
                           + static_cast<quint64>(kBinaryFacetSize)
                                 * static_cast<quint64>(triCount);
    if (static_cast<quint64>(size) != expected) return false;

    if (triCountOut) *triCountOut = triCount;
    return true;
}

MeshLoadResult readStl(const QString& path)
{
    MeshLoadResult result;
    QElapsedTimer timer;
    timer.start();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Cannot open file: %1").arg(file.errorString());
        return result;
    }

    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        result.error = QStringLiteral("File is empty.");
        return result;
    }

    std::vector<QVector3D> corners;
    std::uint32_t triCount = 0;

    if (isBinaryStl(data.constData(), data.size(), &triCount)) {
        result.wasBinary = true;
        result.formatName = QStringLiteral("binary STL");
        parseBinary(data.constData(), triCount, corners);
    } else {
        result.formatName = QStringLiteral("ASCII STL");
        QString error;
        if (!parseAscii(data.constData(), data.size(), corners, error)) {
            // Neither valid ASCII nor a size-consistent binary. If the binary
            // header claims more than the file holds, saying "truncated" is far
            // more useful than "not an STL".
            if (data.size() >= kBinaryHeaderSize) {
                const std::uint32_t claimed = readLe32(data.constData() + 80);
                const quint64 needed = static_cast<quint64>(kBinaryHeaderSize)
                                     + static_cast<quint64>(kBinaryFacetSize)
                                           * static_cast<quint64>(claimed);
                if (claimed > 0 && needed > static_cast<quint64>(data.size())) {
                    error = QStringLiteral(
                                "Not valid ASCII STL, and as binary the header claims %1 "
                                "triangles (%2 bytes) while the file holds only %3 bytes "
                                "-- it looks truncated.")
                                .arg(claimed)
                                .arg(needed)
                                .arg(data.size());
                }
            }
            result.error = error;
            return result;
        }
    }

    int dropped = 0;
    TriMesh mesh = weldSoup(corners, &dropped);
    if (mesh.isEmpty()) {
        result.error = QStringLiteral("No usable triangles (%1 degenerate skipped).").arg(dropped);
        return result;
    }

    result.skippedDegenerate = dropped;
    result.mesh = std::move(mesh);
    result.elapsedMs = timer.elapsed();
    return result;
}

} // namespace suspkin

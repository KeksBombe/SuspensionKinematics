#include "io/Zip.h"

#include <QCoreApplication>
#include <QtEndian>

#include <zlib.h>

#include <cstring>

namespace suspkin::zip {
namespace {

constexpr quint32 kSigLocal        = 0x04034b50;
constexpr quint32 kSigCentral      = 0x02014b50;
constexpr quint32 kSigEocd         = 0x06054b50;
constexpr quint32 kSigEocd64Locate = 0x07064b50;

constexpr qsizetype kLocalHeaderSize   = 30;
constexpr qsizetype kCentralRecordSize = 46;
constexpr qsizetype kEocdSize          = 22;

/// A ZIP comment is a 16-bit length, so the end record can never start further
/// back than this from the end of the file.
constexpr qsizetype kMaxEocdSearch = 0xFFFF + kEocdSize;

/// Offsets inside a central-directory record, used both to read and to patch.
constexpr qsizetype kCdFlags  = 8;
constexpr qsizetype kCdMethod = 10;
constexpr qsizetype kCdCrc    = 16;
constexpr qsizetype kCdCompSize   = 20;
constexpr qsizetype kCdUncompSize = 24;
constexpr qsizetype kCdNameLen    = 28;
constexpr qsizetype kCdExtraLen   = 30;
constexpr qsizetype kCdCommentLen = 32;
constexpr qsizetype kCdLocalOffset = 42;

/// Bit 3: the sizes live in a trailing data descriptor rather than in the local
/// header. We always know the sizes when we write, so we clear it.
constexpr quint16 kFlagDataDescriptor = 0x0008;
/// Bit 0 is any of the ZIP encryption schemes; none of them are readable here.
constexpr quint16 kFlagEncrypted = 0x0001;
/// Bit 11: the name is UTF-8 rather than the legacy code page.
constexpr quint16 kFlagUtf8Name = 0x0800;

template <typename T>
T readLE(const QByteArray& data, qsizetype offset)
{
    return qFromLittleEndian<T>(data.constData() + offset);
}

void appendLE16(QByteArray& out, quint16 value)
{
    char buffer[2];
    qToLittleEndian(value, buffer);
    out.append(buffer, 2);
}

void appendLE32(QByteArray& out, quint32 value)
{
    char buffer[4];
    qToLittleEndian(value, buffer);
    out.append(buffer, 4);
}

void patchLE16(QByteArray& out, qsizetype offset, quint16 value)
{
    qToLittleEndian(value, out.data() + offset);
}

void patchLE32(QByteArray& out, qsizetype offset, quint32 value)
{
    qToLittleEndian(value, out.data() + offset);
}

QString tr(const char* text) { return QCoreApplication::translate("Zip", text); }

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

quint32 crcOf(const QByteArray& data)
{
    return static_cast<quint32>(
        ::crc32(::crc32(0, nullptr, 0), reinterpret_cast<const Bytef*>(data.constData()),
                static_cast<uInt>(data.size())));
}

/// Inflate a raw DEFLATE stream of known output size. ZIP stores no zlib
/// wrapper, hence the negative window bits.
std::optional<QByteArray> inflateRaw(const char* data, quint32 compressedSize,
                                     quint32 expectedSize, QString* error)
{
    QByteArray out(expectedSize, Qt::Uninitialized);
    if (expectedSize == 0) return out;

    z_stream stream{};
    if (::inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        fail(error, tr("Could not start the decompressor."));
        return std::nullopt;
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
    stream.avail_in = compressedSize;
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = expectedSize;

    const int status = ::inflate(&stream, Z_FINISH);
    const uLong produced = stream.total_out;
    ::inflateEnd(&stream);

    if (status != Z_STREAM_END || produced != expectedSize) {
        fail(error, tr("The compressed data is damaged."));
        return std::nullopt;
    }
    return out;
}

/// Deflate for storage in a ZIP: raw stream, no wrapper. Returns nothing when
/// compression does not pay, leaving the caller to store the bytes as they are.
std::optional<QByteArray> deflateRaw(const QByteArray& data)
{
    z_stream stream{};
    if (::deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                       Z_DEFAULT_STRATEGY)
        != Z_OK) {
        return std::nullopt;
    }
    QByteArray out(static_cast<qsizetype>(::deflateBound(&stream, static_cast<uLong>(data.size()))),
                   Qt::Uninitialized);

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());

    const int status = ::deflate(&stream, Z_FINISH);
    const uLong produced = stream.total_out;
    ::deflateEnd(&stream);

    if (status != Z_STREAM_END) return std::nullopt;
    out.resize(static_cast<qsizetype>(produced));
    if (out.size() >= data.size()) return std::nullopt; // storing it is smaller
    return out;
}

} // namespace

std::optional<Archive> Archive::open(QByteArray bytes, QString* error)
{
    if (bytes.size() < kEocdSize) {
        fail(error, tr("The file is too small to be a ZIP container."));
        return std::nullopt;
    }

    // Scan back for the end-of-central-directory record. It has to be searched
    // for rather than simply read at a fixed offset because it is followed by a
    // variable-length comment.
    const qsizetype searchFrom = std::max(qsizetype(0), bytes.size() - kMaxEocdSearch);
    qsizetype eocd = -1;
    for (qsizetype i = bytes.size() - kEocdSize; i >= searchFrom; --i) {
        if (readLE<quint32>(bytes, i) == kSigEocd) { eocd = i; break; }
    }
    if (eocd < 0) {
        fail(error, tr("This is not a ZIP container -- no end-of-directory record."));
        return std::nullopt;
    }

    const quint16 entryCount = readLE<quint16>(bytes, eocd + 10);
    const quint32 dirSize    = readLE<quint32>(bytes, eocd + 12);
    const quint32 dirOffset  = readLE<quint32>(bytes, eocd + 16);

    // The 0xFFFF/0xFFFFFFFF sentinels mean the real values are in a ZIP64
    // record. Nothing this program writes or reads gets near 4 GB, so say so
    // plainly instead of half-supporting the format.
    if (entryCount == 0xFFFF || dirSize == 0xFFFFFFFF || dirOffset == 0xFFFFFFFF
        || (eocd >= 20 && readLE<quint32>(bytes, eocd - 20) == kSigEocd64Locate)) {
        fail(error, tr("ZIP64 containers are not supported."));
        return std::nullopt;
    }
    if (qsizetype(dirOffset) + qsizetype(dirSize) > bytes.size()) {
        fail(error, tr("The ZIP directory points outside the file."));
        return std::nullopt;
    }

    Archive archive;
    archive.m_entries.reserve(entryCount);

    qsizetype cursor = dirOffset;
    for (int i = 0; i < entryCount; ++i) {
        if (cursor + kCentralRecordSize > bytes.size()
            || readLE<quint32>(bytes, cursor) != kSigCentral) {
            fail(error, tr("The ZIP directory is damaged."));
            return std::nullopt;
        }

        Entry entry;
        entry.flags             = readLE<quint16>(bytes, cursor + kCdFlags);
        entry.method            = readLE<quint16>(bytes, cursor + kCdMethod);
        entry.crc32             = readLE<quint32>(bytes, cursor + kCdCrc);
        entry.compressedSize    = readLE<quint32>(bytes, cursor + kCdCompSize);
        entry.uncompressedSize  = readLE<quint32>(bytes, cursor + kCdUncompSize);
        entry.localHeaderOffset = readLE<quint32>(bytes, cursor + kCdLocalOffset);

        const qsizetype nameLen    = readLE<quint16>(bytes, cursor + kCdNameLen);
        const qsizetype extraLen   = readLE<quint16>(bytes, cursor + kCdExtraLen);
        const qsizetype commentLen = readLE<quint16>(bytes, cursor + kCdCommentLen);
        const qsizetype recordSize = kCentralRecordSize + nameLen + extraLen + commentLen;
        if (cursor + recordSize > bytes.size()) {
            fail(error, tr("The ZIP directory is damaged."));
            return std::nullopt;
        }

        entry.nameBytes = bytes.mid(cursor + kCentralRecordSize, nameLen);
        // Bit 11 promises UTF-8. Without it the name is officially CP437, but
        // every writer that matters emits either ASCII or UTF-8 anyway.
        entry.name = (entry.flags & kFlagUtf8Name) ? QString::fromUtf8(entry.nameBytes)
                                                   : QString::fromLatin1(entry.nameBytes);
        entry.centralRecord = bytes.mid(cursor, recordSize);

        if (entry.flags & kFlagEncrypted) {
            fail(error, tr("\"%1\" is encrypted.").arg(entry.name));
            return std::nullopt;
        }
        if (entry.method != 0 && entry.method != Z_DEFLATED) {
            fail(error, tr("\"%1\" uses compression method %2, which is not supported.")
                            .arg(entry.name).arg(entry.method));
            return std::nullopt;
        }

        // Walk into the local header: only it knows where the payload starts,
        // because its extra field may be a different length from the central one.
        const qsizetype local = entry.localHeaderOffset;
        if (local + kLocalHeaderSize > bytes.size()
            || readLE<quint32>(bytes, local) != kSigLocal) {
            fail(error, tr("\"%1\" has a damaged local header.").arg(entry.name));
            return std::nullopt;
        }
        const qsizetype localNameLen  = readLE<quint16>(bytes, local + 26);
        const qsizetype localExtraLen = readLE<quint16>(bytes, local + 28);
        entry.localExtra = bytes.mid(local + kLocalHeaderSize + localNameLen, localExtraLen);
        const qsizetype data = local + kLocalHeaderSize + localNameLen + localExtraLen;
        if (data + qsizetype(entry.compressedSize) > bytes.size()) {
            fail(error, tr("\"%1\" runs past the end of the file.").arg(entry.name));
            return std::nullopt;
        }
        entry.dataOffset = static_cast<quint32>(data);

        archive.m_entries.push_back(std::move(entry));
        cursor += recordSize;
    }

    archive.m_bytes = std::move(bytes);
    return archive;
}

const Entry* Archive::find(const QString& name) const
{
    for (const Entry& entry : m_entries)
        if (entry.name == name) return &entry;
    return nullptr;
}

std::optional<QByteArray> Archive::extract(const Entry& entry, QString* error) const
{
    const char* data = m_bytes.constData() + entry.dataOffset;

    std::optional<QByteArray> content;
    if (entry.method == 0) {
        if (entry.compressedSize != entry.uncompressedSize) {
            fail(error, tr("\"%1\" is stored but its two sizes disagree.").arg(entry.name));
            return std::nullopt;
        }
        content = QByteArray(data, entry.compressedSize);
    } else {
        content = inflateRaw(data, entry.compressedSize, entry.uncompressedSize, error);
        if (!content) {
            if (error) *error = tr("Could not read \"%1\": %2").arg(entry.name, *error);
            return std::nullopt;
        }
    }

    if (crcOf(*content) != entry.crc32) {
        fail(error, tr("\"%1\" failed its checksum -- the file is corrupt.").arg(entry.name));
        return std::nullopt;
    }
    return content;
}

std::optional<QByteArray> Archive::extract(const QString& name, QString* error) const
{
    if (const Entry* entry = find(name)) return extract(*entry, error);
    fail(error, tr("The archive has no member named \"%1\".").arg(name));
    return std::nullopt;
}

QByteArray rebuild(const Archive& archive, const QHash<QString, QByteArray>& replacements,
                   QString* error)
{
    for (auto it = replacements.begin(); it != replacements.end(); ++it) {
        if (!archive.find(it.key())) {
            fail(error, tr("Cannot replace \"%1\": the archive has no such member.").arg(it.key()));
            return {};
        }
    }

    QByteArray out;
    out.reserve(archive.bytes().size() + 4096);

    std::vector<QByteArray> directory;
    directory.reserve(archive.entries().size());

    for (const Entry& entry : archive.entries()) {
        const auto replacement = replacements.find(entry.name);
        const bool replaced = replacement != replacements.end();
        const quint32 offset = static_cast<quint32>(out.size());

        QByteArray payload;
        QByteArray extra;
        quint16 method = entry.method;
        quint32 crc = entry.crc32;
        quint32 uncompressedSize = entry.uncompressedSize;

        if (replaced) {
            const QByteArray& content = *replacement;
            uncompressedSize = static_cast<quint32>(content.size());
            crc = crcOf(content);
            if (std::optional<QByteArray> deflated = deflateRaw(content)) {
                payload = std::move(*deflated);
                method = Z_DEFLATED;
            } else {
                payload = content;
                method = 0;
            }
            // The old extra field described the old bytes, so it is dropped
            // rather than carried over onto contents it no longer matches.
        } else {
            payload = QByteArray(archive.bytes().constData() + entry.dataOffset,
                                 entry.compressedSize);
            extra = entry.localExtra;
        }
        const quint32 compressedSize = static_cast<quint32>(payload.size());
        const quint16 flags = entry.flags & ~kFlagDataDescriptor;

        appendLE32(out, kSigLocal);
        appendLE16(out, 20); // version needed: 2.0, which is what deflate requires
        appendLE16(out, flags);
        appendLE16(out, method);
        // Modification time and date come straight from the original record, so
        // an untouched member keeps the timestamp its author gave it.
        out.append(entry.centralRecord.mid(12, 4));
        appendLE32(out, crc);
        appendLE32(out, compressedSize);
        appendLE32(out, uncompressedSize);
        appendLE16(out, static_cast<quint16>(entry.nameBytes.size()));
        appendLE16(out, static_cast<quint16>(extra.size()));
        out.append(entry.nameBytes);
        out.append(extra);
        out.append(payload);

        QByteArray record = entry.centralRecord;
        patchLE16(record, kCdFlags, flags);
        patchLE16(record, kCdMethod, method);
        patchLE32(record, kCdCrc, crc);
        patchLE32(record, kCdCompSize, compressedSize);
        patchLE32(record, kCdUncompSize, uncompressedSize);
        patchLE32(record, kCdLocalOffset, offset);
        directory.push_back(std::move(record));
    }

    const quint32 directoryOffset = static_cast<quint32>(out.size());
    for (const QByteArray& record : directory) out.append(record);
    const quint32 directorySize = static_cast<quint32>(out.size()) - directoryOffset;

    appendLE32(out, kSigEocd);
    appendLE16(out, 0); // this disk
    appendLE16(out, 0); // disk holding the directory
    appendLE16(out, static_cast<quint16>(directory.size()));
    appendLE16(out, static_cast<quint16>(directory.size()));
    appendLE32(out, directorySize);
    appendLE32(out, directoryOffset);
    appendLE16(out, 0); // no archive comment

    return out;
}

} // namespace suspkin::zip

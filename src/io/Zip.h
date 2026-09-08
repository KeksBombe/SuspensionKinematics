#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include <optional>
#include <vector>

namespace suspkin::zip {

/// One member of an archive, as described by its central-directory record.
///
/// The central directory is the authority here, not the local headers: a local
/// header is allowed to carry zero sizes and defer them to a trailing data
/// descriptor, while the central record always has the real values.
struct Entry {
    QString name;
    QByteArray nameBytes;            ///< the name exactly as stored, re-emitted unchanged
    quint16 flags = 0;
    quint16 method = 0;              ///< 0 stored, 8 deflated
    quint32 crc32 = 0;
    quint32 compressedSize = 0;
    quint32 uncompressedSize = 0;
    quint32 localHeaderOffset = 0;
    QByteArray centralRecord;        ///< the whole record, re-emitted verbatim on rebuild
    QByteArray localExtra;           ///< local header's extra field, which may differ from the central one
    quint32 dataOffset = 0;          ///< first byte of the compressed payload
};

/// A ZIP file held in memory, opened read-only.
class Archive {
public:
    /// Parse @p bytes. Returns nothing with @p error set if this is not a ZIP
    /// this reader can handle.
    static std::optional<Archive> open(QByteArray bytes, QString* error = nullptr);

    const std::vector<Entry>& entries() const { return m_entries; }
    const Entry* find(const QString& name) const;

    /// Decompress one member. @p error is set on a CRC mismatch too, because a
    /// spreadsheet that silently decodes to garbage is worse than one that fails.
    std::optional<QByteArray> extract(const Entry& entry, QString* error = nullptr) const;
    std::optional<QByteArray> extract(const QString& name, QString* error = nullptr) const;

    const QByteArray& bytes() const { return m_bytes; }

private:
    QByteArray m_bytes;
    std::vector<Entry> m_entries;
};

/// Rebuild @p archive with the named members replaced by new contents.
///
/// Members that are not replaced keep their original compressed bytes -- nothing
/// is decoded and re-encoded -- and their central-directory records are copied
/// through, so styles, themes, custom XML parts and anything else the original
/// writer put in the file survive exactly. That is what makes it safe to write
/// back over a workbook somebody else authored.
QByteArray rebuild(const Archive& archive, const QHash<QString, QByteArray>& replacements,
                   QString* error = nullptr);

} // namespace suspkin::zip

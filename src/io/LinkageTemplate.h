#pragma once

#include "model/Linkage.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace suspkin {

/// Outcome of reading a template. Like the other readers, a failure comes back
/// as a message rather than an exception, so the caller can put it straight in
/// front of the user.
struct LinkageTemplateLoadResult {
    std::optional<LinkageTemplate> templ;
    QStringList warnings; ///< parts that were skipped, and why
    QString error;        ///< empty on success

    bool ok() const { return templ.has_value(); }
};

/// Read a template from @p bytes. @p label names the source in any message.
LinkageTemplateLoadResult readLinkageTemplate(const QByteArray& bytes, const QString& label);
LinkageTemplateLoadResult readLinkageTemplateFile(const QString& path);

/// Serialise @p templ back to the file format, indented the way the shipped
/// template is: this file is meant to be opened and edited by hand.
QByteArray writeLinkageTemplate(const LinkageTemplate& templ);

/// The template the application ships: a pushrod-actuated double wishbone corner.
/// It is what a project gets when it does not have one of its own yet.
QByteArray builtinLinkageTemplateBytes();
LinkageTemplate builtinLinkageTemplate();

/// The file name a project stores its template under, relative to the project.
QString linkageTemplateRelativePath();
/// Name filter for the template file dialog.
QString linkageTemplateFileFilter();

} // namespace suspkin

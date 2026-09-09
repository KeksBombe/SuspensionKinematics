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

/// @p bytes with each corner's steering role set from @p corners -- matched by
/// token -- and **nothing else touched**.
///
/// A patch rather than a re-serialisation, for the same reason
/// `writeHardpointsXlsx()` splices a workbook instead of rewriting it: the
/// template is the user's file. Notes they added, parts they wrote and keys this
/// version knows nothing about all survive. A corner written as a bare string
/// becomes an object when it gains a steering role, and keeps its token as its
/// label.
///
/// Returns empty and sets @p error when @p bytes is not readable JSON.
QByteArray setTemplateSteering(const QByteArray& bytes, const std::vector<CornerSpec>& corners,
                               QString* error);

/// The roles the built-in template gives a corner's hardpoints.
///
/// What a template written before the solver existed falls back to, so that a
/// project made last month simulates without anybody hand-editing a file.
MechanismTemplate builtinMechanismTemplate();

/// The template the application ships: a pushrod-actuated double wishbone corner.
/// It is what a project gets when it does not have one of its own yet.
QByteArray builtinLinkageTemplateBytes();
LinkageTemplate builtinLinkageTemplate();

/// The file name a project stores its template under, relative to the project.
QString linkageTemplateRelativePath();
/// Name filter for the template file dialog.
QString linkageTemplateFileFilter();

} // namespace suspkin

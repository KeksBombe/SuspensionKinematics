#pragma once

#include "app/session/SessionMessage.h"
#include "io/LinkageTemplate.h"
#include "model/Linkage.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace suspkin {

class ProjectSession;
struct HardpointTable;

/// What is drawn between the hardpoints: the project's linkage template, what to
/// say about its steering, and the parts it resolves to against the table.
///
/// The template is the project's copy, not the built-in one, unless the project
/// had none and was given one. It is patched rather than rewritten, the way a
/// workbook is: the file is the user's.
class LinkageDocument {
public:
    /// Turns the template's bytes into new bytes, or into nothing with @p error
    /// said.
    using Patch = std::function<QByteArray(const QByteArray& bytes, QString* error)>;

    explicit LinkageDocument(ProjectSession& session);

    const LinkageTemplate& linkageTemplate() const { return m_template; }
    /// The parts the template resolved to, as indices into the table.
    const Linkage& parts() const { return m_parts; }
    /// What to say about steering in the status line: that it was filled in, or
    /// that the template says nothing and every axle is therefore steerable.
    const QString& steeringNote() const { return m_steeringNote; }

    /// Read the project's template, writing the built-in one in first if it has
    /// none, and resolve everything against it again. False, with @p problem
    /// said, when there is no template that can be read -- which is not
    /// repaired by overwriting it.
    bool load(SessionMessage* problem);
    /// Write the built-in template into the project and point the project at it.
    bool installBuiltin(SessionMessage* problem);
    /// Patch the project's template with @p patch and read it back. @p failure
    /// leads the message when the patch cannot be written.
    bool patch(const Patch& patch, const QString& failure, SessionMessage* problem);
    /// Read the template at @p path and copy it into the project in place of
    /// the one there. What the reader skipped, or nothing when it was not read.
    std::optional<QStringList> import(const QString& path, SessionMessage* problem);

    /// Resolve the template against @p table into parts.
    void resolve(const HardpointTable& table);

private:
    /// Give a template that predates the steering role an answer to "which axle
    /// has a rack", when the file is recognisably the built-in one. Sets the
    /// steering note either way; writes nothing to somebody's own template.
    void adoptTemplateSteering();
    /// The built-in answer for every corner, or nothing when the template is not
    /// recognisably the built-in one.
    std::optional<std::vector<CornerSpec>> builtinSteering() const;
    /// @p patch applied to the file at @p relative, written back. Read through
    /// Project::readFile(), which has closed the file again before the write
    /// replaces it: Windows will not replace a file this process has open.
    bool patchFile(const QString& relative, const Patch& patch, QString* error) const;

    ProjectSession& m_session;
    LinkageTemplate m_template;
    QString m_steeringNote;
    Linkage m_parts;
};

} // namespace suspkin

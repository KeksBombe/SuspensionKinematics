#include "app/LicensesDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QTextCursor>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace suspkin {
namespace {

struct Entry {
    const char* title;    ///< what the list shows
    const char* resource; ///< the text itself, compiled in
};

// The order is the order of the dialog: what this program is first, then the
// overview of everything else, then one entry per component. A component with
// two texts (Open CASCADE carries an exception to its licence) gets two lines,
// because splicing them into one file would misquote both.
constexpr Entry kEntries[] = {
    { QT_TRANSLATE_NOOP("suspkin::LicensesDialog", "SuspensionKinematics -- GPL v3 or later"),
      ":/licenses/LICENSE" },
    { QT_TRANSLATE_NOOP("suspkin::LicensesDialog", "Third-party software: the whole list"),
      ":/licenses/THIRD-PARTY-NOTICES.md" },
    { QT_TRANSLATE_NOOP("suspkin::LicensesDialog", "Qt 6 -- LGPL v3"),
      ":/licenses/LGPL-3.0.txt" },
    { QT_TRANSLATE_NOOP("suspkin::LicensesDialog", "Qt 6: third-party code inside it"),
      ":/licenses/qt-third-party.txt" },
    { QT_TRANSLATE_NOOP("suspkin::LicensesDialog", "Open CASCADE -- LGPL v2.1"),
      ":/licenses/LGPL-2.1.txt" },
    { QT_TRANSLATE_NOOP("suspkin::LicensesDialog", "Open CASCADE -- the exception to it"),
      ":/licenses/OCCT-exception.txt" },
    { QT_TRANSLATE_NOOP("suspkin::LicensesDialog", "Tabler Icons -- MIT"),
      ":/licenses/LICENSE-tabler.txt" },
    { QT_TRANSLATE_NOOP("suspkin::LicensesDialog", "zlib"), ":/licenses/zlib.txt" },
    { QT_TRANSLATE_NOOP("suspkin::LicensesDialog", "Mesa llvmpipe and LLVM -- MIT and NCSA"),
      ":/licenses/mesa-llvmpipe.txt" },
};

QString readResource(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

} // namespace

LicensesDialog::LicensesDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Licenses"));

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("SuspensionKinematics is free software, and it is built out of other people's "
           "free software. Every licence it has to pass on is here, in the program itself, "
           "so that a copy of it is a complete copy."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_list = new QListWidget(this);
    m_text = new QTextBrowser(this);
    m_text->setOpenExternalLinks(true);

    for (const Entry& entry : kEntries) {
        // Nothing is listed that cannot be shown: see the class comment.
        if (readResource(QString::fromLatin1(entry.resource)).isEmpty()) continue;
        auto* item = new QListWidgetItem(tr(entry.title), m_list);
        item->setData(Qt::UserRole, QString::fromLatin1(entry.resource));
    }

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_list);
    splitter->addWidget(m_text);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setChildrenCollapsible(false);
    // The list needs room for its longest entry and no more; the rest goes to
    // the text, which is what has to fit 80 columns without re-wrapping.
    splitter->setSizes({ 260, 780 });
    layout->addWidget(splitter, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, &LicensesDialog::showEntry);
    if (m_list->count() > 0) m_list->setCurrentRow(0);

    // Wide enough for the 80-column licence texts not to be re-wrapped, which
    // is what turns a paragraph of the GPL into a ladder.
    resize(1040, 680);
}

void LicensesDialog::showEntry(int row)
{
    QListWidgetItem* item = m_list->item(row);
    if (item == nullptr) return;

    const QString path = item->data(Qt::UserRole).toString();
    const QString text = readResource(path);

    // The overview is Markdown and reads as a document; a licence is a legal
    // text and is shown exactly as it was written, in a fixed pitch, because
    // its own line breaks and indentation are part of it.
    if (path.endsWith(QStringLiteral(".md"))) {
        m_text->setFont(font());
        m_text->setMarkdown(text);
    } else {
        // The fixed font at the dialog's own size, not at whatever size the
        // desktop's fixed font carries -- that one is routinely much larger,
        // and a licence set in it wraps every line into three.
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSizeF(font().pointSizeF());
        m_text->setFont(mono);
        m_text->setPlainText(text);
    }
    m_text->moveCursor(QTextCursor::Start);
}

} // namespace suspkin

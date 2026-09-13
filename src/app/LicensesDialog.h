#pragma once

#include <QDialog>

class QListWidget;
class QTextBrowser;

namespace suspkin {

/// What this program is made of and under what terms -- Help > Licenses.
///
/// The texts are **compiled into the binary** (`:/licenses`), not installed
/// beside it, because the obligation is to supply them with *every copy*: the
/// LGPL asks it of Qt and of Open CASCADE, the MIT licence asks it of the
/// icons. A portable unzip that someone copies onto a memory stick carries one
/// file, and the notices have to be in it. They are installed as files as well,
/// which is what the Arch package and the Windows tree want, but nothing
/// depends on those being present.
///
/// The list is fixed at build time and each entry is one resource. An entry
/// whose resource is missing -- a build without the icons, say -- is left out
/// rather than shown empty, so what the dialog lists is always what the binary
/// actually contains.
class LicensesDialog : public QDialog {
    Q_OBJECT

public:
    explicit LicensesDialog(QWidget* parent = nullptr);

private:
    void showEntry(int row);

    QListWidget* m_list = nullptr;
    QTextBrowser* m_text = nullptr;
};

} // namespace suspkin

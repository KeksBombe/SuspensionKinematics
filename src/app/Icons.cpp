#include "app/Icons.h"

#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPixmapCache>
#include <QSvgRenderer>

#include <algorithm>
#include <utility>

namespace suspkin {
namespace {

struct IconEntry {
    Icon icon;
    const char* file; ///< the Tabler name, which is also the file under :/icons
};

// The one table. A new icon is fetched with tools/fetch_icons.py, listed in
// SUSPKIN_ICONS in CMakeLists.txt, and given a line here; test_icons fails
// until all three agree.
constexpr IconEntry kIcons[] = {
    { Icon::AdjustmentsHorizontal, "adjustments-horizontal" },
    { Icon::Angle, "angle" },
    { Icon::Braces, "braces" },
    { Icon::ChartLine, "chart-line" },
    { Icon::ChevronDown, "chevron-down" },
    { Icon::ChevronUp, "chevron-up" },
    { Icon::CloudDownload, "cloud-download" },
    { Icon::Cube, "cube" },
    { Icon::DeviceFloppy, "device-floppy" },
    { Icon::Edit, "edit" },
    { Icon::FileExport, "file-export" },
    { Icon::FileImport, "file-import" },
    { Icon::FileSpreadsheet, "file-spreadsheet" },
    { Icon::FileTypeCsv, "file-type-csv" },
    { Icon::FileX, "file-x" },
    { Icon::FlipHorizontal, "flip-horizontal" },
    { Icon::FocusCentered, "focus-centered" },
    { Icon::FolderOpen, "folder-open" },
    { Icon::FolderPlus, "folder-plus" },
    { Icon::FolderSearch, "folder-search" },
    { Icon::Forms, "forms" },
    { Icon::History, "history" },
    { Icon::InfoCircle, "info-circle" },
    { Icon::InfoSquareRounded, "info-square-rounded" },
    { Icon::LayoutDashboard, "layout-dashboard" },
    { Icon::License, "license" },
    { Icon::Line, "line" },
    { Icon::ListDetails, "list-details" },
    { Icon::Logout, "logout" },
    { Icon::Refresh, "refresh" },
    { Icon::Restore, "restore" },
    { Icon::RowInsertBottom, "row-insert-bottom" },
    { Icon::RowRemove, "row-remove" },
    { Icon::Sphere, "sphere" },
    { Icon::SteeringWheel, "steering-wheel" },
    { Icon::Table, "table" },
    { Icon::TableExport, "table-export" },
    { Icon::TableMinus, "table-minus" },
    { Icon::TablePlus, "table-plus" },
    { Icon::Tag, "tag" },
    { Icon::Template, "template" },
    { Icon::Trash, "trash" },
    { Icon::Triangles, "triangles" },
    { Icon::Vector, "vector" },
    { Icon::Wand, "wand" },
    { Icon::Wheel, "wheel" },
};

const char* fileFor(Icon icon)
{
    const auto match = std::find_if(std::begin(kIcons), std::end(kIcons),
                                    [icon](const IconEntry& entry) { return entry.icon == icon; });
    return match == std::end(kIcons) ? nullptr : match->file;
}

/// From this size up an icon is drawn with a lighter stroke. Tabler's 2 units on
/// a 24-unit grid is 1.3 px at 16 px and 2.7 px at 32, which puts a large button
/// visibly heavier than the small ones beside it; 1.5 units brings it to 2 px.
constexpr int kThinStrokeFrom = 28;

QColor mix(const QColor& a, const QColor& b, float t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

/// The colour an icon in @p mode is drawn in: the colour a button's text would
/// be, so an icon and its label always match.
QColor colourFor(const QPalette& palette, QIcon::Mode mode)
{
    const QColor normal = palette.color(QPalette::Active, QPalette::ButtonText);
    switch (mode) {
    case QIcon::Disabled: {
        const QColor disabled = palette.color(QPalette::Disabled, QPalette::ButtonText);
        // A palette that does not grey out its disabled text still has to show
        // a disabled button as one, so it is faded toward the button instead.
        if (disabled.rgba() != normal.rgba()) return disabled;
        return mix(normal, palette.color(QPalette::Active, QPalette::Button), 0.55f);
    }
    case QIcon::Selected:
        return palette.color(QPalette::Active, QPalette::HighlightedText);
    case QIcon::Normal:
    case QIcon::Active:
        break;
    }
    return normal;
}

/// Draws one Tabler SVG in the palette's colours.
///
/// The SVGs draw with `stroke="currentColor"`, which QtSvg resolves to black
/// when nothing sets a colour -- so a plain QIcon of one is black on a dark
/// desktop. This writes the palette's colour into the bytes before rendering
/// instead, per mode, and renders through QSvgRenderer directly so nothing
/// depends on the qsvgicon plugin being deployed.
///
/// Rendered pixmaps go into QPixmapCache under a key that includes the
/// palette's cacheKey(), so a change of theme simply misses the cache.
class ThemedIconEngine final : public QIconEngine {
public:
    ThemedIconEngine(QString name, QByteArray svg) : m_name(std::move(name)), m_svg(std::move(svg))
    {
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        // Rendered for the device being painted on, not at 1x and stretched.
        const qreal scale = painter->device() ? painter->device()->devicePixelRatio() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }

    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override
    {
        const int side = std::min(size.width(), size.height());
        return { side, side };
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    /// What QIcon::pixmap() actually calls, with the logical size and the
    /// device pixel ratio apart. The base class ignores the ratio and returns a
    /// 1x pixmap for Qt to stretch, which is a blurred icon at 150 % and 200 %.
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override
    {
        if (size.isEmpty() || m_svg.isEmpty()) return {};
        if (scale <= 0.0) scale = 1.0;

        const QPalette palette = QGuiApplication::palette();
        const QSize pixels(qRound(size.width() * scale), qRound(size.height() * scale));
        const bool thin = std::min(size.width(), size.height()) >= kThinStrokeFrom;
        const QString key = QStringLiteral("suspkin-icon:%1:%2x%3:%4:%5:%6:%7")
                                .arg(m_name)
                                .arg(pixels.width())
                                .arg(pixels.height())
                                .arg(int(mode))
                                .arg(int(state))
                                .arg(palette.cacheKey())
                                .arg(thin ? 1 : 0);
        QPixmap cached;
        if (QPixmapCache::find(key, &cached)) return cached;

        const QColor colour = colourFor(palette, mode);
        QByteArray svg = m_svg;
        svg.replace("currentColor", colour.name(QColor::HexRgb).toLatin1());
        if (thin) svg.replace("stroke-width=\"2\"", "stroke-width=\"1.5\"");

        QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            // Square and centred, whatever shape was asked for.
            const int side = std::min(pixels.width(), pixels.height());
            const QRectF target((pixels.width() - side) / 2.0, (pixels.height() - side) / 2.0,
                                side, side);
            QSvgRenderer renderer(svg);
            renderer.render(&painter, target);
            // Opacity is applied to the finished drawing rather than written
            // into the SVG, where every overlapping stroke would add up.
            if (colour.alpha() < 255) {
                painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                painter.fillRect(image.rect(), QColor(0, 0, 0, colour.alpha()));
            }
        }

        QPixmap pixmap = QPixmap::fromImage(std::move(image));
        pixmap.setDevicePixelRatio(scale);
        QPixmapCache::insert(key, pixmap);
        return pixmap;
    }

    QIconEngine* clone() const override { return new ThemedIconEngine(*this); }
    QString key() const override { return QStringLiteral("ThemedIconEngine"); }
    bool isNull() override { return m_svg.isEmpty(); }

private:
    QString m_name;
    QByteArray m_svg;
};

} // namespace

namespace Icons {

QString resourcePath(Icon icon)
{
    const char* file = fileFor(icon);
    return file ? QStringLiteral(":/icons/%1.svg").arg(QLatin1String(file)) : QString();
}

QList<Icon> all()
{
    QList<Icon> icons;
    for (int value = 0; value < int(Icon::Count); ++value) icons << static_cast<Icon>(value);
    return icons;
}

QIcon get(Icon icon)
{
    // Read once per icon: the same few dozen SVGs are asked for by every menu
    // entry and button, and they never change while the program runs.
    static QHash<int, QByteArray> svgs;

    const char* file = fileFor(icon);
    if (!file) {
        qWarning("Icons::get: no icon for value %d", int(icon));
        return {};
    }
    auto it = svgs.find(int(icon));
    if (it == svgs.end()) {
        QFile source(resourcePath(icon));
        QByteArray bytes;
        if (source.open(QIODevice::ReadOnly)) bytes = source.readAll();
        if (bytes.isEmpty()) {
            qWarning("Icons::get: %s is not embedded", qPrintable(resourcePath(icon)));
            return {};
        }
        it = svgs.insert(int(icon), bytes);
    }
    return QIcon(new ThemedIconEngine(QLatin1String(file), it.value()));
}

} // namespace Icons
} // namespace suspkin

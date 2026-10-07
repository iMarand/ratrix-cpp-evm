#include "theme.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QWidget>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace theme {

const Palette& pal() {
    static Palette p;
    return p;
}

QColor mix(const QColor& a, const QColor& b, qreal t) {
    const float k = float(std::clamp(t, 0.0, 1.0));
    auto lerp = [k](float x, float y) { return x + (y - x) * k; };
    return QColor::fromRgbF(lerp(a.redF(), b.redF()), lerp(a.greenF(), b.greenF()),
                            lerp(a.blueF(), b.blueF()), lerp(a.alphaF(), b.alphaF()));
}

QColor alpha(QColor c, qreal a) {
    c.setAlphaF(float(std::clamp(a, 0.0, 1.0)));
    return c;
}

static QStringList uiFamilies() {
    return {"Segoe UI Variable Text", "Segoe UI", "Inter", "Noto Sans", "Helvetica Neue", "Arial"};
}

QFont font(qreal px, int weight) {
    QFont f;
    f.setFamilies(uiFamilies());
    f.setPixelSize(std::max(1, int(std::lround(px))));
    f.setWeight(QFont::Weight(weight));
    return f;
}

QFont display(qreal px, int weight) {
    QFont f = font(px, weight);
    f.setFamilies(QStringList{"Segoe UI Variable Display"} + uiFamilies());
    return f;
}

QFont mono(qreal px, int weight) {
    QFont f;
    f.setFamilies({"Cascadia Mono", "Cascadia Code", "JetBrains Mono", "Consolas", "DejaVu Sans Mono", "Menlo"});
    f.setStyleHint(QFont::Monospace);
    f.setPixelSize(std::max(1, int(std::lround(px))));
    f.setWeight(QFont::Weight(weight));
    return f;
}


static QString css(const QColor& c) {
    if (c.alpha() == 255) return c.name(QColor::HexRgb);
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

static const char* kQss = R"QSS(
* { outline: 0; }
QWidget { color: @text; background: transparent; }
QMainWindow { background: @bg; }
QToolTip { background: @elevated; color: @text; border: none; padding: 6px 10px; border-radius: 8px; }

QLabel { background: transparent; }
QLabel#muted { color: @muted; }
QLabel#faint { color: @faint; }
QLabel#danger { color: @danger; }
QLabel#accent { color: @accentText; }

QLineEdit#bare, QPlainTextEdit#bare {
    background: transparent; border: none; padding: 0px; margin: 0px; color: @text;
    selection-background-color: @accent; selection-color: @onAccent; placeholder-text-color: @faint; }
QLineEdit { background: @inputBg; border: none; border-radius: 12px; padding: 9px 12px;
    selection-background-color: @accent; selection-color: @onAccent; }

QFrame#inset { background: @inputBg; border: none; border-radius: 12px; }
QFrame#divider { background: @border; border: none; min-height: 1px; max-height: 1px; }

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px 4px 2px; }
QScrollBar::handle:vertical { background: @scroll; border-radius: 3px; min-height: 40px; }
QScrollBar::handle:vertical:hover { background: @scrollHover; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0px; width: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QScrollBar:horizontal { height: 0px; }

QMenu { background: @surface2; border: 1px solid @border; padding: 6px; }
QMenu::item { padding: 7px 22px 7px 12px; border-radius: 6px; color: @text; }
QMenu::item:selected { background: @elevated; }
QMenu::item:disabled { color: @faint; }
QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }
)QSS";

QString styleSheet() {
    const Palette& p = pal();
    const QHash<QString, QColor> tokens = {
        {"bg", p.bg},           {"sidebar", p.sidebar},   {"surface", p.surface},
        {"surface2", p.surface2}, {"elevated", p.elevated}, {"border", p.border},
        {"borderStrong", p.borderStrong}, {"text", p.text}, {"muted", p.muted},
        {"faint", p.faint},     {"accent", p.accent},     {"inputBg", p.inputBg},
        {"scroll", p.scroll},   {"scrollHover", mix(p.scroll, p.faint, 0.6)},
        {"danger", p.danger},   {"onAccent", p.onAccent},
        {"accentText", mix(p.accent, QColor(Qt::white), 0.35)},
    };
    const QString s = QString::fromUtf8(kQss);
    static const QRegularExpression re("@([A-Za-z0-9]+)");
    QString out;
    out.reserve(s.size());
    qsizetype last = 0;
    for (auto it = re.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        const auto c = tokens.constFind(m.captured(1));
        out += c != tokens.constEnd() ? css(*c) : m.captured(0);
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

void apply() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setFont(font(13));

    const Palette& p = pal();
    QPalette qp;
    qp.setColor(QPalette::Window, p.bg);
    qp.setColor(QPalette::WindowText, p.text);
    qp.setColor(QPalette::Base, p.inputBg);
    qp.setColor(QPalette::AlternateBase, p.surface2);
    qp.setColor(QPalette::Text, p.text);
    qp.setColor(QPalette::Button, p.surface2);
    qp.setColor(QPalette::ButtonText, p.text);
    qp.setColor(QPalette::Highlight, p.accent);
    qp.setColor(QPalette::HighlightedText, p.onAccent);
    qp.setColor(QPalette::PlaceholderText, p.faint);
    qp.setColor(QPalette::ToolTipBase, p.elevated);
    qp.setColor(QPalette::ToolTipText, p.text);
    QApplication::setPalette(qp);
    qApp->setStyleSheet(styleSheet());
}

void applyTitleBar(QWidget* window) {
#ifdef _WIN32
    if (!window) return;
    const Palette& p = pal();
    HWND h = reinterpret_cast<HWND>(window->winId());
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20 /*IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    COLORREF cap = RGB(p.bg.red(), p.bg.green(), p.bg.blue());
    COLORREF txt = RGB(p.text.red(), p.text.green(), p.text.blue());
    COLORREF bdr = RGB(p.border.red(), p.border.green(), p.border.blue());
    DwmSetWindowAttribute(h, 35 /*CAPTION_COLOR*/, &cap, sizeof cap);
    DwmSetWindowAttribute(h, 36 /*TEXT_COLOR*/, &txt, sizeof txt);
    DwmSetWindowAttribute(h, 34 /*BORDER_COLOR*/, &bdr, sizeof bdr);
#else
    (void)window;
#endif
}

}  // namespace theme

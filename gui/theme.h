#pragma once
#include <QColor>
#include <QFont>
#include <QString>

class QPainter;
class QRectF;
class QWidget;

// Ratrix design tokens: pure black canvas, charcoal tiles, one lime accent,
// a sage action dock, red reserved for errors and destructive actions. Custom widgets paint themselves from this palette; a small
// global stylesheet covers the stock Qt widgets that remain.

namespace theme {

struct Palette {
    QColor bg{"#000000"};
    QColor sidebar{"#0b0b0b"};   // floating aside panel
    QColor surface{"#121212"};   // tiles and cards
    QColor surface2{"#1a1a1a"};  // inputs, icon buttons
    QColor elevated{"#232323"};  // hover / raised
    QColor border{"#1e1e1e"};
    QColor borderStrong{"#2e2e2e"};
    QColor text{"#f5f5f5"};
    QColor muted{"#9b9b9b"};
    QColor faint{"#5f5f5f"};
    QColor accent{"#d4f53c"};    // lime
    QColor accentHover{"#e0fb63"};
    QColor onAccent{"#0b0b0b"};
    QColor dock{"#a9b4ac"};      // sage action dock
    QColor inputBg{"#141414"};
    QColor scroll{"#2a2a2a"};
    QColor danger{"#e5484d"};
    // Chain brand colors.
    QColor eth{"#627eea"};
    QColor bnb{"#f3ba2f"};
    QColor btc{"#f7931a"};
    QColor usdt{"#26a17b"};
    QColor usdc{"#2775ca"};
    QColor trx{"#eb0029"};
    QColor sol{"#9945ff"};
};

const Palette& pal();

// Returns the application stylesheet with palette tokens substituted.
QString styleSheet();

// Applies Fusion style, palette, font and stylesheet to the whole app.
void apply();

// Windows: paint the native title bar to match (no-op elsewhere).
void applyTitleBar(QWidget* window);

QColor mix(const QColor& a, const QColor& b, qreal t);
QColor alpha(QColor c, qreal a);

QFont font(qreal px, int weight = QFont::Normal);
QFont display(qreal px, int weight = QFont::Bold);  // large numerals / headings
QFont mono(qreal px, int weight = QFont::Normal);

}  // namespace theme

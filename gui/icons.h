#pragma once
#include <QColor>
#include <QRectF>

class QPainter;

// Vector line icons drawn with QPainter on a 24-unit grid, so the app ships
// with no image resources and icons stay crisp at any DPI.
enum class Icon {
    None, Plus, Import, Eye, EyeOff, Copy, Check, Refresh, Trash, Lock, Unlock, Key,
    Wallet, Shield, Search, Close, Sparkle, Alert, Info, ArrowDown, Pencil, Cube, ChevronRight,
    ArrowRight, Clock, CheckCircle, XCircle, Send, Link,
};

namespace icons {

// `weight` is the stroke width in grid units (scales with the icon).
void paint(QPainter& p, Icon icon, const QRectF& r, const QColor& c, qreal weight = 2.0);

}  // namespace icons

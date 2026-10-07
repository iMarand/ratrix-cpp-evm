#include "icons.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace icons {

void paint(QPainter& p, Icon icon, const QRectF& r, const QColor& c, qreal weight) {
    if (icon == Icon::None || r.isEmpty()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = std::min(r.width(), r.height()) / 24.0;
    p.translate(r.center());
    p.scale(s, s);
    p.translate(-12, -12);
    p.setPen(QPen(c, weight, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    QPainterPath path;
    switch (icon) {
        case Icon::None:
            break;
        case Icon::Plus:
            p.drawLine(QPointF(12, 5), QPointF(12, 19));
            p.drawLine(QPointF(5, 12), QPointF(19, 12));
            break;
        case Icon::Import:
            path.moveTo(4, 15);
            path.lineTo(4, 18);
            path.quadTo(4, 20, 6, 20);
            path.lineTo(18, 20);
            path.quadTo(20, 20, 20, 18);
            path.lineTo(20, 15);
            p.drawPath(path);
            p.drawLine(QPointF(12, 4), QPointF(12, 15));
            p.drawPolyline(QPolygonF{QPointF(7.5, 10.5), QPointF(12, 15), QPointF(16.5, 10.5)});
            break;
        case Icon::Eye:
        case Icon::EyeOff:
            path.moveTo(2.5, 12);
            path.cubicTo(5, 7, 8.5, 5, 12, 5);
            path.cubicTo(15.5, 5, 19, 7, 21.5, 12);
            path.cubicTo(19, 17, 15.5, 19, 12, 19);
            path.cubicTo(8.5, 19, 5, 17, 2.5, 12);
            path.closeSubpath();
            p.drawPath(path);
            p.drawEllipse(QPointF(12, 12), 3, 3);
            if (icon == Icon::EyeOff) p.drawLine(QPointF(4, 4), QPointF(20, 20));
            break;
        case Icon::Copy:
            p.drawRoundedRect(QRectF(9, 9, 11, 11), 2.5, 2.5);
            path.moveTo(6.5, 15);
            path.quadTo(4, 15, 4, 12.5);
            path.lineTo(4, 6.5);
            path.quadTo(4, 4, 6.5, 4);
            path.lineTo(12.5, 4);
            path.quadTo(15, 4, 15, 6.5);
            p.drawPath(path);
            break;
        case Icon::Check:
            p.drawPolyline(QPolygonF{QPointF(5, 12.5), QPointF(10, 17.5), QPointF(19, 7)});
            break;
        case Icon::Refresh: {
            const QRectF arc(4, 4, 16, 16);
            path.arcMoveTo(arc, 35);
            path.arcTo(arc, 35, 285);
            p.drawPath(path);
            p.drawPolyline(QPolygonF{QPointF(19.5, 3.5), QPointF(19.5, 8.2), QPointF(14.8, 8.2)});
            break;
        }
        case Icon::Trash:
            p.drawLine(QPointF(4, 7), QPointF(20, 7));
            path.moveTo(9, 7);
            path.lineTo(9, 5);
            path.quadTo(9, 4, 10, 4);
            path.lineTo(14, 4);
            path.quadTo(15, 4, 15, 5);
            path.lineTo(15, 7);
            path.moveTo(6, 7);
            path.lineTo(7, 19);
            path.quadTo(7.2, 20.5, 8.7, 20.5);
            path.lineTo(15.3, 20.5);
            path.quadTo(16.8, 20.5, 17, 19);
            path.lineTo(18, 7);
            p.drawPath(path);
            p.drawLine(QPointF(10, 11), QPointF(10, 16.5));
            p.drawLine(QPointF(14, 11), QPointF(14, 16.5));
            break;
        case Icon::Lock:
        case Icon::Unlock:
            p.drawRoundedRect(QRectF(5, 11, 14, 10), 2.5, 2.5);
            path.moveTo(8, 11);
            path.lineTo(8, 8);
            path.arcTo(QRectF(8, 4, 8, 8), 180, icon == Icon::Lock ? -180 : -150);
            if (icon == Icon::Lock) path.lineTo(16, 11);
            p.drawPath(path);
            p.drawLine(QPointF(12, 15), QPointF(12, 17));
            break;
        case Icon::Key:
            p.drawEllipse(QPointF(8, 15.5), 4, 4);
            p.drawLine(QPointF(10.9, 12.6), QPointF(20, 3.5));
            p.drawLine(QPointF(16.5, 7), QPointF(19, 9.5));
            p.drawLine(QPointF(14, 9.5), QPointF(16, 11.5));
            break;
        case Icon::Wallet:
            p.drawRoundedRect(QRectF(3, 6.5, 18, 13.5), 3, 3);
            path.moveTo(6, 6.5);
            path.lineTo(15.2, 3.6);
            path.quadTo(17, 3.1, 17, 5);
            path.lineTo(17, 6.5);
            p.drawPath(path);
            p.drawRoundedRect(QRectF(14.5, 10.8, 6.5, 5), 2, 2);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(QPointF(17.4, 13.3), 1.1, 1.1);
            break;
        case Icon::Shield:
            path.moveTo(12, 3);
            path.lineTo(19.5, 6);
            path.lineTo(19.5, 11.5);
            path.cubicTo(19.5, 16.5, 16.2, 19.8, 12, 21.2);
            path.cubicTo(7.8, 19.8, 4.5, 16.5, 4.5, 11.5);
            path.lineTo(4.5, 6);
            path.closeSubpath();
            p.drawPath(path);
            p.drawPolyline(QPolygonF{QPointF(9, 12), QPointF(11.2, 14.2), QPointF(15.5, 9.8)});
            break;
        case Icon::Search:
            p.drawEllipse(QPointF(11, 11), 6.5, 6.5);
            p.drawLine(QPointF(16, 16), QPointF(20.5, 20.5));
            break;
        case Icon::Close:
            p.drawLine(QPointF(6.5, 6.5), QPointF(17.5, 17.5));
            p.drawLine(QPointF(17.5, 6.5), QPointF(6.5, 17.5));
            break;
        case Icon::Sparkle:
            path.moveTo(11, 4);
            path.quadTo(11.8, 11.2, 19, 12);
            path.quadTo(11.8, 12.8, 11, 20);
            path.quadTo(10.2, 12.8, 3, 12);
            path.quadTo(10.2, 11.2, 11, 4);
            p.drawPath(path);
            p.drawLine(QPointF(19, 2.5), QPointF(19, 6.5));
            p.drawLine(QPointF(17, 4.5), QPointF(21, 4.5));
            break;
        case Icon::Alert:
            path.moveTo(12, 3.5);
            path.lineTo(21.5, 20);
            path.lineTo(2.5, 20);
            path.closeSubpath();
            p.drawPath(path);
            p.drawLine(QPointF(12, 9.5), QPointF(12, 14));
            p.drawPoint(QPointF(12, 17));
            break;
        case Icon::Info:
            p.drawEllipse(QPointF(12, 12), 9, 9);
            p.drawLine(QPointF(12, 11), QPointF(12, 16.5));
            p.drawPoint(QPointF(12, 7.8));
            break;
        case Icon::ArrowDown:
            p.drawLine(QPointF(12, 4), QPointF(12, 16.5));
            p.drawPolyline(QPolygonF{QPointF(6.5, 11), QPointF(12, 16.5), QPointF(17.5, 11)});
            p.drawLine(QPointF(5, 20.5), QPointF(19, 20.5));
            break;
        case Icon::Pencil:
            path.moveTo(4, 20);
            path.lineTo(4.8, 16);
            path.lineTo(15.5, 5.3);
            path.quadTo(17, 3.8, 18.5, 5.3);
            path.quadTo(20.2, 7, 18.7, 8.5);
            path.lineTo(8, 19.2);
            path.closeSubpath();
            p.drawPath(path);
            p.drawLine(QPointF(13.5, 7.3), QPointF(16.7, 10.5));
            break;
        case Icon::Cube:
            path.moveTo(12, 2.8);
            path.lineTo(20, 7.4);
            path.lineTo(20, 16.6);
            path.lineTo(12, 21.2);
            path.lineTo(4, 16.6);
            path.lineTo(4, 7.4);
            path.closeSubpath();
            p.drawPath(path);
            p.drawPolyline(QPolygonF{QPointF(4, 7.4), QPointF(12, 12), QPointF(20, 7.4)});
            p.drawLine(QPointF(12, 12), QPointF(12, 21.2));
            break;
        case Icon::ChevronRight:
            p.drawPolyline(QPolygonF{QPointF(9, 6), QPointF(15, 12), QPointF(9, 18)});
            break;
        case Icon::ArrowRight:
            p.drawLine(QPointF(5, 12), QPointF(19, 12));
            p.drawPolyline(QPolygonF{QPointF(13, 6), QPointF(19, 12), QPointF(13, 18)});
            break;
        case Icon::Send:
            p.drawPolyline(QPolygonF{QPointF(21, 3), QPointF(10.5, 13.5)});
            p.drawPolygon(QPolygonF{QPointF(21, 3), QPointF(14.5, 21), QPointF(10.5, 13.5), QPointF(3, 9.5)});
            break;
        case Icon::Link:
            p.drawPolyline(QPolygonF{QPointF(10, 14), QPointF(14, 10)});
            p.drawPolyline(QPolygonF{QPointF(8.5, 11.5), QPointF(6, 14), QPointF(6, 14)});
            {QPainterPath lp; lp.moveTo(9.5,15.5); lp.lineTo(7.8,17.2); lp.quadTo(6,19,4.2,17.2); lp.quadTo(2.4,15.4,4.2,13.6); lp.lineTo(6,11.8); p.drawPath(lp);
             QPainterPath rp; rp.moveTo(14.5,8.5); rp.lineTo(16.2,6.8); rp.quadTo(18,5,19.8,6.8); rp.quadTo(21.6,8.6,19.8,10.4); rp.lineTo(18,12.2); p.drawPath(rp);}
            break;
        case Icon::Clock:
            p.drawEllipse(QPointF(12, 12), 9, 9);
            p.drawPolyline(QPolygonF{QPointF(12, 7), QPointF(12, 12), QPointF(15.5, 14)});
            break;
        case Icon::CheckCircle:
            p.drawEllipse(QPointF(12, 12), 9, 9);
            p.drawPolyline(QPolygonF{QPointF(8.2, 12.3), QPointF(10.8, 14.8), QPointF(15.8, 9.4)});
            break;
        case Icon::XCircle:
            p.drawEllipse(QPointF(12, 12), 9, 9);
            p.drawLine(QPointF(9.2, 9.2), QPointF(14.8, 14.8));
            p.drawLine(QPointF(14.8, 9.2), QPointF(9.2, 14.8));
            break;
    }
    p.restore();
}

}  // namespace icons

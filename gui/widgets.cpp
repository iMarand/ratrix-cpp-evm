#include "widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QImage>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPropertyAnimation>
#include <QStyle>
#include <QTextDocument>

#include <algorithm>
#include <cmath>

#include "theme.h"

using theme::alpha;
using theme::mix;

namespace {
const QColor kWhite(255, 255, 255);
}  // namespace

// ===========================================================================
// Tween
// ===========================================================================

Tween::Tween(QWidget* owner, int ms, QEasingCurve::Type curve) : owner_(owner) {
    anim_.setDuration(ms);
    anim_.setEasingCurve(curve);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        v_ = v.toReal();
        if (owner_) owner_->update();
        if (onChange) onChange(v_);
    });
    connect(&anim_, &QVariantAnimation::finished, this, [this] {
        if (onFinished) onFinished();
    });
}

void Tween::to(qreal target) {
    if (target == target_ && (anim_.state() == QAbstractAnimation::Running || v_ == target)) return;
    target_ = target;
    anim_.stop();
    anim_.setStartValue(v_);
    anim_.setEndValue(target);
    anim_.start();
}

void Tween::set(qreal v) {
    anim_.stop();
    v_ = target_ = v;
    if (owner_) owner_->update();
    if (onChange) onChange(v_);
}

// ===========================================================================
// Button
// ===========================================================================

Button::Button(const QString& text, Variant v, QWidget* parent)
    : QPushButton(text, parent), variant_(v), hover_(this, 140), press_(this, 90) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    spin_.setStartValue(0.0);
    spin_.setEndValue(360.0);
    spin_.setDuration(850);
    spin_.setLoopCount(-1);
    connect(&spin_, &QVariantAnimation::valueChanged, this, [this](const QVariant& a) {
        angle_ = a.toReal();
        update();
    });
    flashTimer_.setSingleShot(true);
    connect(&flashTimer_, &QTimer::timeout, this, [this] {
        flash_ = Icon::None;
        update();
    });
}

void Button::setVariant(Variant v) { variant_ = v; updateGeometry(); update(); }
void Button::setIconId(Icon i) { icon_ = i; updateGeometry(); update(); }
void Button::setCompact(bool on) { compact_ = on; updateGeometry(); update(); }

void Button::setLoading(bool on) {
    if (loading_ == on) return;
    loading_ = on;
    if (on) spin_.start();
    else spin_.stop();
    update();
}

void Button::flash(Icon i, int ms) {
    flash_ = i;
    flashTimer_.start(ms);
    update();
}

QFont Button::labelFont() const {
    return theme::font(13, variant_ == Primary ? QFont::DemiBold : QFont::Medium);
}

QSize Button::sizeHint() const {
    const int h = compact_ ? 32 : 40;
    if (text().isEmpty()) return QSize(h, h);
    QFontMetrics fm(labelFont());
    const int w = fm.horizontalAdvance(text()) + (icon_ != Icon::None ? 15 + 8 : 0) + (compact_ ? 26 : 36);
    return QSize(w, h);
}

void Button::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal h = hover_, pr = press_;
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    // Text buttons are pills; icon-only buttons are rounded squares (or circles when ghost).
    const bool iconOnly = text().isEmpty();
    const bool squared = variant_ == Square || (iconOnly && (variant_ == Secondary || variant_ == Danger));
    const qreal radius = squared ? std::min(12.0, r.height() * 0.3) : r.height() / 2;
    if (!isEnabled()) p.setOpacity(0.4);
    if (pr > 0) {
        const QPointF c = r.center();
        const qreal k = 1 - 0.03 * pr;
        p.translate(c);
        p.scale(k, k);
        p.translate(-c);
    }

    QPainterPath shape;
    shape.addRoundedRect(r, radius, radius);
    QColor fg;
    switch (variant_) {
        case Primary:
            p.fillPath(shape, mix(P.accent, P.accentHover, h));
            fg = P.onAccent;
            break;
        case Secondary:
        case Square:
            p.fillPath(shape, mix(P.surface2, P.elevated, h));
            fg = P.text;
            break;
        case Ghost:
            p.fillPath(shape, alpha(kWhite, 0.06 * h));
            fg = mix(P.muted, P.text, h);
            break;
        case Danger:
            p.fillPath(shape, mix(P.surface2, mix(P.surface2, P.danger, 0.22), h));
            fg = P.danger;
            break;
    }

    paintOverlay(p, shape);

    if (keyFocus_ && hasFocus()) {
        p.setPen(QPen(P.accent, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    // Content: [icon] [text], centered.
    const Icon ic = flash_ != Icon::None ? flash_ : icon_;
    const QString t = text();
    const QFont f = labelFont();
    QFontMetricsF fm(f);
    const qreal is = 16;
    if (loading_ && ic == Icon::None) {
        ui::paintSpinner(p, QRectF(r.center().x() - 8, r.center().y() - 8, 16, 16), angle_, fg, 1.8);
        return;
    }
    const bool hasIcon = loading_ || ic != Icon::None;
    const qreal gap = t.isEmpty() ? 0 : 8;
    const qreal tw = t.isEmpty() ? 0 : fm.horizontalAdvance(t);
    const qreal cw = (hasIcon ? is + gap : 0) + tw;
    qreal x = r.center().x() - cw / 2;
    if (hasIcon) {
        const QRectF ir(x, r.center().y() - is / 2, is, is);
        if (loading_) ui::paintSpinner(p, ir.adjusted(1, 1, -1, -1), angle_, fg, 1.7);
        else icons::paint(p, ic, ir, flash_ == Icon::Check && variant_ != Primary ? P.accent : fg);
        x += is + gap;
    }
    if (!t.isEmpty()) {
        p.setPen(fg);
        p.setFont(f);
        p.drawText(QRectF(x, r.top(), tw + 2, r.height()), Qt::AlignVCenter | Qt::AlignLeft, t);
    }
}

void Button::enterEvent(QEnterEvent* e) {
    if (isEnabled()) hover_.to(1);
    QPushButton::enterEvent(e);
}

void Button::leaveEvent(QEvent* e) {
    hover_.to(0);
    QPushButton::leaveEvent(e);
}

void Button::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) press_.to(1);
    QPushButton::mousePressEvent(e);
}

void Button::mouseReleaseEvent(QMouseEvent* e) {
    press_.to(0);
    QPushButton::mouseReleaseEvent(e);
}

void Button::focusInEvent(QFocusEvent* e) {
    keyFocus_ = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason;
    QPushButton::focusInEvent(e);
}

void Button::focusOutEvent(QFocusEvent* e) {
    keyFocus_ = false;
    QPushButton::focusOutEvent(e);
}

void Button::changeEvent(QEvent* e) {
    if (e->type() == QEvent::EnabledChange && !isEnabled()) {
        hover_.set(0);
        press_.set(0);
    }
    QPushButton::changeEvent(e);
}

// ===========================================================================
// HoldButton
// ===========================================================================

HoldButton::HoldButton(const QString& text, int ms, QWidget* parent) : Button(text, Primary, parent), ms_(ms) {
    connect(&hold_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        t_ = v.toReal();
        update();
    });
    connect(&hold_, &QVariantAnimation::finished, this, [this] {
        if (t_ >= 0.999 && !done_) {
            done_ = true;
            emit held();
        }
    });
}

void HoldButton::runTo(qreal target, int ms, QEasingCurve::Type curve) {
    hold_.stop();
    hold_.setStartValue(t_);
    hold_.setEndValue(target);
    hold_.setDuration(std::max(1, ms));
    hold_.setEasingCurve(curve);
    hold_.start();
}

void HoldButton::mousePressEvent(QMouseEvent* e) {
    Button::mousePressEvent(e);
    if (!done_ && e->button() == Qt::LeftButton) runTo(1.0, int(ms_ * (1 - t_)), QEasingCurve::Linear);
}

void HoldButton::mouseReleaseEvent(QMouseEvent* e) {
    Button::mouseReleaseEvent(e);
    if (!done_) runTo(0.0, int(280 * t_), QEasingCurve::OutCubic);
}

void HoldButton::keyPressEvent(QKeyEvent* e) {
    if (e->key() != Qt::Key_Space) return Button::keyPressEvent(e);
    if (!e->isAutoRepeat() && !done_) runTo(1.0, int(ms_ * (1 - t_)), QEasingCurve::Linear);
    e->accept();
}

void HoldButton::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() != Qt::Key_Space) return Button::keyReleaseEvent(e);
    if (!e->isAutoRepeat() && !done_) runTo(0.0, int(280 * t_), QEasingCurve::OutCubic);
    e->accept();
}

void HoldButton::reset() {
    hold_.stop();
    t_ = 0;
    done_ = false;
    setLoading(false);
    setEnabled(true);
    update();
}

void HoldButton::paintOverlay(QPainter& p, const QPainterPath& shape) {
    if (t_ <= 0) return;
    p.save();
    p.setClipPath(shape);
    const QRectF r = shape.boundingRect();
    p.fillRect(QRectF(r.left(), r.top(), r.width() * t_, r.height()), alpha(QColor(0, 0, 0), 0.16));
    p.restore();
}

// ===========================================================================
// Card / IconTile / LogoMark / Avatar / ElideLabel
// ===========================================================================

Card::Card(QWidget* parent) : QFrame(parent), hover_(this, 220) {}

void Card::setHoverable(bool on) {
    hoverable_ = on;
    setAttribute(Qt::WA_Hover, on);
}

void Card::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(mix(P.surface, P.surface2, hover_));
    p.drawRoundedRect(QRectF(rect()), radius_, radius_);
}

void Card::enterEvent(QEnterEvent* e) {
    if (hoverable_) hover_.to(1);
    QFrame::enterEvent(e);
}

void Card::leaveEvent(QEvent* e) {
    if (hoverable_) hover_.to(0);
    QFrame::leaveEvent(e);
}

IconTile::IconTile(Icon icon, const QColor& tone, int size, QWidget* parent)
    : QWidget(parent), icon_(icon), tone_(tone), pop_(this, 480, QEasingCurve::OutBack) {
    setFixedSize(size, size);
    pop_.set(1);
}

void IconTile::setIcon(Icon i, const QColor& tone) {
    icon_ = i;
    tone_ = tone;
    update();
}

void IconTile::pop() {
    pop_.set(0);
    pop_.to(1);
}

void IconTile::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal k = 0.7 + 0.3 * pop_;
    p.translate(r.center());
    p.scale(k, k);
    p.translate(-r.center());
    p.setPen(Qt::NoPen);
    p.setBrush(solid_ ? tone_ : P.surface2);
    p.drawEllipse(r);
    const qreal in = r.width() * 0.29;
    icons::paint(p, icon_, r.adjusted(in, in, -in, -in), solid_ ? P.onAccent : tone_, 2.0);
}

LogoMark::LogoMark(int size, QWidget* parent) : QWidget(parent) { setFixedSize(size, size); }

void LogoMark::paintEvent(QPaintEvent*) {
    QPainter p(this);
    ui::paintLogo(p, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
}

Avatar::Avatar(int size, QWidget* parent) : QWidget(parent) { setFixedSize(size, size); }

void Avatar::setLabel(const QString& label) {
    label_ = label;
    update();
}

void Avatar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    ui::paintAvatar(p, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), label_);
}

ElideLabel::ElideLabel(const QString& text, QWidget* parent)
    : QWidget(parent), text_(text), color_(theme::pal().text) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ElideLabel::setText(const QString& t) {
    text_ = t;
    setToolTip(t);
    updateGeometry();
    update();
}

QSize ElideLabel::sizeHint() const {
    QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(text_) + 2, fm.height() + 2);
}

QSize ElideLabel::minimumSizeHint() const {
    QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(QStringLiteral("0x1234…abcd")), fm.height() + 2);
}

void ElideLabel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QFontMetrics fm(font());
    p.setPen(color_);
    p.setFont(font());
    p.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(text_, mode_, width()));
}

// ===========================================================================
// FieldFrame / TextField / TextArea
// ===========================================================================

FieldFrame::FieldFrame(QWidget* editor, bool multiline, QWidget* parent)
    : QWidget(parent), editor_(editor), multiline_(multiline), focus_(this, 200), error_(this, 220),
      hover_(this, 150) {
    setAttribute(Qt::WA_Hover);
    lay_ = new QHBoxLayout(this);
    lay_->setContentsMargins(16, multiline ? 12 : 0, 16, multiline ? 12 : 0);
    lay_->setSpacing(6);
    editor->setObjectName("bare");
    lay_->addWidget(editor, 1);
    editor->installEventFilter(this);
    setCursor(Qt::IBeamCursor);
    setFixedHeight(multiline ? 112 : 46);
    setFocusProxy(editor);
}

void FieldFrame::setError(bool on) { error_.to(on ? 1 : 0); }

void FieldFrame::setLarge(bool on) {
    large_ = on;
    if (!multiline_) setFixedHeight(on ? 54 : 46);
    QFont f = editor_->font();
    f.setPixelSize(on ? 15 : 13);
    editor_->setFont(f);
    update();
}

void FieldFrame::setLeadingIcon(Icon i) {
    lead_ = i;
    QMargins m = lay_->contentsMargins();
    m.setLeft(i == Icon::None ? 16 : 42);
    lay_->setContentsMargins(m);
    update();
}

void FieldFrame::addTrailing(QWidget* w) {
    QMargins m = lay_->contentsMargins();
    m.setRight(6);
    lay_->setContentsMargins(m);
    w->setCursor(Qt::PointingHandCursor);
    lay_->addWidget(w, 0, multiline_ ? Qt::AlignTop : Qt::AlignVCenter);
}

void FieldFrame::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal f = focus_, e = error_, h = hover_;
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal rad = large_ ? 16 : 14;
    p.setPen(Qt::NoPen);
    p.setBrush(mix(P.inputBg, P.surface2, 0.6 * std::max(h, f)));
    p.drawRoundedRect(r, rad, rad);
    if (std::max(f, e) > 0.01) {
        p.setPen(QPen(e > 0.01 ? alpha(P.danger, 0.85 * e) : alpha(kWhite, 0.22 * f), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, rad, rad);
    }
    if (lead_ != Icon::None) {
        const qreal y = multiline_ ? 14 : height() / 2.0 - 8;
        icons::paint(p, lead_, QRectF(16, y, 16, 16), mix(P.faint, P.muted, f));
    }
}

bool FieldFrame::eventFilter(QObject* o, QEvent* e) {
    if (o == editor_) {
        if (e->type() == QEvent::FocusIn) focus_.to(1);
        else if (e->type() == QEvent::FocusOut) focus_.to(0);
    }
    return QWidget::eventFilter(o, e);
}

void FieldFrame::mousePressEvent(QMouseEvent* e) {
    editor_->setFocus(Qt::MouseFocusReason);
    QWidget::mousePressEvent(e);
}

void FieldFrame::enterEvent(QEnterEvent* e) {
    hover_.to(1);
    QWidget::enterEvent(e);
}

void FieldFrame::leaveEvent(QEvent* e) {
    hover_.to(0);
    QWidget::leaveEvent(e);
}

TextField::TextField(const QString& placeholder, QWidget* parent) : FieldFrame(new QLineEdit, false, parent) {
    edit_ = static_cast<QLineEdit*>(editor_);
    edit_->setPlaceholderText(placeholder);
    edit_->setFont(theme::font(13));
    connect(edit_, &QLineEdit::textChanged, this, &TextField::textChanged);
    connect(edit_, &QLineEdit::returnPressed, this, &TextField::returnPressed);
}

QString TextField::text() const { return edit_->text(); }
void TextField::setText(const QString& t) { edit_->setText(t); }

void TextField::setPassword(bool withToggle) {
    edit_->setEchoMode(QLineEdit::Password);
    if (!withToggle) return;
    auto* eye = new Button("", Button::Ghost);
    eye->setCompact(true);
    eye->setIconId(Icon::Eye);
    eye->setFixedSize(28, 28);
    eye->setFocusPolicy(Qt::NoFocus);
    eye->setToolTip("Show / hide");
    connect(eye, &QPushButton::clicked, this, [this, eye] {
        const bool hidden = edit_->echoMode() == QLineEdit::Password;
        edit_->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        eye->setIconId(hidden ? Icon::EyeOff : Icon::Eye);
    });
    addTrailing(eye);
}

void TextField::setMono(bool on) { edit_->setFont(on ? theme::mono(13) : theme::font(13)); }

TextArea::TextArea(const QString& placeholder, QWidget* parent) : FieldFrame(new QPlainTextEdit, true, parent) {
    edit_ = static_cast<QPlainTextEdit*>(editor_);
    edit_->setPlaceholderText(placeholder);
    edit_->setFont(theme::font(13));
    edit_->setFrameShape(QFrame::NoFrame);
    edit_->setTabChangesFocus(true);
    edit_->document()->setDocumentMargin(1);
    edit_->viewport()->setAutoFillBackground(false);
    connect(edit_, &QPlainTextEdit::textChanged, this, &TextArea::textChanged);
}

QString TextArea::text() const { return edit_->toPlainText(); }
void TextArea::setText(const QString& t) { edit_->setPlainText(t); }
void TextArea::setMono(bool on) { edit_->setFont(on ? theme::mono(13) : theme::font(13)); }

// ===========================================================================
// Segmented
// ===========================================================================

Segmented::Segmented(const QStringList& items, QWidget* parent)
    : QWidget(parent), items_(items), pos_(this, 280, QEasingCurve::OutCubic) {
    setFixedHeight(36);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    pos_.set(0);
}

QRectF Segmented::seg(int i) const {
    const qreal pad = 3;
    const qreal w = (width() - 2 * pad) / std::max<qreal>(1, items_.size());
    return QRectF(pad + i * w, pad, w, height() - 2 * pad);
}

int Segmented::indexAt(const QPointF& pt) const {
    for (int i = 0; i < items_.size(); ++i)
        if (seg(i).contains(pt)) return i;
    return -1;
}

void Segmented::setCurrent(int i) {
    if (i < 0 || i >= items_.size() || i == cur_) return;
    cur_ = i;
    pos_.to(i);
    emit changed(i);
}

QSize Segmented::sizeHint() const {
    QFontMetrics fm(theme::font(12, QFont::Medium));
    int w = 0;
    for (const auto& t : items_) w = std::max(w, fm.horizontalAdvance(t));
    return QSize(int((w + 30) * items_.size() + 6), 36);
}

void Segmented::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled()) p.setOpacity(0.45);
    const QRectF r = QRectF(rect());
    p.setPen(Qt::NoPen);
    p.setBrush(P.inputBg);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);

    const QRectF s0 = seg(0);
    const QRectF pill = s0.translated(pos_ * s0.width(), 0);
    p.setBrush(P.elevated);
    p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);

    p.setFont(theme::font(12, QFont::Medium));
    for (int i = 0; i < items_.size(); ++i) {
        const qreal near = 1 - std::clamp(std::abs(pos_ - i), 0.0, 1.0);
        const QColor base = i == hover_ ? mix(P.faint, P.text, 0.6) : P.muted;
        p.setPen(mix(base, P.text, near));
        p.drawText(seg(i), Qt::AlignCenter, items_[i]);
    }
}

void Segmented::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && isEnabled()) setCurrent(indexAt(e->position()));
}

void Segmented::mouseMoveEvent(QMouseEvent* e) {
    const int h = indexAt(e->position());
    if (h != hover_) {
        hover_ = h;
        update();
    }
}

void Segmented::leaveEvent(QEvent*) {
    hover_ = -1;
    update();
}

// ===========================================================================
// StrengthMeter
// ===========================================================================

StrengthMeter::StrengthMeter(QWidget* parent) : QWidget(parent), level_(this, 340) { setFixedHeight(18); }

int StrengthMeter::score(const QString& pw) {
    if (pw.isEmpty()) return 0;
    bool lo = false, up = false, di = false, sy = false;
    for (QChar c : pw) {
        if (c.isLower()) lo = true;
        else if (c.isUpper()) up = true;
        else if (c.isDigit()) di = true;
        else sy = true;
    }
    const int classes = int(lo) + int(up) + int(di) + int(sy);
    int s = 1;
    if (pw.size() >= 10) ++s;
    if (pw.size() >= 14) ++s;
    if (classes >= 3 && pw.size() >= 8) ++s;
    if (pw.size() < 6) s = 1;
    return std::min(s, 4);
}

void StrengthMeter::setPassword(const QString& pw) {
    score_ = score(pw);
    level_.to(score_);
}

void StrengthMeter::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    static const char* names[] = {"", "Weak", "Fair", "Good", "Strong"};
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal labelW = 50;
    const QRectF track(2, height() / 2.0 - 1.5, width() - labelW - 12, 3);
    p.setPen(Qt::NoPen);
    p.setBrush(P.surface2);
    p.drawRoundedRect(track, 1.5, 1.5);
    const qreal fill = std::clamp(level_.value() / 4.0, 0.0, 1.0);
    if (fill > 0) {
        p.setBrush(score_ <= 1 ? P.danger : P.accent);
        p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * fill, track.height()), 1.5, 1.5);
    }
    p.setPen(P.faint);
    p.setFont(theme::font(12, QFont::Medium));
    p.drawText(QRectF(width() - labelW, 0, labelW, height()), Qt::AlignRight | Qt::AlignVCenter, names[score_]);
}

// ===========================================================================
// Toast
// ===========================================================================

namespace {
constexpr int kToastPad = 2;
}

Toast::Toast(QWidget* host) : QWidget(host), t_(this, 280) {
    setObjectName("ratrixToast");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    host->installEventFilter(this);
    hold_.setSingleShot(true);
    connect(&hold_, &QTimer::timeout, this, [this] {
        t_.setDuration(220);
        t_.to(0);
    });
    t_.onChange = [this](qreal v) {
        place();
        if (v <= 0.001 && t_.target() == 0) hide();
    };
}

void Toast::notify(QWidget* from, const QString& text, Icon icon, bool error) {
    QWidget* host = ui::hostWindow(from);
    if (!host) return;
    auto* t = host->findChild<Toast*>("ratrixToast", Qt::FindDirectChildrenOnly);
    if (!t) t = new Toast(host);
    t->popup(text, icon, error);
}

void Toast::popup(const QString& text, Icon icon, bool error) {
    text_ = text;
    icon_ = icon;
    error_ = error;
    QFontMetrics fm(theme::font(13, QFont::Medium));
    resize(fm.horizontalAdvance(text_) + 8 + 24 + 10 + 20 + 2 * kToastPad, 40 + 2 * kToastPad);
    raise();
    show();
    t_.setDuration(300);
    t_.to(1);
    place();
    update();
    hold_.start(error ? 3400 : 2100);
}

void Toast::place() {
    if (QWidget* h = parentWidget())
        move((h->width() - width()) / 2, h->height() - height() - 20 + int((1 - t_.value()) * 12));
}

bool Toast::eventFilter(QObject* o, QEvent* e) {
    if (o == parentWidget() && e->type() == QEvent::Resize) place();
    return QWidget::eventFilter(o, e);
}

void Toast::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(std::clamp(t_.value(), 0.0, 1.0));
    const QRectF body = QRectF(rect()).adjusted(kToastPad, kToastPad, -kToastPad, -kToastPad);
    p.setPen(Qt::NoPen);
    p.setBrush(P.elevated);
    p.drawRoundedRect(body, body.height() / 2, body.height() / 2);
    const QRectF dot(body.left() + 8, body.center().y() - 12, 24, 24);
    p.setBrush(error_ ? P.danger : P.accent);
    p.drawEllipse(dot);
    icons::paint(p, error_ ? Icon::Close : icon_, dot.adjusted(6, 6, -6, -6), error_ ? kWhite : P.onAccent, 2.6);
    p.setPen(P.text);
    p.setFont(theme::font(13, QFont::Medium));
    p.drawText(QRectF(dot.right() + 10, body.top(), body.width(), body.height()), Qt::AlignVCenter | Qt::AlignLeft,
               text_);
}

// ===========================================================================
// DockButton / Dock
// ===========================================================================

DockButton::DockButton(Icon icon, const QString& tip, bool primary, QWidget* parent)
    : QPushButton(parent), icon_(icon), primary_(primary), hover_(this, 140) {
    setToolTip(tip);
    setAccessibleName(tip);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setFixedSize(44, 44);
}

void DockButton::paintEvent(QPaintEvent*) {
    const auto& P = theme::pal();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setPen(Qt::NoPen);
    if (primary_) p.setBrush(mix(P.accent, P.accentHover, hover_));
    else p.setBrush(alpha(QColor(0, 0, 0), 0.10 * hover_));
    p.drawEllipse(r);
    if (hasFocus()) {
        p.setPen(QPen(P.onAccent, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r.adjusted(1, 1, -1, -1));
    }
    icons::paint(p, icon_, QRectF(r.center().x() - 9, r.center().y() - 9, 18, 18), QColor("#111311"), 2.0);
}

void DockButton::enterEvent(QEnterEvent* e) {
    hover_.to(1);
    QPushButton::enterEvent(e);
}

void DockButton::leaveEvent(QEvent* e) {
    hover_.to(0);
    QPushButton::leaveEvent(e);
}

Dock::Dock(QWidget* parent) : QWidget(parent) {
    lay_ = new QHBoxLayout(this);
    lay_->setContentsMargins(6, 6, 6, 6);
    lay_->setSpacing(6);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

DockButton* Dock::add(Icon icon, const QString& tip, bool primary) {
    auto* b = new DockButton(icon, tip, primary);
    lay_->addWidget(b);
    return b;
}

void Dock::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::pal().dock);
    const QRectF r = QRectF(rect());
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
}

// ===========================================================================
// ui:: helpers
// ===========================================================================

namespace ui {

QLabel* label(const QString& text, qreal px, int weight, const char* tone) {
    auto* l = new QLabel(text);
    l->setFont(theme::font(px, weight));
    if (tone) l->setObjectName(tone);
    return l;
}

QLabel* caption(const QString& text) { return label(text, 13, QFont::Medium, "muted"); }

void setTone(QLabel* l, const char* tone) {
    l->setObjectName(tone ? tone : "");
    l->style()->unpolish(l);
    l->style()->polish(l);
    l->update();
}

QFrame* divider() {
    auto* d = new QFrame();
    d->setObjectName("divider");
    d->setFixedHeight(1);
    return d;
}

void copy(QWidget* from, const QString& text, bool sensitive) {
    if (text.isEmpty()) return;
    QGuiApplication::clipboard()->setText(text);
    if (sensitive) {
        QTimer::singleShot(30000, qApp, [text] {
            QClipboard* cb = QGuiApplication::clipboard();
            if (cb->text() == text) cb->clear();
        });
        Toast::notify(from, "Copied · clipboard clears in 30s", Icon::Shield);
    } else {
        Toast::notify(from, "Copied to clipboard");
    }
}

void fadeIn(QWidget* w, int ms) {
    if (!w) return;
    if (auto* old = w->findChild<QVariantAnimation*>("ui_fade", Qt::FindDirectChildrenOnly)) {
        old->stop();
        delete old;
    }
    auto* eff = new QGraphicsOpacityEffect(w);
    eff->setOpacity(0.0);
    w->setGraphicsEffect(eff);  // replaces (and deletes) any previous effect
    auto* a = new QVariantAnimation(w);
    a->setObjectName("ui_fade");
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    a->setDuration(ms);
    a->setEasingCurve(QEasingCurve::OutCubic);
    QPointer<QGraphicsOpacityEffect> ep(eff);
    QObject::connect(a, &QVariantAnimation::valueChanged, w, [ep](const QVariant& v) {
        if (ep) ep->setOpacity(v.toReal());
    });
    QObject::connect(a, &QVariantAnimation::finished, w, [w, ep] {
        if (ep && w->graphicsEffect() == ep) w->setGraphicsEffect(nullptr);
    });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void shake(QWidget* w) {
    if (!w || w->property("ui_shaking").toBool()) return;
    w->setProperty("ui_shaking", true);
    const QPoint base = w->pos();
    auto* a = new QPropertyAnimation(w, "pos", w);
    a->setDuration(440);
    a->setStartValue(base);
    const int d[] = {-11, 10, -7, 5, -3, 1};
    for (int i = 0; i < 6; ++i) a->setKeyValueAt((i + 1) / 7.0, base + QPoint(d[i], 0));
    a->setEndValue(base);
    QObject::connect(a, &QPropertyAnimation::finished, w, [w] { w->setProperty("ui_shaking", false); });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

QWidget* hostWindow(QWidget* w) {
    QWidget* win = w ? w->window() : nullptr;
    while (win && qobject_cast<QDialog*>(win) && win->parentWidget()) win = win->parentWidget()->window();
    return win;
}

QString shortAddress(const QString& a, int head, int tail) {
    if (a.size() <= head + tail + 1) return a;
    return a.left(head) + QStringLiteral("…") + a.right(tail);
}

void paintSpinner(QPainter& p, const QRectF& r, qreal angleDeg, const QColor& c, qreal width) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(alpha(c, 0.18), width, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(r);
    p.setPen(QPen(c, width, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(r, int(-angleDeg * 16), 100 * 16);
    p.restore();
}

void paintAvatar(QPainter& p, const QRectF& r, const QString& label) {
    const auto& P = theme::pal();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(P.elevated);
    p.drawEllipse(r);
    p.setPen(P.text);
    p.setFont(theme::font(r.height() * 0.4, QFont::DemiBold));
    p.drawText(r, Qt::AlignCenter, label.trimmed().left(1).toUpper());
    p.restore();
}

// Draws the brand artwork, picking the smallest pre-scaled size that still
// covers the target at the current device pixel ratio.
void paintLogo(QPainter& p, const QRectF& r) {
    static const int sizes[] = {32, 48, 64, 128, 256, 512};
    static QHash<int, QImage> cache;
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const qreal need = std::max(r.width(), r.height()) * dpr;
    int pick = 512;
    for (int s : sizes)
        if (s >= need) {
            pick = s;
            break;
        }
    auto it = cache.find(pick);
    if (it == cache.end()) it = cache.insert(pick, QImage(QString(":/brand/logo-%1.png").arg(pick)));
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, *it);
    p.restore();
}

}  // namespace ui

#pragma once
#include <QFrame>
#include <QPushButton>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include <functional>

#include "icons.h"

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPainterPath;
class QPlainTextEdit;

// ---------------------------------------------------------------------------
// Animation helpers
// ---------------------------------------------------------------------------

// A value that eases toward a target, repainting its owner on every frame.
class Tween : public QObject {
public:
    explicit Tween(QWidget* owner, int ms = 170, QEasingCurve::Type curve = QEasingCurve::OutCubic);
    void to(qreal target);
    void set(qreal v);  // jump, no animation
    void setDuration(int ms) { anim_.setDuration(ms); }
    qreal value() const { return v_; }
    qreal target() const { return target_; }
    operator qreal() const { return v_; }
    std::function<void(qreal)> onChange;
    std::function<void()> onFinished;

private:
    QWidget* owner_;
    QVariantAnimation anim_;
    qreal v_ = 0, target_ = 0;
};

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

class Button : public QPushButton {
    Q_OBJECT
public:
    // Text buttons are pills; Square is the rounded-square icon button.
    enum Variant { Primary, Secondary, Ghost, Danger, Square };
    explicit Button(const QString& text = {}, Variant v = Secondary, QWidget* parent = nullptr);

    void setVariant(Variant v);
    void setIconId(Icon i);
    void setCompact(bool on);
    void setLoading(bool on);
    bool isLoading() const { return loading_; }
    void flash(Icon i, int ms = 1400);  // briefly swap the icon (copy -> check)

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void changeEvent(QEvent*) override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    virtual void paintOverlay(QPainter&, const QPainterPath&) {}
    QFont labelFont() const;

    Variant variant_;

private:
    Icon icon_ = Icon::None, flash_ = Icon::None;
    bool compact_ = false, loading_ = false, keyFocus_ = false;
    Tween hover_, press_;
    QVariantAnimation spin_;
    qreal angle_ = 0;
    QTimer flashTimer_;
};

// Press-and-hold confirmation: fills while held, emits held() when complete.
class HoldButton : public Button {
    Q_OBJECT
public:
    explicit HoldButton(const QString& text, int ms = 1100, QWidget* parent = nullptr);

signals:
    void held();

public:
    void reset();  // allow another hold after completion (e.g. retry on error)

protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;    // Space works like holding the mouse
    void keyReleaseEvent(QKeyEvent*) override;
    void paintOverlay(QPainter&, const QPainterPath&) override;

private:
    void runTo(qreal target, int ms, QEasingCurve::Type curve);
    QVariantAnimation hold_;
    qreal t_ = 0;
    int ms_;
    bool done_ = false;
};

// ---------------------------------------------------------------------------
// Surfaces and decorations
// ---------------------------------------------------------------------------

class Card : public QFrame {
public:
    explicit Card(QWidget* parent = nullptr);
    void setRadius(qreal r) { radius_ = r; update(); }
    void setHoverable(bool on);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    Tween hover_;
    qreal radius_ = 20;
    bool hoverable_ = false;
};

// Rounded square holding an icon, lightly tinted with `tone`; `round` makes it a circle.
class IconTile : public QWidget {
public:
    IconTile(Icon icon, const QColor& tone, int size = 40, QWidget* parent = nullptr);
    void setIcon(Icon i, const QColor& tone);
    void setSolid(bool on) { solid_ = on; update(); }  // filled tone, dark glyph
    void pop();  // springy entrance

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Icon icon_;
    QColor tone_;
    bool solid_ = false;
    Tween pop_;
};

// The Ratrix brand mark (from resources/brand).
class LogoMark : public QWidget {
public:
    explicit LogoMark(int size, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
};

// Circular monogram avatar.
class Avatar : public QWidget {
public:
    explicit Avatar(int size, QWidget* parent = nullptr);
    void setLabel(const QString& label);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString label_;
};

// Single-line text that elides (middle by default) instead of growing.
class ElideLabel : public QWidget {
public:
    explicit ElideLabel(const QString& text = {}, QWidget* parent = nullptr);
    void setText(const QString& t);
    QString text() const { return text_; }
    void setColor(const QColor& c) { color_ = c; update(); }
    void setElideMode(Qt::TextElideMode m) { mode_ = m; update(); }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString text_;
    QColor color_;
    Qt::TextElideMode mode_ = Qt::ElideMiddle;
};

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

// Rounded input chrome with an animated focus outline, optional leading icon
// and trailing widgets. Hosts a bare QLineEdit / QPlainTextEdit.
class FieldFrame : public QWidget {
    Q_OBJECT
public:
    void setError(bool on);
    void setLeadingIcon(Icon i);
    void addTrailing(QWidget* w);
    void setLarge(bool on);  // taller, bigger text (lock screen)

protected:
    FieldFrame(QWidget* editor, bool multiline, QWidget* parent);
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject* o, QEvent* e) override;
    void mousePressEvent(QMouseEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

    QWidget* editor_;

private:
    QHBoxLayout* lay_;
    Icon lead_ = Icon::None;
    bool multiline_, large_ = false;
    Tween focus_, error_, hover_;
};

class TextField : public FieldFrame {
    Q_OBJECT
public:
    explicit TextField(const QString& placeholder = {}, QWidget* parent = nullptr);
    QLineEdit* edit() const { return edit_; }
    QString text() const;
    void setText(const QString& t);
    void setPassword(bool withToggle = true);
    void setMono(bool on);

signals:
    void textChanged(const QString&);
    void returnPressed();

private:
    QLineEdit* edit_;
};

class TextArea : public FieldFrame {
    Q_OBJECT
public:
    explicit TextArea(const QString& placeholder = {}, QWidget* parent = nullptr);
    QPlainTextEdit* edit() const { return edit_; }
    QString text() const;
    void setText(const QString& t);
    void setMono(bool on);

signals:
    void textChanged();

private:
    QPlainTextEdit* edit_;
};

// Pill-style segmented control with a sliding selection.
class Segmented : public QWidget {
    Q_OBJECT
public:
    explicit Segmented(const QStringList& items, QWidget* parent = nullptr);
    int current() const { return cur_; }
    void setCurrent(int i);
    QSize sizeHint() const override;

signals:
    void changed(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF seg(int i) const;
    int indexAt(const QPointF& p) const;
    QStringList items_;
    int cur_ = 0, hover_ = -1;
    Tween pos_;
};

// Thin passphrase strength bar.
class StrengthMeter : public QWidget {
public:
    explicit StrengthMeter(QWidget* parent = nullptr);
    void setPassword(const QString& pw);
    static int score(const QString& pw);  // 0 (empty) .. 4

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Tween level_;
    int score_ = 0;
};

// ---------------------------------------------------------------------------
// Action dock: the sage pill of round icon buttons
// ---------------------------------------------------------------------------

class DockButton : public QPushButton {
public:
    DockButton(Icon icon, const QString& tip, bool primary, QWidget* parent = nullptr);
    QSize sizeHint() const override { return {44, 44}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    Icon icon_;
    bool primary_;
    Tween hover_;
};

class Dock : public QWidget {
public:
    explicit Dock(QWidget* parent = nullptr);
    DockButton* add(Icon icon, const QString& tip, bool primary = false);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QHBoxLayout* lay_;
};

// ---------------------------------------------------------------------------
// Toast notifications
// ---------------------------------------------------------------------------

class Toast : public QWidget {
    Q_OBJECT
public:
    // Shows a toast at the bottom of `from`'s main window.
    static void notify(QWidget* from, const QString& text, Icon icon = Icon::Check, bool error = false);

protected:
    void paintEvent(QPaintEvent*) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    explicit Toast(QWidget* host);
    void popup(const QString& text, Icon icon, bool error);
    void place();
    QString text_;
    Icon icon_ = Icon::Check;
    bool error_ = false;
    Tween t_;
    QTimer hold_;
};

// ---------------------------------------------------------------------------
// Free helpers
// ---------------------------------------------------------------------------

namespace ui {

QLabel* label(const QString& text, qreal px = 13, int weight = QFont::Normal, const char* tone = nullptr);
QLabel* caption(const QString& text);  // small muted field / section label
void setTone(QLabel* l, const char* tone);
QFrame* divider();

// Copies to the clipboard with a toast. Sensitive values are wiped from the
// clipboard after 30 seconds if still present.
void copy(QWidget* from, const QString& text, bool sensitive = false);

void fadeIn(QWidget* w, int ms = 280);
void shake(QWidget* w);

// The top-level main window behind any dialogs `w` lives in.
QWidget* hostWindow(QWidget* w);

QString shortAddress(const QString& a, int head = 6, int tail = 4);

void paintSpinner(QPainter& p, const QRectF& r, qreal angleDeg, const QColor& c, qreal width = 2);
void paintAvatar(QPainter& p, const QRectF& r, const QString& label);
void paintLogo(QPainter& p, const QRectF& r);

}  // namespace ui

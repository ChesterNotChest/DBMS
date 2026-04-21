/**
 * animated_button.h — 花瓣动画工具按钮
 *
 * Hover 时 6 个花瓣展开 + 旋转动画
 * 替代原有的普通 QToolButton 风格
 */
#ifndef ANIMATED_BUTTON_H
#define ANIMATED_BUTTON_H

#include <QWidget>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QParallelAnimationGroup>
#include <QVector>
#include <QTimer>

class FlowerPetals : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal scale READ scale WRITE setScale)
public:
    FlowerPetals(QWidget *parent = nullptr) : QWidget(parent), m_scale(0.0) {
        setFixedSize(100, 100);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    qreal scale() const { return m_scale; }
    void setScale(qreal s) {
        m_scale = s;
        update();
    }

    // 6 个花瓣的初始角度和相对中心偏移
    static const QVector<QPair<qreal, QPointF>> & petals() {
        static const QVector<QPair<qreal, QPointF>> p = {
            { 5.0,  QPointF(-12, -11) },
            { 35.0, QPointF(  9,  -5) },
            { 0.0,  QPointF(  0,  -14) },
            { 15.0, QPointF(-14,  -3) },
            { 25.0, QPointF( 12,   9) },
            { 30.0, QPointF(-14,  -13) },
        };
        return p;
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QBrush(QLinearGradient(0, 0, width(), height())));
        p.save();

        QLinearGradient grad(0, 0, width(), height());
        grad.setColorAt(0, QColor("#07a6d7"));
        grad.setColorAt(1, QColor("#93e0ee"));
        p.setBrush(grad);
        p.setPen(QPen(QColor("#96d1ec"), 0.5));

        QFont f("Microsoft YaHei", 10, QFont::Bold);
        p.setFont(f);

        qreal r1 = 8.0;
        qreal r2 = r1 * m_scale;
        QPointF center(width() / 2.0, height() / 2.0);

        for (const auto &petal : petals()) {
            qreal baseAngle = petal.first;
            QPointF offset = petal.second;
            offset *= m_scale;
            QPointF pos = center + offset;

            QRectF rc(pos.x() - r2, pos.y() - r2, r2 * 2, r2 * 2);
            p.save();
            p.translate(pos);
            p.rotate(baseAngle + (m_scale - 1.0) * 30.0);  // hover 时额外旋转
            p.translate(-pos);
            p.drawEllipse(rc);
            p.restore();
        }
        p.restore();
    }

private:
    qreal m_scale;
};

class AnimatedButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(qreal scale READ scale WRITE setScale)
public:
    explicit AnimatedButton(const QString &text, QWidget *parent = nullptr);

    qreal scale() const { return m_scale; }
    void setScale(qreal s);

private:
    qreal m_scale = 0.0;
    FlowerPetals *m_flowers[6] = { nullptr };
    QPropertyAnimation *m_scaleAnim = nullptr;
    QPropertyAnimation *m_rotationAnim = nullptr;
    QTimer *m_blurTimer = nullptr;
    int m_rotationAngle = 0;

private slots:
    void onHover();
    void onUnhover();
    void onBlurTick();
};

#endif // ANIMATED_BUTTON_H
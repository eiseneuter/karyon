#pragma once

#include <QObject>
#include <QPoint>
#include <QRect>
#include <memory>

class QQuickView;
class QTimer;

class GestureTrail : public QObject
{
    Q_OBJECT

public:
    explicit GestureTrail(QObject *parent = nullptr);
    ~GestureTrail() override;

    void prewarm();
    void startTrail(const QPoint &startPos);
    void updateCurrentPos(int globalX, int globalY);
    void finishTrail();
    void cancelTrail();

private:
    std::unique_ptr<QQuickView> m_view;
    QRect m_geometry;
    QPoint m_startPos;
    QTimer *m_hideTimer = nullptr;
};

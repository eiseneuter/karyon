#include "GestureTrail.h"
#include "ThemeManager.h"
#include "SettingsManager.h"

#include <QQuickView>
#include <QQuickItem>
#include <QQmlContext>
#include <QGuiApplication>
#include <QScreen>
#include <QColor>
#include <QUrl>
#include <QTimer>
#include <KWindowSystem>
#include <KX11Extras>
#include <xcb/xcb.h>
#include <xcb/shape.h>

GestureTrail::GestureTrail(QObject *parent)
    : QObject(parent)
{
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, [this]() {
        if (m_view && m_view->isVisible()) {
            m_view->hide();
        }
    });

    prewarm();
}

GestureTrail::~GestureTrail()
{
    if (m_view) {
        m_view->close();
    }
}

void GestureTrail::prewarm()
{
    QRect totalGeo;
    for (QScreen *s : QGuiApplication::screens()) {
        totalGeo = totalGeo.united(s->geometry());
    }
    if (totalGeo.isEmpty()) {
        totalGeo = QRect(0, 0, 1920, 1080);
    }
    m_geometry = totalGeo;

    m_view = std::make_unique<QQuickView>();
    m_view->setFlags(
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint |
        Qt::BypassWindowManagerHint |
        Qt::Tool |
        Qt::WindowDoesNotAcceptFocus |
        Qt::WindowTransparentForInput
    );
    m_view->setColor(QColor(Qt::transparent));
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setGeometry(m_geometry);
    m_view->setTitle(QStringLiteral("karyon-trail"));

    m_view->rootContext()->setContextProperty(QStringLiteral("karyonTheme"), &ThemeManager::instance());
    m_view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/karyon/qml/GestureTrail.qml")));
    m_view->create();

    if (KWindowSystem::isPlatformX11()) {
        KX11Extras::setOnAllDesktops(m_view->winId(), true);

        auto *x11App = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        if (x11App) {
            xcb_connection_t *conn = x11App->connection();
            // Leere ShapeInput-Maske setzen -> Das Fenster fängt absolut keine Klicks/Scrolls ab!
            xcb_shape_rectangles(conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT,
                                 XCB_CLIP_ORDERING_UNSORTED,
                                 m_view->winId(), 0, 0, 0, nullptr);
            xcb_flush(conn);
        }
    }
}

void GestureTrail::startTrail(const QPoint &startPos)
{
    if (!m_view || !SettingsManager::instance().showGestureTrail()) return;

    if (m_hideTimer) {
        m_hideTimer->stop();
    }

    QRect totalGeo;
    for (QScreen *s : QGuiApplication::screens()) {
        totalGeo = totalGeo.united(s->geometry());
    }
    if (!totalGeo.isEmpty() && totalGeo != m_geometry) {
        m_geometry = totalGeo;
        m_view->setGeometry(m_geometry);
        if (KWindowSystem::isPlatformX11()) {
            auto *x11App = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
            if (x11App) {
                xcb_connection_t *conn = x11App->connection();
                xcb_shape_rectangles(conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT,
                                     XCB_CLIP_ORDERING_UNSORTED,
                                     m_view->winId(), 0, 0, 0, nullptr);
                xcb_flush(conn);
            }
        }
    }

    m_startPos = startPos;
    int localStartX = startPos.x() - m_geometry.x();
    int localStartY = startPos.y() - m_geometry.y();

    m_view->show();
    m_view->raise();

    QQuickItem *root = m_view->rootObject();
    if (root) {
        QMetaObject::invokeMethod(root, "startTrail", Q_ARG(QVariant, localStartX), Q_ARG(QVariant, localStartY));
    }
}

void GestureTrail::updateCurrentPos(int globalX, int globalY)
{
    if (!m_view || !m_view->isVisible()) return;

    int localCurX = globalX - m_geometry.x();
    int localCurY = globalY - m_geometry.y();

    QQuickItem *root = m_view->rootObject();
    if (root) {
        QMetaObject::invokeMethod(root, "updateCurPos", Q_ARG(QVariant, localCurX), Q_ARG(QVariant, localCurY));
    }
}

void GestureTrail::finishTrail()
{
    if (!m_view || !m_view->isVisible()) return;

    QQuickItem *root = m_view->rootObject();
    if (root) {
        QMetaObject::invokeMethod(root, "finishTrail");
    }

    if (m_hideTimer) {
        m_hideTimer->start(280);
    }
}

void GestureTrail::cancelTrail()
{
    if (!m_view) return;
    if (m_hideTimer) {
        m_hideTimer->stop();
    }
    QQuickItem *root = m_view->rootObject();
    if (root) {
        QMetaObject::invokeMethod(root, "cancelTrail");
    }
    m_view->hide();
}

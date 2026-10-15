#include "SessionDialogController.h"
#include "SessionManager.h"
#include "KaryonGlyphs.h"

#include <QQuickItem>
#include <QGuiApplication>
#include <QScreen>
#include <QColor>
#include <QDir>
#include <QUrl>
#include <algorithm>
#include <QEvent>
#include <QKeyEvent>

#include <KWindowSystem>
#include <KX11Extras>

SessionDialogController::SessionDialogController(QObject *parent)
    : QObject(parent)
{
    m_view = new QQuickView();
    m_view->setColor(QColor(Qt::transparent));
    m_view->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Dialog);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setTitle(QStringLiteral("Karyon Session"));
    m_view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/karyon/qml/SessionDialog.qml")));
    m_view->create();

    if (KWindowSystem::isPlatformX11()) {
        KX11Extras::setOnAllDesktops(m_view->winId(), true);
        KX11Extras::setType(m_view->winId(), NET::Dialog);
    }

    QQuickItem *root = m_view->rootObject();
    if (root) {
        connect(root, SIGNAL(confirmed()), this, SLOT(onConfirmed()));
        connect(root, SIGNAL(cancelled()), this, SLOT(onCancelled()));
    }

    m_view->installEventFilter(this);
}

SessionDialogController::~SessionDialogController()
{
    if (m_view) {
        m_view->close();
        delete m_view;
        m_view = nullptr;
    }
}

bool SessionDialogController::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_view && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            onCancelled();
            return true;
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            onConfirmed();
            return true;
        }
    }
    return QObject::eventFilter(obj, event);
}

void SessionDialogController::showPrompt(const QString &action, const QPoint &cursorPos)
{
    if (!m_view) return;
    QQuickItem *root = m_view->rootObject();
    if (!root) return;

    QString glyph;
    QString title;
    QString subtitle;
    QString actionText;
    QColor actionColor;

    if (action == QLatin1String("shutdown")) {
        glyph = QStringLiteral("session-shutdown");
        title = QStringLiteral("Shut Down Computer");
        subtitle = QStringLiteral("Are you sure you want to shut down the system?");
        actionText = QStringLiteral("Shut Down");
        actionColor = QColor(QStringLiteral("#e06c75"));
    } else if (action == QLatin1String("reboot")) {
        glyph = QStringLiteral("session-reboot");
        title = QStringLiteral("Restart Computer");
        subtitle = QStringLiteral("Are you sure you want to restart the system?");
        actionText = QStringLiteral("Restart");
        actionColor = QColor(QStringLiteral("#e06c75"));
    } else if (action == QLatin1String("logout")) {
        glyph = QStringLiteral("session-logout");
        title = QStringLiteral("Log Out");
        subtitle = QStringLiteral("Are you sure you want to log out of your session?");
        actionText = QStringLiteral("Log Out");
        actionColor = QColor(QStringLiteral("#e06c75"));
    } else if (action == QLatin1String("suspend")) {
        glyph = QStringLiteral("session-suspend");
        title = QStringLiteral("Sleep");
        subtitle = QStringLiteral("Are you sure you want to enter sleep mode?");
        actionText = QStringLiteral("Sleep");
        actionColor = QColor(QStringLiteral("#e06c75"));
    } else if (action == QLatin1String("switch_user")) {
        glyph = QStringLiteral("session-switch-user");
        title = QStringLiteral("Switch User");
        subtitle = QStringLiteral("Are you sure you want to switch to another user?");
        actionText = QStringLiteral("Switch User");
        actionColor = QColor(QStringLiteral("#e06c75"));
    } else {
        return;
    }

    QString tmpDir = QDir::tempPath() + QStringLiteral("/karyon_icons");
    QString iconPath = KaryonGlyphs::ensureGlyphCached(glyph, tmpDir, 36);

    root->setProperty("iconSource", QStringLiteral("file://") + iconPath);
    root->setProperty("titleText", title);
    root->setProperty("subtitleText", subtitle);
    root->setProperty("actionButtonText", actionText);
    root->setProperty("actionButtonColor", actionColor);

    m_currentAction = action;

    const int dlgWidth = 420;
    const int dlgHeight = 180;
    int x = cursorPos.x() - dlgWidth / 2;
    int y = cursorPos.y() - dlgHeight / 2;

    QScreen *screen = QGuiApplication::screenAt(cursorPos);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect sg = screen->geometry();
        x = std::max(sg.left() + 20, std::min(sg.right() - dlgWidth - 20, x));
        y = std::max(sg.top() + 20, std::min(sg.bottom() - dlgHeight - 20, y));
    }

    m_view->resize(dlgWidth, dlgHeight);
    m_view->setPosition(x, y);
    m_view->show();
    m_view->requestActivate();
    m_view->raise();
    root->forceActiveFocus();
}

void SessionDialogController::hideDialog()
{
    if (m_view) {
        m_view->hide();
    }
}

void SessionDialogController::onConfirmed()
{
    hideDialog();
    QString act = m_currentAction;
    m_currentAction.clear();
    executeAction(act);
}

void SessionDialogController::onCancelled()
{
    hideDialog();
    m_currentAction.clear();
}

void SessionDialogController::executeAction(const QString &action)
{
    if (action == QLatin1String("shutdown")) {
        SessionManager::shutdown();
    } else if (action == QLatin1String("reboot")) {
        SessionManager::reboot();
    } else if (action == QLatin1String("logout")) {
        SessionManager::logout();
    } else if (action == QLatin1String("suspend")) {
        SessionManager::suspend();
    } else if (action == QLatin1String("switch_user")) {
        SessionManager::switchUser();
    }
}

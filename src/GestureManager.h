#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>

class InputMonitor;
class KWinBridge;
class MediaManager;
class VolumeManager;

class GestureManager : public QObject
{
    Q_OBJECT

public:
    // Directions (atan2-based, Y-down screen coords, 45° sectors)
    enum Direction {
        Right     = 0,
        DownRight = 1,
        Down      = 2,
        DownLeft  = 3,
        Left      = 4,
        UpLeft    = 5,
        Up        = 6,
        UpRight   = 7
    };

    struct ActionDef {
        QString id;
        QString displayName;
        std::vector<int> keys; // linux KEY_* codes if simulated via keyboard
    };

    static const QList<ActionDef> &availableActions();
    static QStringList actionIdList();
    static QStringList actionDisplayList();
    static QString displayNameForId(const QString &id);
    static const std::vector<int> &keysForId(const QString &id);
    static bool isGermanLayout();
    static int getZKey();
    static int getYKey();

    static int determineDirection(int deltaX, int deltaY);
    static double directionAngle(int direction);
    static QString directionLabel(int direction);

    explicit GestureManager(QObject *parent = nullptr);

    void executeGesture(int direction, InputMonitor *monitor, KWinBridge *kwinBridge = nullptr,
                        MediaManager *mediaManager = nullptr, VolumeManager *volumeManager = nullptr);
};

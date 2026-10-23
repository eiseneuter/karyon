#pragma once

#include <QObject>
#include <QPoint>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <chrono>

class InputMonitor : public QObject
{
    Q_OBJECT

public:
    explicit InputMonitor(QObject *parent = nullptr);
    ~InputMonitor() override;

    bool start();
    void stop();

    void warpCursor(int targetX, int targetY, int screenW, int screenH);
    void sendKeyCombo(const std::vector<int> &keys);

    void setHoldTriggerMs(int ms) { m_holdTriggerMs.store(ms); }
    int holdTriggerMs() const { return m_holdTriggerMs.load(); }

    void setOverlayMouseSpeed(double speed);
    double overlayMouseSpeed() const { return m_overlayMouseSpeed.load(); }

    bool isLeftDown() const { return m_leftDown.load(); }
    void requestRescan() { m_rescanRequested.store(true); }

    void setGameModeActive(bool active) { m_gameModeActive.store(active); }
    bool isGameModeActive() const { return m_gameModeActive.load(); }

    void setGameModeEnabled(bool enabled) { m_gameModeEnabled.store(enabled); }
    void setTrigger(const QString &type, int button, const QString &keySeq);
    int triggerType() const { return m_triggerType.load(); }
    int triggerButton() const { return m_triggerButton.load(); }

    void setInhibited(bool inhibited) { m_inhibited.store(inhibited); }
    bool isInhibited() const { return m_inhibited.load(); }

    void setRecordingTrigger(bool rec) { m_isRecordingTrigger.store(rec); }
    bool isRecordingTrigger() const { return m_isRecordingTrigger.load(); }

    void requestReleaseButtons() { m_releaseButtonsRequested.store(true); }
    void syncAim(int offsetX, int offsetY);

    // Gesture detection constants
    static constexpr double GESTURE_MIN_DISPLACEMENT = 60.0;
    static constexpr int GESTURE_TIME_WINDOW_MS = 350;

signals:
    void rightPressed();
    void triggerPressed();
    void inputCaptured(const QString &type, int button, const QString &keySeq, const QString &displayName);
    void holdTriggered(int deltaX, int deltaY);
    void holdReleased();
    void holdCancelled();
    void cursorMoved(int offsetX, int offsetY);
    void middleClicked();
    void wheelScrolled(int delta);
    void gestureDetected(int direction, int deltaX, int deltaY);
    void gestureStarted();
    void gesturePointAdded(int deltaX, int deltaY);
    void gestureFinished();
    void resumedFromSleep();
    void mouseConnected(const QString &deviceName);
    void mouseDisconnected();

public slots:
    void onPrepareForSleep(bool sleeping);

private:
    void workerLoop();
    void scanDevices();
    bool rescanDevices();
    void tryGrabPendingDevices();
    void syncKWinMouseSettings();
    void setupUInput();
    void cleanup();

    enum class State {
        Idle,
        Pending,
        Triggered,
        Dragging,
        Cancelled,
        Gesture
    };

    struct DeviceInfo {
        int fd = -1;
        std::string path;
        bool grabbed = false;
        std::string name;
    };

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_rescanRequested{false};
    std::atomic<bool> m_releaseButtonsRequested{false};
    std::atomic<bool> m_leftDown{false};
    std::atomic<int> m_holdTriggerMs{120};
    std::atomic<double> m_overlayMouseSpeed{-0.5};
    std::atomic<bool> m_gameModeActive{false};
    std::atomic<bool> m_gameModeEnabled{true};
    std::atomic<int> m_triggerType{0}; // 0 = mouse, 1 = keyboard
    std::atomic<int> m_triggerButton{273}; // BTN_RIGHT
    std::vector<int> m_triggerKeyCodes;
    bool m_triggerInitialized{false};
    std::atomic<bool> m_inhibited{false};
    std::atomic<bool> m_isRecordingTrigger{false};
    std::atomic<bool> m_syncAimRequested{false};
    std::atomic<int> m_syncAimX{0};
    std::atomic<int> m_syncAimY{-20};
    std::thread m_thread;
    std::vector<DeviceInfo> m_devices;
    std::vector<DeviceInfo> m_keyboards;
    std::string m_lastReportedMouse;
    std::chrono::steady_clock::time_point m_lastReportedTime{};
    std::chrono::steady_clock::time_point m_uinputCreatedTime{};
    int m_uinputFd = -1;
    int m_uinputAbsFd = -1;
    int m_uinputKbdFd = -1;
    int m_inotifyFd = -1;
};

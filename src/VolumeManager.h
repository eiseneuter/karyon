#pragma once

#include <QObject>

class VolumeManager : public QObject
{
    Q_OBJECT

public:
    explicit VolumeManager(QObject *parent = nullptr);
    ~VolumeManager() override = default;

    void refresh();
    int volume() const { return m_volume; }
    int maxVolume() const { return m_maxVolume; }
    bool isMuted() const { return m_muted; }

    void stepVolume(int stepPercent);
    void toggleMute();

signals:
    void volumeChanged(int volume, bool isMuted, int maxVolume);

private:
    void updateMaxVolume();

    int m_volume = 50;
    int m_maxVolume = 100;
    bool m_muted = false;
    bool m_hasWpctl = true;
};

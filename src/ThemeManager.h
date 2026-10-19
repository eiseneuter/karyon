#pragma once

#include <QObject>
#include <QColor>
#include <QString>

struct KaryonTheme
{
    bool isLight = false;

    // Base and background colors
    QColor baseBg;
    QColor cardBg;
    QColor cardBorder;
    QColor dividerColor;
    QColor controlBg;
    QColor controlHoverBg;
    QColor controlBorder;

    // Overlay ring backgrounds and borders
    QColor ring1Bg;
    QColor ring1Border;
    QColor ring2Bg;
    QColor ring2Border;
    QColor ring3Bg;
    QColor ring3Border;
    QColor ringSelectionBg;
    QColor windowSelectionBg;
    QColor windowUnselBg;

    // Accent colors
    QColor accentColor;
    QColor accentHoverColor;
    QColor drilldownColor;

    // Category identification colors
    QColor identWindows;
    QColor identApps;
    QColor identFiles;

    // Text colors
    QColor textPrimary;
    QColor textSecondary;
    QColor textMuted;
    QColor textSubtle;

    // Gesture trail color
    QColor trailColor;
};

class ThemeManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isLight READ isLight NOTIFY themeChanged)
    Q_PROPERTY(int colorSchemeIndex READ colorSchemeIndex NOTIFY themeChanged)
    Q_PROPERTY(QString customBaseColorHex READ customBaseColorHex NOTIFY themeChanged)

    Q_PROPERTY(QColor baseBg READ baseBg NOTIFY themeChanged)
    Q_PROPERTY(QColor cardBg READ cardBg NOTIFY themeChanged)
    Q_PROPERTY(QColor cardBorder READ cardBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor dividerColor READ dividerColor NOTIFY themeChanged)
    Q_PROPERTY(QColor controlBg READ controlBg NOTIFY themeChanged)
    Q_PROPERTY(QColor controlHoverBg READ controlHoverBg NOTIFY themeChanged)
    Q_PROPERTY(QColor controlBorder READ controlBorder NOTIFY themeChanged)

    Q_PROPERTY(QColor ring1Bg READ ring1Bg NOTIFY themeChanged)
    Q_PROPERTY(QColor ring1Border READ ring1Border NOTIFY themeChanged)
    Q_PROPERTY(QColor ring2Bg READ ring2Bg NOTIFY themeChanged)
    Q_PROPERTY(QColor ring2Border READ ring2Border NOTIFY themeChanged)
    Q_PROPERTY(QColor ring3Bg READ ring3Bg NOTIFY themeChanged)
    Q_PROPERTY(QColor ring3Border READ ring3Border NOTIFY themeChanged)
    Q_PROPERTY(QColor ringSelectionBg READ ringSelectionBg NOTIFY themeChanged)
    Q_PROPERTY(QColor windowSelectionBg READ windowSelectionBg NOTIFY themeChanged)
    Q_PROPERTY(QColor windowUnselBg READ windowUnselBg NOTIFY themeChanged)

    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY themeChanged)
    Q_PROPERTY(QColor accentHoverColor READ accentHoverColor NOTIFY themeChanged)
    Q_PROPERTY(QColor drilldownColor READ drilldownColor NOTIFY themeChanged)

    Q_PROPERTY(QColor identWindows READ identWindows NOTIFY themeChanged)
    Q_PROPERTY(QColor identApps READ identApps NOTIFY themeChanged)
    Q_PROPERTY(QColor identFiles READ identFiles NOTIFY themeChanged)

    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY themeChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY themeChanged)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY themeChanged)
    Q_PROPERTY(QColor textSubtle READ textSubtle NOTIFY themeChanged)
    Q_PROPERTY(QColor trailColor READ trailColor NOTIFY themeChanged)

public:
    static ThemeManager &instance();

    bool isLight() const { return m_theme.isLight; }
    int colorSchemeIndex() const;
    QString customBaseColorHex() const;

    QColor baseBg() const { return m_theme.baseBg; }
    QColor cardBg() const { return m_theme.cardBg; }
    QColor cardBorder() const { return m_theme.cardBorder; }
    QColor dividerColor() const { return m_theme.dividerColor; }
    QColor controlBg() const { return m_theme.controlBg; }
    QColor controlHoverBg() const { return m_theme.controlHoverBg; }
    QColor controlBorder() const { return m_theme.controlBorder; }

    QColor ring1Bg() const { return m_theme.ring1Bg; }
    QColor ring1Border() const { return m_theme.ring1Border; }
    QColor ring2Bg() const { return m_theme.ring2Bg; }
    QColor ring2Border() const { return m_theme.ring2Border; }
    QColor ring3Bg() const { return m_theme.ring3Bg; }
    QColor ring3Border() const { return m_theme.ring3Border; }
    QColor ringSelectionBg() const { return m_theme.ringSelectionBg; }
    QColor windowSelectionBg() const { return m_theme.windowSelectionBg; }
    QColor windowUnselBg() const { return m_theme.windowUnselBg; }

    QColor accentColor() const { return m_theme.accentColor; }
    QColor accentHoverColor() const { return m_theme.accentHoverColor; }
    QColor drilldownColor() const { return m_theme.drilldownColor; }

    QColor identWindows() const { return m_theme.identWindows; }
    QColor identApps() const { return m_theme.identApps; }
    QColor identFiles() const { return m_theme.identFiles; }

    QColor textPrimary() const { return m_theme.textPrimary; }
    QColor textSecondary() const { return m_theme.textSecondary; }
    QColor textMuted() const { return m_theme.textMuted; }
    QColor textSubtle() const { return m_theme.textSubtle; }
    QColor trailColor() const { return m_theme.trailColor; }

    void applyTheme(int schemeIndex, const QString &customHex = QString());

signals:
    void themeChanged();

private:
    explicit ThemeManager(QObject *parent = nullptr);

    void loadCharcoalPreset();
    void loadSlatePreset();
    void loadEmeraldPreset();
    void loadAmethystPreset();
    void loadCappuccinoPreset();
    void loadCrimsonPreset();
    void generatePalette(const QColor &baseColor);

    KaryonTheme m_theme;
};

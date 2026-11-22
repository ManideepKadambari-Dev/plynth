#pragma once

#include <QColor>
#include <QObject>
#include <QString>

namespace plynth::app {

// The theme as QML sees it: every color and number the 3D scene uses, read from the one theme file.
class ThemeQml : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor ground READ ground CONSTANT)
    Q_PROPERTY(QColor muted READ muted CONSTANT)
    Q_PROPERTY(QColor signalAmber READ signalAmber CONSTANT)
    Q_PROPERTY(QColor lightColor READ lightColor CONSTANT)
    Q_PROPERTY(QColor skyTopColor READ skyTopColor CONSTANT)
    Q_PROPERTY(QColor skyHorizonColor READ skyHorizonColor CONSTANT)
    Q_PROPERTY(QColor groundHorizonColor READ groundHorizonColor CONSTANT)
    Q_PROPERTY(QColor groundBottomColor READ groundBottomColor CONSTANT)
    Q_PROPERTY(double keyLightBrightness READ keyLightBrightness CONSTANT)
    Q_PROPERTY(double probeExposure READ probeExposure CONSTANT)
    Q_PROPERTY(double aoStrength READ aoStrength CONSTANT)
    Q_PROPERTY(QString uiFamily READ uiFamily CONSTANT)
    Q_PROPERTY(int sizeNormal READ sizeNormal CONSTANT)
    Q_PROPERTY(int unit READ unit CONSTANT)
public:
    explicit ThemeQml(QObject* parent = nullptr) : QObject(parent) {}

    QColor ground() const;
    QColor muted() const;
    QColor signalAmber() const;
    QColor lightColor() const;
    QColor skyTopColor() const;
    QColor skyHorizonColor() const;
    QColor groundHorizonColor() const;
    QColor groundBottomColor() const;
    double keyLightBrightness() const;
    double probeExposure() const;
    double aoStrength() const;
    QString uiFamily() const;
    int sizeNormal() const;
    int unit() const;
};

}

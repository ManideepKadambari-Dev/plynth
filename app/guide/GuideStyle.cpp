#include "GuideStyle.h"

namespace plynth::guide {

// Builds the style sheet from the colors and sizes.
QString guideStyleSheet(const GuideStyle& s)
{
    const QString pad = QString::number(s.unit * 2);
    const QString radius = QString::number(s.radius);
    return QStringLiteral(
               "QWidget#guideRoot { background: %1; color: %2; }"
               "QLabel { color: %2; background: transparent; }"
               "QLabel[muted=\"true\"] { color: %5; }"
               "QPushButton { background: %6; color: %2; border: 1px solid %3; border-radius: %4px; padding: %7px %8px; }"
               "QPushButton:hover { border-color: %5; }"
               "QPushButton:disabled { color: %5; }"
               "QPushButton[primary=\"true\"] { background: %9; color: %10; border-color: %9; }"
               "QLineEdit { background: %11; color: %2; border: 1px solid %3; border-radius: %4px; padding: %7px; }"
               "QLineEdit:focus { border-color: %9; }"
               "QListWidget, QTextBrowser { background: %11; color: %2; border: 1px solid %3; }"
               "QListWidget::item { padding: %7px; }"
               "QListWidget::item:selected { background: %6; color: %2; border-left: 2px solid %9; }")
        .arg(s.panel.name(), s.text.name(), s.line.name(), radius, s.muted.name(), s.raised.name(),
             QString::number(s.unit), pad, s.blue.name())
        .arg(s.onBlue.name(), s.ground.name());
}

}

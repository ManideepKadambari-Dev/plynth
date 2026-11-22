#pragma once

#include <QAbstractButton>
#include <QPixmap>
#include <QString>

namespace plynth::app {

// One material setting in Properties: a swatch of the thumbnail, the material's name and, in Muted under it,
// where it comes from. A material that is not in the library shows its id with "not in the library" in amber.
// Clicking it opens the material picker.
class MaterialRow : public QAbstractButton {
    Q_OBJECT
public:
    explicit MaterialRow(QWidget* parent = nullptr);

    // What the row shows. A null swatch draws an empty square. warning is drawn in amber instead of the note.
    void setContent(const QPixmap& swatch, const QString& name, const QString& note, const QString& warning);
    const QString& name() const { return name_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QPixmap swatch_;
    QString name_;
    QString note_;
    QString warning_;
    bool hover_ = false;
};

}

#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

namespace plynth::app {

// Asks for the name and the location of a new project. The project folder is the location plus the name.
// Create stays off, with a sentence that says why, while the name is not a valid folder name or the folder
// exists and is not empty.
class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    // location is the folder the project goes in, suggestedName the first name shown.
    NewProjectDialog(const QString& location, const QString& suggestedName, QWidget* parent = nullptr);

    QString name() const;
    // The project folder: the location with the name added.
    QString folder() const;

    // For checks only: fills the name and the location as if typed.
    void setFields(const QString& name, const QString& location);

    // The first of "base", "base 2", "base 3" and so on that is not yet a folder in location.
    static QString freeName(const QString& location, const QString& base);
    // Why a project cannot be made with this name in this location, or empty when it can.
    static QString problem(const QString& location, const QString& name);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void browse();
    void validate();

    QLineEdit* name_ = nullptr;
    QLineEdit* location_ = nullptr;
    QLabel* targetCaption_ = nullptr;
    QLabel* target_ = nullptr;
    QLabel* problem_ = nullptr;
    QPushButton* create_ = nullptr;
};

}

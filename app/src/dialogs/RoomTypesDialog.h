#pragma once

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;

namespace plynth::app {

class LibraryHost;

// Edits the room types and the categories of the assets catalog. Each change is saved at once and the
// window redraws room labels and pickers through LibraryHost.
class RoomTypesDialog : public QDialog {
    Q_OBJECT
public:
    RoomTypesDialog(LibraryHost& host, QWidget* parent = nullptr);

private:
    void fillRooms(const QString& select);
    void fillCategories(const QString& select);
    void fillLinks();
    void updateButtons();
    void addRoom();
    void renameRoom();
    void removeRoom();
    void moveRoom(int step);
    void addCategory();
    void renameCategory();
    void removeCategory();
    void linksEdited();
    void report(const QString& text);
    QString askName(const QString& title, const QString& label, const QString& start, bool* ok);
    QPushButton* addButton(QWidget* parent, const QString& text, const QString& tip);

    LibraryHost& host_;
    bool filling_ = false;
    QListWidget* rooms_ = nullptr;
    QListWidget* categories_ = nullptr;
    QListWidget* links_ = nullptr;
    QLabel* linksTitle_ = nullptr;
    QLabel* problem_ = nullptr;
    QPushButton* renameRoom_ = nullptr;
    QPushButton* removeRoom_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
    QPushButton* renameCategory_ = nullptr;
    QPushButton* removeCategory_ = nullptr;
};

}

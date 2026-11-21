#include "TerminalSession.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>

#include "ConPty_win.h"

namespace plynth::terminal {

TerminalSession::TerminalSession(int rows, int columns, QObject* parent)
    : QObject(parent), screen_(rows, columns), pty_(std::make_unique<ConPty>()) {
    screen_.setDamageCallback([this](int first, int last) { emit screenDamaged(first, last); });
    screen_.setOutputCallback([this](const char* bytes, std::size_t size) {
        write(QByteArray(bytes, static_cast<qsizetype>(size)));
    });
    exitTimer_.setInterval(150);
    connect(&exitTimer_, &QTimer::timeout, this, &TerminalSession::checkExit);
}

TerminalSession::~TerminalSession() {
    close();
}

// Picks the shell: pwsh when found, else Windows PowerShell, else the command prompt.
QString TerminalSession::chooseShell() {
    QString pwsh = QStandardPaths::findExecutable(QStringLiteral("pwsh.exe"));
    if (!pwsh.isEmpty()) return QStringLiteral("\"%1\" -NoLogo").arg(QDir::toNativeSeparators(pwsh));
    QString root = qEnvironmentVariable("SystemRoot");
    if (!root.isEmpty()) {
        QString legacy = root + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe");
        if (QFileInfo::exists(legacy)) return QStringLiteral("\"%1\" -NoLogo").arg(QDir::toNativeSeparators(legacy));
    }
    QString comspec = qEnvironmentVariable("COMSPEC");
    if (!comspec.isEmpty()) return QStringLiteral("\"%1\"").arg(comspec);
    return QStringLiteral("cmd.exe");
}

// Starts the program and the thread that reads its output.
bool TerminalSession::start(const SessionOptions& options, QString* error) {
    close();
    stopping_ = false;
    finishedSent_ = false;
    screen_.resize(options.rows, options.columns);

    ConPtyOptions opts;
    QString command = options.commandLine.isEmpty() ? chooseShell() : options.commandLine;
    opts.commandLine = command.toStdWString();
    opts.workingDir = QDir::toNativeSeparators(options.workingDir).toStdWString();
    opts.columns = static_cast<short>(options.columns);
    opts.rows = static_cast<short>(options.rows);
    opts.environment.emplace_back(L"COLORTERM", L"truecolor");
    if (!options.pathPrefix.isEmpty()) {
        QString path = QDir::toNativeSeparators(options.pathPrefix) + QLatin1Char(';') + qEnvironmentVariable("PATH");
        opts.environment.emplace_back(L"PATH", path.toStdWString());
    }

    std::wstring wideError;
    pty_ = std::make_unique<ConPty>();
    if (!pty_->start(opts, &wideError)) {
        if (error) *error = QString::fromStdWString(wideError);
        return false;
    }
    reader_ = std::thread([this] { readerLoop(); });
    exitTimer_.start();
    return true;
}

bool TerminalSession::isRunning() const { return pty_->isOpen() && !pty_->hasExited(); }
bool TerminalSession::hasExited() const { return pty_->isOpen() && pty_->hasExited(); }
int TerminalSession::processCount() const { return pty_->processCount(); }

// Sends bytes to the program.
void TerminalSession::write(const QByteArray& bytes) {
    if (!bytes.isEmpty()) pty_->write(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// Resizes the console and the screen together.
void TerminalSession::resize(int columns, int rows) {
    if (columns < 1 || rows < 1) return;
    screen_.resize(rows, columns);
    pty_->resize(static_cast<short>(columns), static_cast<short>(rows));
}

// Stops the program and its children and waits for the reader thread to end.
void TerminalSession::close() {
    exitTimer_.stop();
    stopping_ = true;
    pty_->close();
    if (reader_.joinable()) reader_.join();
}

// Gives output to the screen.
void TerminalSession::feedOutput(const QByteArray& bytes) {
    screen_.feed(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    emit outputReceived();
}

// Runs on the reader thread. Collects output and hands it to the GUI thread in batches.
void TerminalSession::readerLoop() {
    char buffer[16384];
    // Never stops on a flag: ClosePseudoConsole can wait for the output pipe to drain, so reading
    // goes on until the pipe ends. After close() the data is thrown away.
    for (;;) {
        std::size_t got = pty_->read(buffer, sizeof(buffer));
        if (got == 0) break;
        if (stopping_) continue;
        bool post = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.append(buffer, static_cast<qsizetype>(got));
            post = !deliveryQueued_;
            deliveryQueued_ = true;
        }
        if (post) {
            QMetaObject::invokeMethod(this, [this] {
                QByteArray data;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    data.swap(pending_);
                    deliveryQueued_ = false;
                }
                feedOutput(data);
            }, Qt::QueuedConnection);
        }
    }
}

// Reports once when the first program has ended.
void TerminalSession::checkExit() {
    if (finishedSent_ || !pty_->isOpen() || !pty_->hasExited()) return;
    finishedSent_ = true;
    emit finished(static_cast<int>(pty_->exitCode()));
}

}  // namespace plynth::terminal

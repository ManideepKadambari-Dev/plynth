// A tiny program for the runProcess tests. Every argument that is not an option is printed on its own line
// as "arg:<text>". Options: --delay <ms> between lines, --stderr <text> prints a line on standard error,
// --env <name> prints "env:<name>=<value>", --exit <code> sets the exit code, --hang waits until killed.
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    int delay = 0;
    int exitCode = 0;
    bool hang = false;
    const auto pause = [&]() {
        if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--delay" && i + 1 < argc) {
            delay = std::atoi(argv[++i]);
        } else if (a == "--exit" && i + 1 < argc) {
            exitCode = std::atoi(argv[++i]);
        } else if (a == "--hang") {
            hang = true;
        } else if (a == "--stderr" && i + 1 < argc) {
            std::cerr << argv[++i] << std::endl;
            pause();
        } else if (a == "--env" && i + 1 < argc) {
            const std::string name = argv[++i];
            char* value = nullptr;
            std::size_t size = 0;
            _dupenv_s(&value, &size, name.c_str());
            std::cout << "env:" << name << "=" << (value ? value : "<unset>") << std::endl;
            free(value);
            pause();
        } else {
            std::cout << "arg:" << a << std::endl;
            pause();
        }
    }
    while (hang) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return exitCode;
}

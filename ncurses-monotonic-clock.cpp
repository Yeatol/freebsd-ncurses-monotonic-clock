#include <chrono>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <cstring>

#include <curses.h>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
    if (geteuid() != 0)
    {
        std::fprintf(stderr, "This program must be run as root.\n");
        return 1;
    }

    if (initscr() == nullptr)
    {
        return 1;
    }

    cbreak();
    noecho();
    keypad(stdscr, true);
    timeout(2000);

    mvprintw(0, 0, "Press B to move the clock back 3 seconds during the next 2s wait | Ctrl+C: exit");

    int nextLogRow = 2;
    pid_t pendingClockChange = -1;
    scrollok(stdscr, true);
    if (LINES > 2)
    {
        setscrreg(2, LINES - 1);
    }
    auto appendLog = [&](const char* message) {
        if (LINES > 2 && nextLogRow >= LINES)
        {
            scroll(stdscr);
            nextLogRow = LINES - 1;
        }
        int row = LINES > 2 ? nextLogRow++ : LINES - 1;
        mvprintw(row, 0, "%s", message);
        clrtoeol();
    };
    refresh();

    long long count = 0;
    while (true)
    {
        bool waitForClockChange = pendingClockChange > 0;
        auto start   = std::chrono::steady_clock::now();
        int  key     = getch();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (key == 'b' || key == 'B')
        {
            if (pendingClockChange != -1)
            {
                appendLog("Clock-change helper is already pending.");
            }
            else
            {
                pendingClockChange = fork();
                if (pendingClockChange == -1)
                {
                    char message[128];
                    std::snprintf(message, sizeof(message), "fork failed: %s", std::strerror(errno));
                    appendLog(message);
                }
                else if (pendingClockChange == 0)
                {
                    struct timespec delay{0, 100000000};
                    while (nanosleep(&delay, &delay) == -1)
                    {
                        if (errno != EINTR)
                        {
                            _exit(1);
                        }
                    }

                    struct timespec targetTime;
                    if (clock_gettime(CLOCK_REALTIME, &targetTime) == -1)
                    {
                        _exit(2);
                    }
                    targetTime.tv_sec -= 3;
                    if (clock_settime(CLOCK_REALTIME, &targetTime) == -1)
                    {
                        _exit(3);
                    }
                    _exit(0);
                }
                else
                {
                    appendLog("Clock shift scheduled during the next getch() wait.");
                }
            }
        }
        auto        wallTime   = std::chrono::system_clock::now();
        auto        wallTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(wallTime.time_since_epoch()).count();
        std::time_t wallClock  = std::chrono::system_clock::to_time_t(wallTime);
        std::tm     localTime;
        localtime_r(&wallClock, &localTime);

        char timeText[16];
        std::strftime(timeText, sizeof(timeText), "%H:%M:%S", &localTime);
        ++count;

        if (elapsed > 150)
        {
            if (LINES > 2 && nextLogRow >= LINES)
            {
                scroll(stdscr);
                nextLogRow = LINES - 1;
            }
            int row = LINES > 2 ? nextLogRow++ : LINES - 1;
            if (key == ERR)
            {
                mvprintw(row, 0, "%8lld | %s.%03lld | timeout (ERR) after %lld ms", count, timeText, wallTimeMs % 1000, static_cast<long long>(elapsed));
            }
            else
            {
                mvprintw(row, 0, "%8lld | %s.%03lld | key code %d after %lld ms", count, timeText, wallTimeMs % 1000, key, static_cast<long long>(elapsed));
            }
            clrtoeol();
        }
        else
        {
            move(1, 0);
            clrtoeol();
            if (key == ERR)
            {
                mvprintw(1, 0, "%8lld | %s.%03lld | timeout (ERR) after %lld ms", count, timeText, wallTimeMs % 1000, static_cast<long long>(elapsed));
            }
            else
            {
                mvprintw(1, 0, "%8lld | %s.%03lld | key code %d after %lld ms", count, timeText, wallTimeMs % 1000, key, static_cast<long long>(elapsed));
            }
        }

        if (waitForClockChange && pendingClockChange > 0)
        {
            int status;
            pid_t result;
            do
            {
                result = waitpid(pendingClockChange, &status, 0);
            } while (result == -1 && errno == EINTR);

            if (result == -1)
            {
                char message[128];
                std::snprintf(message, sizeof(message), "waitpid failed: %s", std::strerror(errno));
                appendLog(message);
            }
            else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            {
                appendLog("Clock-change helper failed (clock_settime may require privilege).");
            }
            else
            {
                appendLog("Clock moved backward by 3 seconds during getch().");
            }
            pendingClockChange = -1;
        }

        refresh();
    }

    endwin();

    return 0;
}
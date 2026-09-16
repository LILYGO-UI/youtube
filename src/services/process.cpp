#include "services/process.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

ProcessResult run_process(const std::vector<std::string> &arguments, const std::atomic_bool &cancel,
                          std::chrono::seconds timeout, std::size_t output_limit)
{
    ProcessResult result;
    if (arguments.empty() || cancel) return result;
    int out[2], err[2];
    if (pipe(out) != 0) {
        result.error = std::strerror(errno);
        return result;
    }
    if (pipe(err) != 0) {
        close(out[0]);
        close(out[1]);
        result.error = std::strerror(errno);
        return result;
    }
    for (int fd : {out[0], out[1], err[0], err[1]}) fcntl(fd, F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, out[0]);
    posix_spawn_file_actions_addclose(&actions, err[0]);
    posix_spawn_file_actions_addclose(&actions, out[1]);
    posix_spawn_file_actions_addclose(&actions, err[1]);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    std::vector<char *> argv;
    for (const auto &arg : arguments) argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    pid_t pid         = -1;
    const int spawned = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(out[1]);
    close(err[1]);
    if (spawned != 0) {
        close(out[0]);
        close(err[0]);
        result.error = arguments[0] + ": " + std::strerror(spawned);
        return result;
    }
    fcntl(out[0], F_SETFL, O_NONBLOCK);
    fcntl(err[0], F_SETFL, O_NONBLOCK);
    pollfd descriptors[] = {{out[0], POLLIN, 0}, {err[0], POLLIN, 0}};
    const auto deadline  = std::chrono::steady_clock::now() + timeout;
    int status           = 0;
    bool exited          = false;
    bool aborted         = false;
    while (!exited || descriptors[0].fd >= 0 || descriptors[1].fd >= 0) {
        if (cancel || std::chrono::steady_clock::now() >= deadline || aborted) {
            result.error = cancel ? "Cancelled" : (aborted ? "Response too large" : "Request timed out");
            kill(-pid, SIGKILL);
            if (!exited)
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                }
            aborted = true;
            break;
        }
        poll(descriptors, 2, 40);
        for (int i = 0; i < 2; ++i) {
            auto &descriptor = descriptors[i];
            if (descriptor.fd < 0) continue;
            auto &target = i == 0 ? result.output : result.error;
            char buffer[8192];
            // Bound each drain so a noisy child cannot starve cancellation.
            for (int chunk = 0; chunk < 16; ++chunk) {
                const auto count = read(descriptor.fd, buffer, sizeof(buffer));
                if (count > 0) {
                    const auto limit     = i == 0 ? output_limit : std::size_t(16384);
                    const auto remaining = limit > target.size() ? limit - target.size() : 0;
                    target.append(buffer, std::min(remaining, static_cast<std::size_t>(count)));
                    if (i == 0 && static_cast<std::size_t>(count) > remaining) aborted = true;
                } else {
                    if (count == 0 || (errno != EAGAIN && errno != EINTR)) {
                        close(descriptor.fd);
                        descriptor.fd = -1;
                    }
                    break;
                }
            }
        }
        if (!exited) {
            const auto waited = waitpid(pid, &status, WNOHANG);
            exited            = waited == pid;
            if (waited < 0 && errno != EINTR) {
                aborted = true;
            }
        }
    }
    for (auto &descriptor : descriptors)
        if (descriptor.fd >= 0) close(descriptor.fd);
    if (!aborted) result.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return result;
}

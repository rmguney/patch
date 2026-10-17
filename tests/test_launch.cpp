#include <cstdio>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* A menu-only run renders its frames and must exit on its own */
static const int MENU_TIMEOUT_MS = 60000;
/* Scene runs generate the full world (single-threaded) before rendering, and must exit on their own */
static const int SCENE_TIMEOUT_MS = 300000;
static const int TEST_FRAMES = 10;
/* --test-resize steps the window to a new client size every 4 frames */
static const int RESIZE_TEST_FRAMES = 40;
/* patch_samples exits with this code when --scene names no scene (3 would be abort()'s) */
static const DWORD EXIT_INVALID_SCENE = 2;

enum class Outcome
{
    StillRunning, /* Terminated by the test at the timeout */
    Exited,       /* Exited on its own; see exit_code */
    Crashed,      /* Exited with a Windows exception code */
    LaunchFailed
};

struct LaunchResult
{
    Outcome outcome;
    DWORD exit_code;
    /* Errors + warnings from the validation summary Debug builds print at exit; -1: no summary */
    int validation_messages;
};

/* Inheritable handle to a new temporary file for the app's stdout and stderr */
static HANDLE create_output_file(char (&path)[MAX_PATH])
{
    char dir[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, dir) || !GetTempFileNameA(dir, "pl", 0, path))
        return INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES inherit = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    return CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_TEMPORARY, NULL);
}

static int read_validation_messages(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    int messages = -1;
    char line[1024];
    while (fgets(line, sizeof(line), f))
    {
        unsigned errors = 0;
        unsigned warnings = 0;
        if (sscanf(line, "Vulkan validation summary: %u error(s), %u warning(s)", &errors, &warnings) == 2)
            messages = static_cast<int>(errors + warnings);
    }
    fclose(f);
    return messages;
}

static LaunchResult launch(const char *exe_path, const char *args, int wait_ms)
{
    char cmd_line[512];
    if (args && args[0])
        snprintf(cmd_line, sizeof(cmd_line), "\"%s\" %s", exe_path, args);
    else
        snprintf(cmd_line, sizeof(cmd_line), "\"%s\"", exe_path);

    char output_path[MAX_PATH];
    HANDLE output = create_output_file(output_path);
    if (output == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "  Creating the output file failed (error %lu)\n", GetLastError());
        return {Outcome::LaunchFailed, 0, -1};
    }

    /* The output file is the only handle the app inherits (not this test's own pipes) */
    SIZE_T attributes_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attributes_size);
    std::vector<unsigned char> attributes_storage(attributes_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes_storage.data());
    InitializeProcThreadAttributeList(attributes, 1, 0, &attributes_size);
    UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &output, sizeof(output), NULL, NULL);

    STARTUPINFOEXA si = {};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.StartupInfo.wShowWindow = SW_HIDE;
    si.StartupInfo.hStdOutput = output;
    si.StartupInfo.hStdError = output;
    si.lpAttributeList = attributes;

    PROCESS_INFORMATION pi = {};

    const BOOL created = CreateProcessA(NULL, cmd_line, NULL, NULL, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                       NULL, NULL, &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(output);
    if (!created)
    {
        fprintf(stderr, "  CreateProcess failed (error %lu)\n", GetLastError());
        DeleteFileA(output_path);
        return {Outcome::LaunchFailed, 0, -1};
    }

    LaunchResult result = {Outcome::LaunchFailed, 0, -1};
    DWORD wait_result = WaitForSingleObject(pi.hProcess, wait_ms);
    if (wait_result == WAIT_OBJECT_0)
    {
        GetExitCodeProcess(pi.hProcess, &result.exit_code);
        /* NTSTATUS error codes (0xC0000000-0xCFFFFFFF) are exceptions; -1 (0xFFFFFFFF) is an ordinary error exit */
        bool exception = result.exit_code >= 0xC0000000 && result.exit_code <= 0xCFFFFFFF;
        result.outcome = exception ? Outcome::Crashed : Outcome::Exited;
    }
    else if (wait_result == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 1000);
        result.outcome = Outcome::StillRunning;
    }
    else
    {
        fprintf(stderr, "  WaitForSingleObject failed\n");
        TerminateProcess(pi.hProcess, 1);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    result.validation_messages = read_validation_messages(output_path);
    DeleteFileA(output_path);
    return result;
}

/* Exited on its own, successfully, and (Debug builds) without a Vulkan validation message */
static bool exited_cleanly(const LaunchResult &r)
{
    return r.outcome == Outcome::Exited && r.exit_code == 0 && r.validation_messages <= 0;
}

static void print_failure(const LaunchResult &r)
{
    switch (r.outcome)
    {
    case Outcome::StillRunning:
        printf("FAIL (still running at the timeout)\n");
        break;
    case Outcome::Exited:
        if (r.exit_code == 0)
            printf("FAIL (%d Vulkan validation messages)\n", r.validation_messages);
        else
            printf("FAIL (exit code %lu)\n", r.exit_code);
        break;
    case Outcome::Crashed:
        printf("FAIL (crash 0x%08lX%s)\n", r.exit_code,
               r.exit_code == 0xC0000005   ? " ACCESS_VIOLATION"
               : r.exit_code == 0xC0000094 ? " INTEGER_DIVIDE_BY_ZERO"
               : r.exit_code == 0xC00000FD ? " STACK_OVERFLOW"
                                           : "");
        break;
    case Outcome::LaunchFailed:
        printf("FAIL (launch error)\n");
        break;
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: test_launch <executable>\n");
        return 1;
    }

    const char *exe_path = argv[1];
    int passed = 0;
    int failed = 0;

    /* Inherited by the app: a crash must end the process, not wait on an error dialog that
     * would keep it alive past the wait and read as a pass */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    printf("=== Launch Tests ===\n\n");

    /* Menu only (no scene): renders its frames and exits cleanly (Debug builds: without
     * validation messages) */
    printf("Basic launch... ");
    fflush(stdout);
    char menu_args[64];
    snprintf(menu_args, sizeof(menu_args), "--test-frames %d --profile-csv NUL", TEST_FRAMES);
    LaunchResult result = launch(exe_path, menu_args, MENU_TIMEOUT_MS);
    if (exited_cleanly(result))
    {
        printf("PASS\n");
        passed++;
    }
    else
    {
        print_failure(result);
        failed++;
    }

    /* Every scene renders its frames and exits cleanly; the first unknown scene ID ends the
     * list (and there must be at least one scene) */
    printf("\n=== Scene Render Tests ===\n\n");

    for (int scene_id = 0;; scene_id++)
    {
        char args[256];
        snprintf(args, sizeof(args), "--scene %d --test-frames %d --profile-csv NUL",
                 scene_id, TEST_FRAMES);

        printf("Scene %d... ", scene_id);
        fflush(stdout);

        result = launch(exe_path, args, SCENE_TIMEOUT_MS);

        if (exited_cleanly(result))
        {
            printf("PASS\n");
            passed++;
        }
        else if (result.outcome == Outcome::Exited && result.exit_code == EXIT_INVALID_SCENE && scene_id > 0)
        {
            printf("(end of scenes)\n");
            break;
        }
        else
        {
            print_failure(result);
            failed++;
            break;
        }
    }

    /* Resolution changes: the window steps through degenerate (1x1, 1xN, Nx1), odd and
     * larger-than-1080p client sizes; the app fails the run when a frame after a step is not
     * rendered at exactly that size, and Debug builds must stay free of validation messages */
    printf("\n=== Resize Test ===\n\n");
    printf("Scene 0 resize cycle... ");
    fflush(stdout);
    {
        char args[256];
        snprintf(args, sizeof(args), "--scene 0 --test-frames %d --test-resize --profile-csv NUL", RESIZE_TEST_FRAMES);
        result = launch(exe_path, args, SCENE_TIMEOUT_MS);
    }
    if (exited_cleanly(result))
    {
        printf("PASS\n");
        passed++;
    }
    else
    {
        print_failure(result);
        failed++;
    }

    printf("\nResults: %d passed, %d failed\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}

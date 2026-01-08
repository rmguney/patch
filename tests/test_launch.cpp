#include <cstdio>
#include <cstdlib>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const int LAUNCH_WAIT_MS = 2000;
static const int TEST_FRAMES = 10;

/* Returns: 0=success, 1=crash (exception), 2=launch failed, 3=app error (e.g. invalid scene) */
static int launch_and_test(const char *exe_path, const char *args, int wait_ms)
{
    char cmd_line[512];
    if (args && args[0])
        snprintf(cmd_line, sizeof(cmd_line), "\"%s\" %s", exe_path, args);
    else
        snprintf(cmd_line, sizeof(cmd_line), "\"%s\"", exe_path);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        DWORD err = GetLastError();
        fprintf(stderr, "  CreateProcess failed (error %lu)\n", err);
        return 2;
    }

    DWORD wait_result = WaitForSingleObject(pi.hProcess, wait_ms);

    if (wait_result == WAIT_OBJECT_0)
    {
        DWORD exit_code = 0;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exit_code == 0)
            return 0;

        /* Windows exception codes have high bit set (0xC0000000) */
        if ((exit_code & 0xC0000000) == 0xC0000000)
        {
            fprintf(stderr, "  CRASH: exit code %lu (0x%08lX)", exit_code, exit_code);
            if (exit_code == 0xC0000005)
                fprintf(stderr, " ACCESS_VIOLATION");
            else if (exit_code == 0xC0000094)
                fprintf(stderr, " INTEGER_DIVIDE_BY_ZERO");
            else if (exit_code == 0xC00000FD)
                fprintf(stderr, " STACK_OVERFLOW");
            fprintf(stderr, "\n");
            return 1;
        }

        /* Normal non-zero exit (e.g., invalid scene ID) */
        return 3;
    }
    else if (wait_result == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }
    else
    {
        fprintf(stderr, "  WaitForSingleObject failed\n");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 2;
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

    printf("=== Launch Tests ===\n\n");

    /* Test 1: Basic launch (menu only) */
    printf("Basic launch... ");
    fflush(stdout);
    int result = launch_and_test(exe_path, NULL, LAUNCH_WAIT_MS);
    if (result == 0)
    {
        printf("PASS\n");
        passed++;
    }
    else
    {
        printf("FAIL\n");
        failed++;
    }

    /* Test 2-N: Each scene with rendering (iterate until invalid scene) */
    printf("\n=== Scene Render Tests ===\n\n");

    for (int scene_id = 0;; scene_id++)
    {
        char args[256];
        snprintf(args, sizeof(args), "--scene %d --test-frames %d --profile-csv NUL",
                 scene_id, TEST_FRAMES);

        printf("Scene %d... ", scene_id);
        fflush(stdout);

        result = launch_and_test(exe_path, args, LAUNCH_WAIT_MS * 2);

        if (result == 0)
        {
            printf("PASS\n");
            passed++;
        }
        else if (result == 1)
        {
            printf("FAIL (crash)\n");
            failed++;
        }
        else if (result == 3)
        {
            /* Invalid scene ID - tested all scenes */
            printf("(end of scenes)\n");
            break;
        }
        else
        {
            printf("FAIL (launch error)\n");
            failed++;
            break;
        }
    }

    printf("\nResults: %d passed, %d failed\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}

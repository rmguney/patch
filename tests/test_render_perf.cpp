#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <ctime>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const int TEST_FRAMES = 120;
static const int LAUNCH_WAIT_MS = 5000;

static const float FRAME_BUDGET_MS = 16.667f;
static const char *TEMP_CSV = "profile_temp.csv";

/*
 * Performance Budget Thresholds (RTX 4050M / mid-range laptop)
 */
struct PerfThreshold
{
    float pass_ms; // Green: performance is good
    float warn_ms; // Yellow: approaching limit
    float fail_ms; // Red: regression detected
};

static const PerfThreshold THRESHOLD_50 = {12.0f, 15.0f, 18.0f};
static const PerfThreshold THRESHOLD_250 = {14.0f, 16.67f, 20.0f};
static const PerfThreshold THRESHOLD_500 = {16.0f, 20.0f, 28.0f};
static const PerfThreshold THRESHOLD_1000 = {25.0f, 30.0f, 45.0f};

enum PerfStatus
{
    PERF_PASS,
    PERF_WARN,
    PERF_FAIL
};

static PerfStatus evaluate_perf(float frame_ms, const PerfThreshold &thresh)
{
    if (frame_ms <= thresh.pass_ms)
        return PERF_PASS;
    if (frame_ms <= thresh.warn_ms)
        return PERF_WARN;
    return PERF_FAIL;
}

static const char *status_string(PerfStatus s)
{
    switch (s)
    {
    case PERF_PASS:
        return "PASS";
    case PERF_WARN:
        return "WARN";
    case PERF_FAIL:
        return "FAIL";
    }
    return "UNKNOWN";
}

struct ProfileData
{
    float frame_avg_ms;
    float frame_max_ms;
    float frame_p95_ms;
    float render_total_avg_ms;
    float render_shadow_avg_ms;
    float render_main_avg_ms;
    float render_ui_avg_ms;
    float sim_tick_avg_ms;
    float sim_physics_avg_ms;
    float sim_collision_avg_ms;
    int32_t samples;
    bool valid;
};

static int launch_app(const char *exe_path, const char *args, int wait_ms, int stress_objects = 0)
{
    if (stress_objects > 0)
    {
        char env_val[32];
        snprintf(env_val, sizeof(env_val), "%d", stress_objects);
        SetEnvironmentVariableA("PATCH_STRESS_OBJECTS", env_val);
    }
    else
    {
        SetEnvironmentVariableA("PATCH_STRESS_OBJECTS", NULL);
    }

    char cmd_line[512];
    snprintf(cmd_line, sizeof(cmd_line), "\"%s\" %s", exe_path, args);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        printf("CreateProcess failed (error %lu)\n", GetLastError());
        return 1;
    }

    DWORD wait_result = WaitForSingleObject(pi.hProcess, wait_ms);

    if (wait_result == WAIT_OBJECT_0)
    {
        DWORD exit_code = 0;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (int)exit_code;
    }
    else
    {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return -1;
    }
}

static float parse_csv_value(const char *line, const char *category, int col)
{
    if (strncmp(line, category, strlen(category)) != 0)
        return -1.0f;

    const char *p = line + strlen(category);
    if (*p != ',')
        return -1.0f;
    p++;

    for (int i = 0; i < col; i++)
    {
        p = strchr(p, ',');
        if (!p)
            return -1.0f;
        p++;
    }

    return (float)atof(p);
}

static ProfileData parse_profile_csv(const char *filepath)
{
    ProfileData data = {};
    data.valid = false;

    FILE *f = fopen(filepath, "r");
    if (!f)
        return data;

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n')
            continue;

        if (strncmp(line, "category,", 9) == 0)
            continue;

        float val;

        if ((val = parse_csv_value(line, "frame_total", 0)) >= 0.0f)
        {
            data.frame_avg_ms = val;
            data.frame_max_ms = parse_csv_value(line, "frame_total", 1);
            data.frame_p95_ms = parse_csv_value(line, "frame_total", 4);
            data.samples = (int32_t)parse_csv_value(line, "frame_total", 5);
        }
        else if ((val = parse_csv_value(line, "render_total", 0)) >= 0.0f)
            data.render_total_avg_ms = val;
        else if ((val = parse_csv_value(line, "render_shadow", 0)) >= 0.0f)
            data.render_shadow_avg_ms = val;
        else if ((val = parse_csv_value(line, "render_main", 0)) >= 0.0f)
            data.render_main_avg_ms = val;
        else if ((val = parse_csv_value(line, "render_ui", 0)) >= 0.0f)
            data.render_ui_avg_ms = val;
        else if ((val = parse_csv_value(line, "sim_tick", 0)) >= 0.0f)
            data.sim_tick_avg_ms = val;
        else if ((val = parse_csv_value(line, "sim_physics", 0)) >= 0.0f)
            data.sim_physics_avg_ms = val;
        else if ((val = parse_csv_value(line, "sim_collision", 0)) >= 0.0f)
            data.sim_collision_avg_ms = val;
    }

    fclose(f);
    data.valid = (data.samples > 0);
    return data;
}

static bool run_perf_test(const char *exe_path, const char *test_name, int frames,
                          int stress_objects, const PerfThreshold &threshold,
                          int *passed, int *warned, int *failed)
{
    char args[256];
    snprintf(args, sizeof(args), "--scene 0 --test-frames %d --profile-csv %s", frames, TEMP_CSV);

    printf("\n");
    printf("================================================================================\n");
    printf("  %s\n", test_name);
    printf("================================================================================\n");
    printf("Configuration: %d frames, %d objects\n", frames, stress_objects > 0 ? stress_objects : 10);
    printf("Thresholds: PASS <%.1fms | WARN <%.1fms | FAIL >%.1fms\n\n",
           threshold.pass_ms, threshold.warn_ms, threshold.fail_ms);
    fflush(stdout);

    DeleteFileA(TEMP_CSV);

    int result = launch_app(exe_path, args, LAUNCH_WAIT_MS, stress_objects);
    if (result != 0)
    {
        printf("FAIL: App exited with code %d\n", result);
        (*failed)++;
        return false;
    }

    ProfileData data = parse_profile_csv(TEMP_CSV);
    if (!data.valid)
    {
        printf("FAIL: Could not parse profile data\n");
        (*failed)++;
        return false;
    }

    float frame_budget_pct = (data.frame_avg_ms / FRAME_BUDGET_MS) * 100.0f;
    float render_pct = (data.render_total_avg_ms / FRAME_BUDGET_MS) * 100.0f;
    float sim_pct = (data.sim_tick_avg_ms / FRAME_BUDGET_MS) * 100.0f;
    float effective_fps = 1000.0f / data.frame_avg_ms;

    printf("Frame Timing (target: %.2fms @ 60 FPS):\n", FRAME_BUDGET_MS);
    printf("  Average:     %7.2f ms  (%5.1f%% budget)\n", data.frame_avg_ms, frame_budget_pct);
    printf("  Maximum:     %7.2f ms\n", data.frame_max_ms);
    printf("  95th pct:    %7.2f ms\n", data.frame_p95_ms);
    printf("  Samples:     %d\n", data.samples);
    printf("  Effective:   %.1f FPS\n\n", effective_fps);

    printf("Render Breakdown (%5.1f%% of budget):\n", render_pct);
    printf("  Total:       %7.2f ms\n", data.render_total_avg_ms);
    printf("  Shadow:      %7.2f ms\n", data.render_shadow_avg_ms);
    printf("  Main:        %7.2f ms\n", data.render_main_avg_ms);
    printf("  UI:          %7.2f ms\n\n", data.render_ui_avg_ms);

    printf("Simulation Breakdown (%5.1f%% of budget):\n", sim_pct);
    printf("  Tick:        %7.2f ms\n", data.sim_tick_avg_ms);
    printf("  Physics:     %7.2f ms\n", data.sim_physics_avg_ms);
    printf("  Collision:   %7.2f ms\n\n", data.sim_collision_avg_ms);

    PerfStatus status = evaluate_perf(data.frame_avg_ms, threshold);
    printf("Result: %s (%.2fms avg, threshold: %.1fms pass / %.1fms warn)\n",
           status_string(status), data.frame_avg_ms, threshold.pass_ms, threshold.warn_ms);

    switch (status)
    {
    case PERF_PASS:
        (*passed)++;
        break;
    case PERF_WARN:
        (*warned)++;
        break;
    case PERF_FAIL:
        (*failed)++;
        break;
    }

    return (status != PERF_FAIL);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: test_render_perf <patch_samples.exe>\n");
        return 1;
    }

    const char *exe_path = argv[1];
    int passed = 0;
    int warned = 0;
    int failed = 0;

    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    printf("################################################################################\n");
    printf("#                        PATCH ENGINE BUILD REPORT                            #\n");
    printf("################################################################################\n");
    printf("Generated: %s\n", time_str);
    printf("Executable: %s\n", exe_path);

    run_perf_test(exe_path, "BASELINE (50 objects, 30 frames)", 30, 50,
                  THRESHOLD_50, &passed, &warned, &failed);
    run_perf_test(exe_path, "STRESS TEST (250 objects, 30 frames)", 30, 250,
                  THRESHOLD_250, &passed, &warned, &failed);
    run_perf_test(exe_path, "HEAVY STRESS (500 objects, 30 frames)", 30, 500,
                  THRESHOLD_500, &passed, &warned, &failed);
    run_perf_test(exe_path, "ANXIETY IS KILLING ME (1000 objects, 30 frames)", 30, 1000,
                  THRESHOLD_1000, &passed, &warned, &failed);

    printf("\n");
    printf("################################################################################\n");
    printf("#                              SUMMARY                                        #\n");
    printf("################################################################################\n");
    printf("Tests passed: %d\n", passed);
    printf("Tests warned: %d\n", warned);
    printf("Tests failed: %d\n", failed);

    DeleteFileA(TEMP_CSV);

    return (failed == 0) ? 0 : 1;
}
